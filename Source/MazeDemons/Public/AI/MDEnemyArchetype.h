#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AI/MDAITypes.h"
#include "MDCombatTypes.h"
#include "MDEnemyArchetype.generated.h"

class UAnimMontage;
class UMDPoiseProfile;
class USoundBase;

// One attack of an enemy (v1-design 2.3 "Attack execution", 3.3 attack sets). The montage carries the C1 notifies
// (MD Weapon Trace window, Motion Warping "AttackTarget"); the hit itself goes through UMDCombatStatics::ApplyMeleeHit with
// Step as the attack step (damage multiplier, poise damage, reaction, knockback, warp range).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDEnemyAttack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	FName Name = TEXT("Swipe");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// The attack may start when the target is within this distance (cm, centre to centre).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0"))
	float Range = 220.0f;

	// Readable wind-up (1.3): seconds from the montage start to the first trace window at T0 (tiers scale it, floor 0.35 s).
	// The attack component slows the montage during the wind-up to reach it; 0 = play as authored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0"))
	float TellTime = 0.8f;

	// Seconds after this attack ends before the enemy may attack again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0"))
	float Cooldown = 0.8f;

	// Damage / poise / reaction / knockback / warp numbers of the hit (the montage reference here is unused: Montage above plays).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	FMDAttackStep Step;

	// Weaponless attackers: sockets (or bones) swept as claws, e.g. hand_r / hand_l. Empty = the enemy swings attached weapon actors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Trace")
	TArray<FName> TraceSockets = { TEXT("hand_r") };

	// Claw box from ClawBack behind the socket to ClawLength in front of it, along the socket's X axis (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Trace", meta = (ClampMin = "1"))
	float ClawLength = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Trace", meta = (ClampMin = "0"))
	float ClawBack = 10.0f;
};

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

	// The attack set (3.3). The brain picks the tightest attack whose Range covers the target; empty = the enemy never attacks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<FMDEnemyAttack> Attacks;

	// ---- Tells and off-screen threat (V1 WP4, v1-design 1.3) ----

	// Short growl played on every machine at the start of each attack wind-up (the MDAN_Tell notify). Null = silent tell.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tells")
	TSoftObjectPtr<USoundBase> TellSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tells", meta = (ClampMin = "0", ClampMax = "2"))
	float TellVolume = 0.35f;

	// Sting played at the enemy's position when it starts its swing at the local player while off-screen for them.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tells")
	TSoftObjectPtr<USoundBase> ThreatSting;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tells", meta = (ClampMin = "0", ClampMax = "2"))
	float ThreatStingVolume = 0.8f;

	// R1 hook: the ranged weapon this enemy fires (Bone Archer bow, Cultist bolt). The same asset type and the same AMDProjectile as the
	// player's guns; UMDRangedStatics::FireFromData spawns the shot. V1 WP5 only wires the brain. Null = melee only.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<class UMDRangedWeaponData> RangedData;

	// ---- Rules ----

	// Longest Range in Attacks (0 with none): the distance an Attack intent may start from.
	float GetAttackRange() const;

	// Index of the attack to use at Distance: the one with the smallest Range that still covers it (a swipe up close, the
	// lunge from further out). INDEX_NONE when nothing reaches.
	int32 PickAttack(float Distance) const;

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
