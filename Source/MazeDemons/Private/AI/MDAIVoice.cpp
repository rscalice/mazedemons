#include "AI/MDAIVoice.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "TimerManager.h"

// ---------------------------------------------------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------------------------------------------------

FMDVoiceRules FMDVoiceRules::ForKind(EMDVoiceKind Kind)
{
	FMDVoiceRules R;
	switch (Kind)
	{
	case EMDVoiceKind::Tell:
		// The Fab growls run 2 - 4 s; a tell is the first 0.7 s. Two at once at most, one per enemy per 1.2 s (an attack
		// cooldown is 0.8 s), and the listener never hears two tells start within 0.3 s of each other.
		R.MaxConcurrent = 2;
		R.EnemyCooldown = 1.2;
		R.ListenerInterval = 0.3;
		R.MaxSeconds = 0.7f;
		R.VolumeScale = 0.7f;
		break;
	case EMDVoiceKind::Bark:
		R.MaxConcurrent = 1;
		R.EnemyCooldown = 4.0;
		R.ListenerInterval = 1.0;
		R.MaxSeconds = 1.5f;
		R.VolumeScale = 0.8f;
		break;
	case EMDVoiceKind::Sting:
		R.MaxConcurrent = 1;
		R.EnemyCooldown = 2.0;
		R.ListenerInterval = 2.0; // max 1 sting per 2 s, whoever is swinging off-screen
		R.MaxSeconds = 1.0f;
		R.VolumeScale = 1.0f;
		break;
	}
	return R;
}

namespace MDAIVoiceLocal
{
	uint64 Key(EMDVoiceKind Kind, int32 EnemyId)
	{
		return (static_cast<uint64>(static_cast<uint8>(Kind)) << 32) | static_cast<uint32>(EnemyId);
	}
}

void FMDVoiceGate::Purge(double Now)
{
	Playing.RemoveAll([Now](const FPlaying& P) { return Now >= P.EndTime; });
}

int32 FMDVoiceGate::NumPlaying(EMDVoiceKind Kind, double Now) const
{
	int32 N = 0;
	for (const FPlaying& P : Playing)
	{
		N += (P.Kind == Kind && Now < P.EndTime) ? 1 : 0;
	}
	return N;
}

bool FMDVoiceGate::TryStart(EMDVoiceKind Kind, int32 EnemyId, double Now, FString* OutReason)
{
	Purge(Now);
	const FMDVoiceRules R = FMDVoiceRules::ForKind(Kind);
	const int32 KindIndex = FMath::Clamp(static_cast<int32>(Kind), 0, 2);
	if (Now - LastByKind[KindIndex] < R.ListenerInterval)
	{
		if (OutReason) { *OutReason = TEXT("listener interval"); }
		return false;
	}
	if (const double* Last = LastByEnemy.Find(MDAIVoiceLocal::Key(Kind, EnemyId)); Last && Now - *Last < R.EnemyCooldown)
	{
		if (OutReason) { *OutReason = TEXT("enemy cooldown"); }
		return false;
	}
	if (NumPlaying(Kind, Now) >= R.MaxConcurrent)
	{
		if (OutReason) { *OutReason = TEXT("concurrency"); }
		return false;
	}
	FPlaying& P = Playing.AddDefaulted_GetRef();
	P.Kind = Kind;
	P.EnemyId = EnemyId;
	P.EndTime = Now + static_cast<double>(R.MaxSeconds);
	LastByEnemy.Add(MDAIVoiceLocal::Key(Kind, EnemyId), Now);
	LastByKind[KindIndex] = Now;
	return true;
}

void FMDVoiceGate::Reset()
{
	Playing.Reset();
	LastByEnemy.Reset();
	for (double& T : LastByKind)
	{
		T = -1000.0;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------------------------------------------------

UMDAIVoiceSubsystem* UMDAIVoiceSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMDAIVoiceSubsystem>() : nullptr;
}

bool UMDAIVoiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UMDAIVoiceSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		for (FTimerHandle& Handle : CutTimers)
		{
			World->GetTimerManager().ClearTimer(Handle);
		}
	}
	CutTimers.Reset();
	Super::Deinitialize();
}

USoundConcurrency* UMDAIVoiceSubsystem::ConcurrencyFor(EMDVoiceKind Kind)
{
	const int32 Index = FMath::Clamp(static_cast<int32>(Kind), 0, 2);
	while (Concurrency.Num() <= Index)
	{
		Concurrency.Add(nullptr);
	}
	if (!Concurrency[Index])
	{
		// Transient, code-owned (no Fab cue is edited, no asset to keep in step with the rules): MaxConcurrent voices of
		// this kind, the newest one prevented when they are all busy.
		USoundConcurrency* C = NewObject<USoundConcurrency>(this);
		C->Concurrency.bLimitToOwner = false;
		C->Concurrency.MaxCount = FMDVoiceRules::ForKind(Kind).MaxConcurrent;
		C->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
		Concurrency[Index] = C;
	}
	return Concurrency[Index];
}

UAudioComponent* UMDAIVoiceSubsystem::Play(EMDVoiceKind Kind, int32 EnemyId, USoundBase* Sound, const FVector& Location, float Volume)
{
	UWorld* World = GetWorld();
	if (!World || !Sound || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (!Gate.TryStart(Kind, EnemyId, World->GetTimeSeconds()))
	{
		return nullptr;
	}
	const FMDVoiceRules R = FMDVoiceRules::ForKind(Kind);
	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume * R.VolumeScale, 1.0f, 0.0f,
		nullptr, ConcurrencyFor(Kind), true);
	if (Component && R.MaxSeconds > 0.0f)
	{
		// The cut: fade the line out at MaxSeconds (a 0.12 s fade so it does not click).
		FTimerHandle& Handle = CutTimers.AddDefaulted_GetRef();
		TWeakObjectPtr<UAudioComponent> Weak(Component);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UMDAIVoiceSubsystem::CutLine, Weak), R.MaxSeconds, false);
		if (CutTimers.Num() > 64)
		{
			CutTimers.RemoveAt(0, CutTimers.Num() - 32);
		}
	}
	return Component;
}

void UMDAIVoiceSubsystem::CutLine(TWeakObjectPtr<UAudioComponent> Component)
{
	if (UAudioComponent* C = Component.Get(); C && C->IsPlaying())
	{
		C->FadeOut(0.12f, 0.0f);
	}
}
