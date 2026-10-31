#include "LockOn/MDPawnSeparationSubsystem.h"

#include "AI/MDEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "LockOn/MDLockOnRules.h"
#include "MDGameplayStatics.h"

namespace MDPawnSepLocal
{
	TAutoConsoleVariable<int32> CVarBlock(TEXT("md.Pawn.Block"), 1,
		TEXT("1 (default) = living character capsules block the Pawn channel (player/player, player/enemy). 0 = leave the Blueprint settings alone."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarSeparation(TEXT("md.Pawn.Separation"), 1,
		TEXT("1 (default) = server eases overlapping character capsules apart. 0 = off."), ECVF_Default);
	TAutoConsoleVariable<float> CVarRate(TEXT("md.Pawn.SeparationRate"), 10.0f,
		TEXT("Fraction of the overlap corrected per second (default 10 = most of it within a tenth of a second)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarMaxSpeed(TEXT("md.Pawn.SeparationMaxSpeed"), 350.0f,
		TEXT("Fastest separation push, cm/s."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarDebug(TEXT("md.Pawn.Debug"), 0,
		TEXT("1 = log every separation push and every capsule changed to block."), ECVF_Default);

	constexpr float Skin = 2.0f;          // cm of daylight left between capsules
	constexpr float MinPenetration = 1.0f; // cm of overlap tolerated before pushing
	constexpr float EnforceInterval = 0.5f;

	bool IsDeadCharacter(const ACharacter* Character)
	{
		if (const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Character))
		{
			if (Enemy->IsEnemyDead())
			{
				return true;
			}
		}
		return UMDGameplayStatics::IsPawnDead(Character);
	}

	bool IsParticipant(const ACharacter* Character)
	{
		if (!Character || Character->IsPendingKillPending() || IsDeadCharacter(Character))
		{
			return false;
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		return Capsule && Capsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision && (Character->IsPlayerControlled() || Cast<AMDEnemyCharacter>(Character));
	}

	bool IsPlayer(const ACharacter* Character)
	{
		return Character->IsPlayerControlled() || Character->GetPlayerState() != nullptr;
	}
}

float UMDPawnSeparationSubsystem::PushShare(bool bSelfIsPlayer, bool bOtherIsPlayer)
{
	if (bSelfIsPlayer && bOtherIsPlayer)
	{
		return 0.5f;
	}
	if (bSelfIsPlayer != bOtherIsPlayer)
	{
		return bSelfIsPlayer ? 0.0f : 1.0f; // the enemy gives way
	}
	return 0.5f; // enemy / enemy
}

bool UMDPawnSeparationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

bool UMDPawnSeparationSubsystem::IsTickable() const
{
	return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld();
}

TStatId UMDPawnSeparationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDPawnSeparationSubsystem, STATGROUP_Tickables);
}

void UMDPawnSeparationSubsystem::EnforceBlocking()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Character = *It;
		if (!MDPawnSepLocal::IsParticipant(Character))
		{
			continue;
		}
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		if (Capsule->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			if (MDPawnSepLocal::CVarDebug.GetValueOnGameThread() != 0)
			{
				UE_LOG(LogTemp, Display, TEXT("PawnSeparation: %s capsule now blocks Pawn"), *Character->GetName());
			}
		}
	}
}

void UMDPawnSeparationSubsystem::Separate(float DeltaTime)
{
	using namespace MDPawnSepLocal;
	UWorld* World = GetWorld();
	TArray<ACharacter*> Characters;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (IsParticipant(*It))
		{
			Characters.Add(*It);
		}
	}
	const float Rate = FMath::Clamp(CVarRate.GetValueOnGameThread() * DeltaTime, 0.0f, 1.0f);
	const float MaxPush = CVarMaxSpeed.GetValueOnGameThread() * DeltaTime;
	for (int32 i = 0; i < Characters.Num(); ++i)
	{
		for (int32 j = i + 1; j < Characters.Num(); ++j)
		{
			ACharacter* A = Characters[i];
			ACharacter* B = Characters[j];
			const UCapsuleComponent* CA = A->GetCapsuleComponent();
			const UCapsuleComponent* CB = B->GetCapsuleComponent();
			const FVector LA = A->GetActorLocation();
			const FVector LB = B->GetActorLocation();
			// Different heights (one on a ledge above the other) is not an overlap.
			if (FMath::Abs(LA.Z - LB.Z) > CA->GetScaledCapsuleHalfHeight() + CB->GetScaledCapsuleHalfHeight())
			{
				continue;
			}
			const float RA = CA->GetScaledCapsuleRadius();
			const float RB = CB->GetScaledCapsuleRadius();
			const bool bPlayerA = IsPlayer(A);
			const bool bPlayerB = IsPlayer(B);
			const FVector PushA = FMDPawnSeparation::ComputePush(LA, RA, LB, RB, Skin, MinPenetration, Rate, MaxPush, PushShare(bPlayerA, bPlayerB));
			const FVector PushB = FMDPawnSeparation::ComputePush(LB, RB, LA, RA, Skin, MinPenetration, Rate, MaxPush, PushShare(bPlayerB, bPlayerA));
			if (!PushA.IsNearlyZero())
			{
				A->AddActorWorldOffset(PushA, true);
				++LastPushCount;
			}
			if (!PushB.IsNearlyZero())
			{
				B->AddActorWorldOffset(PushB, true);
				++LastPushCount;
			}
			if (CVarDebug.GetValueOnGameThread() != 0 && (!PushA.IsNearlyZero() || !PushB.IsNearlyZero()))
			{
				UE_LOG(LogTemp, Display, TEXT("PawnSeparation: %s <-> %s overlap %.1f push %s / %s"), *A->GetName(), *B->GetName(),
					FMDPawnSeparation::Penetration(LA, RA, LB, RB), *PushA.ToString(), *PushB.ToString());
			}
		}
	}
}

void UMDPawnSeparationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	LastPushCount = 0;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (MDPawnSepLocal::CVarBlock.GetValueOnGameThread() != 0)
	{
		EnforceTimer += DeltaTime;
		if (EnforceTimer >= MDPawnSepLocal::EnforceInterval)
		{
			EnforceTimer = 0.0f;
			EnforceBlocking();
		}
	}
	// The separation push is server authority (standalone counts); clients just see the replicated result.
	if (World->GetNetMode() != NM_Client && MDPawnSepLocal::CVarSeparation.GetValueOnGameThread() != 0)
	{
		Separate(DeltaTime);
	}
}
