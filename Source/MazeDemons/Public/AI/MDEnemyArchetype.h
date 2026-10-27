#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AI/MDAITypes.h"
#include "MDEnemyArchetype.generated.h"

class UMDPoiseProfile;

// One enemy kind (Ghoul, Bone Archer, ...). Stats and AI numbers live here so a Blueprint child of AMDEnemyCharacter carries
// defaults only. Rules that tests need are plain functions on this class.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDEnemyArchetype : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Tag put on every enemy of this archetype (Enemy.Ghoul ...). Quest kill credit matches on it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (Categories = "Enemy"))
	FGameplayTag ArchetypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "1"))
	float MaxHealth = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "1"))
	float MaxPoise = 30.0f;

	// Damage of this enemy's own attacks before combo multipliers (used by the V1 attack execution).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0"))
	float AttackPower = 8.0f;

	// Damage reduction percent, 0-90.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0", ClampMax = "90"))
	float BaseDefense = 0.0f;

	// cm/s
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0"))
	float MoveSpeed = 450.0f;

	// Gold shared among the players in range when this enemy dies (the U2 kill-gold split). 0 = none.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards", meta = (ClampMin = "0"))
	int32 KillGold = 5;

	// Budget the spawner spends on one of these (V1 spawn budgets).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning", meta = (ClampMin = "0"))
	int32 SpawnPointCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	EMDIntelligenceTier MinTier = EMDIntelligenceTier::T0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	EMDIntelligenceTier MaxTier = EMDIntelligenceTier::T3;

	// Reaction montages and poise tuning. Its MaxPoise is overridden by this asset's MaxPoise (see ResolvePoiseProfile).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UMDPoiseProfile> PoiseProfile;

	// Cosmetic physical flinch (md.Combat.PhysicalReactions). Enemies keep it; informational until the poise component
	// gains a per-owner switch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bPhysicalReactions = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	FMDArchetypeAIParams AIParams;

	// ---- Rules ----

	// The tags a dead enemy of this archetype carries for kill credit (empty when ArchetypeTag is unset).
	FGameplayTagContainer GetVictimTags() const;

	// Gold awarded on death, never negative.
	int32 GetKillGold() const { return FMath::Max(KillGold, 0); }

	// MinTier <= Tier <= MaxTier (a swapped min/max is treated as the range between them).
	bool AllowsTier(EMDIntelligenceTier Tier) const;

	// The poise profile an enemy of this archetype should run: PoiseProfile when its MaxPoise already equals MaxPoise,
	// otherwise a copy (owned by Outer) with MaxPoise replaced; with no profile, a default one with this MaxPoise.
	UMDPoiseProfile* ResolvePoiseProfile(UObject* Outer) const;

	// True for Enemy.* tags (the archetype family).
	static bool IsEnemyTag(const FGameplayTag& Tag);
};
