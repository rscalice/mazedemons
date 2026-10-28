#include "AI/MDTestArchetypes.h"

#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Ranged/MDRangedWeaponData.h"
#include "UObject/UObjectGlobals.h"

namespace MDTestArchetypes
{
	bool ParseKind(const FString& Text, EKind& OutKind)
	{
		if (Text.Equals(TEXT("archer"), ESearchCase::IgnoreCase)) { OutKind = EKind::Archer; return true; }
		if (Text.Equals(TEXT("brute"), ESearchCase::IgnoreCase)) { OutKind = EKind::Brute; return true; }
		if (Text.Equals(TEXT("werewolf"), ESearchCase::IgnoreCase)) { OutKind = EKind::Werewolf; return true; }
		if (Text.Equals(TEXT("caster"), ESearchCase::IgnoreCase)) { OutKind = EKind::Caster; return true; }
		return false;
	}

	UMDEnemyArchetype* Make(UObject* Outer, EKind Kind)
	{
		UMDEnemyArchetype* A = NewObject<UMDEnemyArchetype>(Outer ? Outer : GetTransientPackage());
		A->MaxHealth = 80.0f;
		A->MaxPoise = 40.0f;
		A->MoveSpeed = 420.0f;
		A->MinTier = EMDIntelligenceTier::T0;
		A->MaxTier = EMDIntelligenceTier::T4;

		FMDEnemyAttack Swipe;
		Swipe.Name = TEXT("Swipe");
		Swipe.Range = 200.0f;
		Swipe.TellTime = 0.8f;
		Swipe.Cooldown = 0.8f;

		FMDArchetypeAIParams& AI = A->AIParams;
		switch (Kind)
		{
		case EKind::Archer:
			A->DisplayName = FText::FromString(TEXT("Test Archer"));
			A->RangedData = LoadObject<UMDRangedWeaponData>(nullptr, TEXT("/Game/Data/Ranged/DA_Ranged_HollowPistol.DA_Ranged_HollowPistol"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			AI.PreferredRange = 900.0f;
			AI.ThreatRange = 1100.0f;
			AI.bKiter = true;
			AI.AllowedRoles = { EMDRole::Harasser, EMDRole::Striker };
			Swipe.Name = TEXT("Shot");
			Swipe.Range = 1100.0f;
			Swipe.Cooldown = 2.0f;
			A->Attacks = { Swipe };
			break;
		case EKind::Brute:
			A->DisplayName = FText::FromString(TEXT("Test Brute"));
			A->MaxHealth = 200.0f;
			A->MaxPoise = 120.0f;
			AI.PreferredRange = 220.0f;
			AI.bCanCharge = true;
			AI.PunishGreedHits = 3;
			AI.AllowedRoles = { EMDRole::Striker, EMDRole::Leader, EMDRole::Guard };
			Swipe.Range = 240.0f;
			{
				FMDEnemyAttack Slam = Swipe;
				Slam.Name = TEXT("Slam");
				Slam.TellTime = 1.1f;
				Slam.Step.PoiseDamage = 40.0f;
				A->Attacks = { Swipe, Slam };
			}
			break;
		case EKind::Werewolf:
			A->DisplayName = FText::FromString(TEXT("Test Werewolf"));
			AI.bCanBlink = true;
			AI.AllowedRoles = { EMDRole::Striker, EMDRole::Flanker };
			A->Attacks = { Swipe };
			break;
		case EKind::Caster:
			A->DisplayName = FText::FromString(TEXT("Test Caster"));
			AI.PreferredRange = 1100.0f;
			AI.ThreatRange = 1300.0f;
			AI.bIsCaster = true;
			AI.AllowedRoles = { EMDRole::Harasser };
			break;
		}
		return A;
	}
}

// md.AI.SpawnTest <archer|brute|werewolf|caster> [count]: spawns count ghoul-bodied enemies of a code-built test archetype 6 m
// in front of the first local player (server / standalone PIE only). Content for these archetypes is V2 / WP10.
static FAutoConsoleCommandWithWorldAndArgs GMDSpawnTestEnemy(
	TEXT("md.AI.SpawnTest"),
	TEXT("md.AI.SpawnTest <archer|brute|werewolf|caster> [count]: spawn test-archetype enemies in front of the player (host only)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		MDTestArchetypes::EKind Kind;
		if (!World || Args.Num() < 1 || !MDTestArchetypes::ParseKind(Args[0], Kind) || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("md.AI.SpawnTest <archer|brute|werewolf|caster> [count] (host only)"));
			return;
		}
		const int32 Count = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 8) : 1;
		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		UClass* Class = LoadClass<AMDEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemies/BP_Enemy_Ghoul.BP_Enemy_Ghoul_C"));
		if (!Pawn || !Class)
		{
			return;
		}
		UMDEnemyArchetype* Archetype = MDTestArchetypes::Make(World, Kind);
		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector Right = Pawn->GetActorRightVector();
		const FName Group(*FString::Printf(TEXT("SpawnTest_%d"), static_cast<int32>(World->GetTimeSeconds() * 10.0)));
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector Offset = Forward * 600.0 + Right * ((i - (Count - 1) * 0.5) * 150.0);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			AMDEnemyCharacter* Enemy = World->SpawnActorDeferred<AMDEnemyCharacter>(Class, FTransform(FRotator(0.0, (-Forward).Rotation().Yaw, 0.0), Pawn->GetActorLocation() + Offset), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (Enemy)
			{
				Enemy->Archetype = Archetype;
				Enemy->SpawnGroupId = Group;
				Enemy->FinishSpawning(FTransform(FRotator(0.0, (-Forward).Rotation().Yaw, 0.0), Pawn->GetActorLocation() + Offset));
			}
		}
	}));
