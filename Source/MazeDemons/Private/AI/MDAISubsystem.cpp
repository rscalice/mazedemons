#include "AI/MDAISubsystem.h"

#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

namespace MDAISubsystemLocal
{
	TAutoConsoleVariable<int32> CVarDebug(TEXT("md.AI.Debug"), 0,
		TEXT("1 = draw each enemy's intent, top 3 scores and token state above it (server)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarForceTier(TEXT("md.AI.ForceTier"), -1,
		TEXT("-1 = off (T0 until V1 WP4 computes tiers). 0..4 = every enemy brain runs at that intelligence tier."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarBrainHz(TEXT("md.AI.BrainHz"), 8.0f,
		TEXT("Decisions per second per enemy brain (5-10)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarSeed(TEXT("md.AI.Seed"), 0,
		TEXT("Run seed for the enemy brains' noise. 0 = drawn once per world (logged)."), ECVF_Cheat);
}

UMDAISubsystem* UMDAISubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMDAISubsystem>() : nullptr;
}

bool UMDAISubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld() || World->WorldType == EWorldType::Game);
}

void UMDAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RunSeed = MDAISubsystemLocal::CVarSeed.GetValueOnGameThread();
	if (RunSeed == 0)
	{
		RunSeed = static_cast<int32>(FPlatformTime::Cycles() & 0x7fffffff);
		if (RunSeed == 0)
		{
			RunSeed = 1;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("MDAISubsystem: run seed %d (md.AI.Seed to fix it)"), RunSeed);
}

bool UMDAISubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && !IsTemplate();
}

TStatId UMDAISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDAISubsystem, STATGROUP_Tickables);
}

double UMDAISubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

EMDIntelligenceTier UMDAISubsystem::ResolveTier()
{
	const int32 Forced = MDAISubsystemLocal::CVarForceTier.GetValueOnGameThread();
	if (Forced >= 0)
	{
		return static_cast<EMDIntelligenceTier>(FMath::Clamp(Forced, 0, static_cast<int32>(EMDIntelligenceTier::T4)));
	}
	return EMDIntelligenceTier::T0;
}

bool UMDAISubsystem::IsDebugEnabled()
{
	return MDAISubsystemLocal::CVarDebug.GetValueOnGameThread() != 0;
}

float UMDAISubsystem::GetBrainHz()
{
	return FMath::Clamp(MDAISubsystemLocal::CVarBrainHz.GetValueOnGameThread(), 1.0f, 30.0f);
}

// ---- Brains ----

int32 UMDAISubsystem::RegisterBrain(UMDEnemyBrainComponent* Brain)
{
	if (!Brain)
	{
		return 0;
	}
	Brains.AddUnique(Brain);
	return NextEnemyId++;
}

void UMDAISubsystem::UnregisterBrain(UMDEnemyBrainComponent* Brain)
{
	const int32 Index = Brains.IndexOfByPredicate([Brain](const TWeakObjectPtr<UMDEnemyBrainComponent>& B) { return B.Get() == Brain; });
	if (Index != INDEX_NONE)
	{
		Brains.RemoveAt(Index);
		if (Cursor > Index)
		{
			--Cursor;
		}
	}
	if (AActor* Enemy = Brain ? static_cast<AActor*>(Brain->GetEnemy()) : nullptr)
	{
		ReleaseAttackToken(Enemy); // tokens are keyed on the pawn, not the controller the brain lives on
	}
}

int32 UMDAISubsystem::StepScheduler(float DeltaTime)
{
	// Drop dead entries first so the batch covers live brains only.
	Brains.RemoveAll([](const TWeakObjectPtr<UMDEnemyBrainComponent>& B) { return !B.IsValid(); });
	if (Brains.Num() == 0)
	{
		Cursor = 0;
		Accumulator = 0.0f;
		return 0;
	}
	const int32 Batch = MDBrainScheduling::BatchSize(Brains.Num(), GetBrainHz(), DeltaTime, Accumulator);
	const double Time = Now();
	for (int32 i = 0; i < Batch; ++i)
	{
		if (Cursor >= Brains.Num())
		{
			Cursor = 0;
		}
		if (UMDEnemyBrainComponent* Brain = Brains[Cursor].Get())
		{
			Brain->RunDecision(Time);
		}
		++Cursor;
	}
	return Batch;
}

void UMDAISubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Tokens.Purge(Now());
	StepScheduler(DeltaTime);
}

// ---- Tokens ----

int32 UMDAISubsystem::GetActorId(const AActor* Actor)
{
	if (!Actor)
	{
		return 0;
	}
	if (const int32* Found = ActorIds.Find(Actor))
	{
		return *Found;
	}
	// Forget stale actors now and then so the map does not grow with every spawn.
	if (ActorIds.Num() > 256)
	{
		for (auto It = ActorIds.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	const int32 Id = NextActorId++;
	ActorIds.Add(Actor, Id);
	return Id;
}

bool UMDAISubsystem::RequestAttackToken(AActor* Enemy, AActor* Target)
{
	if (!Enemy || !Target)
	{
		return false;
	}
	return Tokens.TryGrant(GetActorId(Enemy), GetActorId(Target), Now(), ResolveTierParams().MaxTokensPerPlayer);
}

bool UMDAISubsystem::HoldsAttackToken(const AActor* Enemy) const
{
	const int32* Id = Enemy ? ActorIds.Find(Enemy) : nullptr;
	return Id && Tokens.Holds(*Id, Now());
}

void UMDAISubsystem::ReleaseAttackToken(AActor* Enemy)
{
	if (const int32* Id = Enemy ? ActorIds.Find(Enemy) : nullptr)
	{
		Tokens.Release(*Id);
	}
}

void UMDAISubsystem::ReleaseAttackTokenAfter(AActor* Enemy, double Delay)
{
	if (const int32* Id = Enemy ? ActorIds.Find(Enemy) : nullptr)
	{
		Tokens.ReleaseAfter(*Id, Now(), Delay);
	}
}

int32 UMDAISubsystem::CountAttackTokensOn(const AActor* Target) const
{
	const int32* Id = Target ? ActorIds.Find(Target) : nullptr;
	return Id ? Tokens.CountForTarget(*Id, Now()) : 0;
}
