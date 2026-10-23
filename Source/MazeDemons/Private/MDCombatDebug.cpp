#include "MDCombatDebug.h"

#include "MDPoiseComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace MDCombatDebugLocal
{
	TAutoConsoleVariable<int32> CVarDebug(TEXT("md.Combat.Debug"), 0,
		TEXT("Show combo step, windows, buffer, target poise and last reaction on screen."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarDrawTraces(TEXT("md.Combat.DrawTraces"), 0,
		TEXT("Draw weapon traces and impact points."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarHitStopScale(TEXT("md.Combat.HitStopScale"), 1.0f,
		TEXT("Multiplier on hit-stop length (0 = off)."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarShakeScale(TEXT("md.Combat.ShakeScale"), 1.0f,
		TEXT("Multiplier on combat camera shake (0 = off)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarForceReaction(TEXT("md.Combat.ForceReaction"), 0,
		TEXT("0 = off. Otherwise every landed hit resolves to this reaction: 1 Flinch, 2 Stagger, 3 Knockback, 4 Knockdown."), ECVF_Cheat);

	// The poise component of the actor the first local player is looking at, else of their pawn.
	UMDPoiseComponent* FindDebugTarget(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		if (!PC)
		{
			return nullptr;
		}
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MDCombatDebugTarget), false, PC->GetPawn());
		if (World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.0f, ECC_Visibility, Params))
		{
			if (UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Hit.GetActor()))
			{
				return Poise;
			}
		}
		return UMDPoiseComponent::FindFor(PC->GetPawn());
	}

	void RunSetPoise(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("MDSetPoise <value>: sets the poise of the target under the crosshair (else your pawn). Server / standalone only."));
			return;
		}
		UMDPoiseComponent* Poise = FindDebugTarget(World);
		if (!Poise || !Poise->GetOwner() || !Poise->GetOwner()->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("MDSetPoise: no authoritative poise target."));
			return;
		}
		Poise->SetPoise(FCString::Atof(*Args[0]));
		UE_LOG(LogTemp, Log, TEXT("MDSetPoise: %s poise is now %.0f%%"), *GetNameSafe(Poise->GetOwner()), Poise->GetPoiseFraction() * 100.0f);
	}

	void RunStun(const TArray<FString>& Args, UWorld* World)
	{
		UMDPoiseComponent* Poise = FindDebugTarget(World);
		if (!Poise || !Poise->GetOwner() || !Poise->GetOwner()->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("MDStun: no authoritative poise target."));
			return;
		}
		Poise->Stun(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f);
		UE_LOG(LogTemp, Log, TEXT("MDStun: %s stunned"), *GetNameSafe(Poise->GetOwner()));
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSetPoise(TEXT("MDSetPoise"),
		TEXT("MDSetPoise <value>: set the poise of the target under the crosshair (else your pawn)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSetPoise));

	FAutoConsoleCommandWithWorldAndArgs CmdStun(TEXT("MDStun"),
		TEXT("MDStun [seconds]: stun the target under the crosshair (else your pawn) for the profile duration or the given seconds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunStun));
}

namespace MDCombatDebug
{
	bool IsDebugEnabled()
	{
		return MDCombatDebugLocal::CVarDebug.GetValueOnGameThread() != 0;
	}

	bool ShouldDrawTraces()
	{
		return MDCombatDebugLocal::CVarDrawTraces.GetValueOnGameThread() != 0;
	}

	float HitStopScale()
	{
		return FMath::Max(MDCombatDebugLocal::CVarHitStopScale.GetValueOnGameThread(), 0.0f);
	}

	float ShakeScale()
	{
		return FMath::Max(MDCombatDebugLocal::CVarShakeScale.GetValueOnGameThread(), 0.0f);
	}

	int32 ForceReaction()
	{
		return MDCombatDebugLocal::CVarForceReaction.GetValueOnGameThread();
	}
}
