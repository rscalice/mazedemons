#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MDPerkTypes.generated.h"

// Perk system data (U3). Node values are BALANCE.md section 5 (all PER RANK); caps are section 7.

constexpr int32 MD_NumPerkNodes = 16;
constexpr int32 MD_NodesPerPerkPath = 4;

UENUM(BlueprintType)
enum class EMDPerkPath : uint8
{
	None = 0,
	Defense,
	Offense,
	Vitality,
	Movement
};

UENUM(BlueprintType)
enum class EMDPerkTier : uint8
{
	None = 0,
	Tier1a,
	Tier1b,
	Tier2,
	Capstone
};

// What a node does, per rank.
UENUM(BlueprintType)
enum class EMDPerkEffect : uint8
{
	None = 0,
	DamageReductionPP,
	FlatDamageReduction,
	StalwartDRPP,
	BulwarkHitCap,
	AttackPowerPct,
	CritChance,
	CritDamage,
	ExecutionerPct,
	MaxHealth,
	HealthRegen,
	PotionHealPct,
	SecondWind,
	MaxStamina,
	StaminaRegenPct,
	StaminaCostPct,
	FleetSprint
};

// Why a perk commit / respec was refused. Server decides; the UI greys Confirm with the same pure rule.
UENUM(BlueprintType)
enum class EMDPerkReject : uint8
{
	None = 0,
	InvalidRanks,
	Dead,
	InCombat,
	NoPoints,
	WouldReduceRank,
	PathLocked,
	PathCap,
	NotAtStation
};

USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPerkNode
{
	GENERATED_BODY()

	// Stable id ("Hardened", "ThickHide", ...); tests and UI look nodes up by it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	EMDPerkPath Path = EMDPerkPath::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	EMDPerkTier Tier = EMDPerkTier::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	EMDPerkEffect Effect = EMDPerkEffect::None;

	// 2 for tiers 1a / 1b / 2, 1 for capstones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk", meta = (ClampMin = "1"))
	int32 MaxRank = 1;

	// Points already spent in THIS path (excluding this node's own ranks) before rank 1: 0 / 0 / 2 / 4.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk", meta = (ClampMin = "0"))
	int32 RequiresPathPoints = 0;

	// BALANCE section 5 "per rank" value (pp, fraction, HP, ... depending on Effect).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	float ValuePerRank = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	FText Name;

	// Uses {Value} (this rank) and {Total}.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perk")
	FText Description;
};

// BALANCE section 7 hard caps (PlayerDRCap lives in FMDEconomyTuning).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPerkCaps
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float MaxHealthFromPerks = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float HealthRegenCap = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float CritChanceCap = 0.25f;

	// Total crit damage multiplier.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float CritDamageCap = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float StaminaCostScaleMin = 0.7f;

	// Bonus fraction on stamina regen (0.45 = +45%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float StaminaRegenBonusCap = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float MaxStaminaCap = 150.0f;

	// Bonus fraction on sprint speed (0.05 = +5%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	float SprintSpeedBonusCap = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caps")
	int32 PathPointCap = 5;
};

// Other perk tuning (lives in UMDBalanceData::Perks). Defaults are the BALANCE.md values so tests and a missing asset need nothing.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPerkTuning
{
	GENERATED_BODY()

	// Seconds without damage taken before perks can be committed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points")
	float SpendDelay = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points")
	int32 StartingPoints = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points")
	int32 PointsPerZone = 1;

	// Zones that grant a point (zones 1-4).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points")
	int32 MaxZones = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points")
	TArray<int32> SoulsSavedMilestones = { 5, 10, 15 };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float BaseCritDamage = 1.5f;

	// Bulwark: no single hit above this fraction of max health.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float BulwarkMaxHitFraction = 0.30f;

	// Executioner applies to targets below this health fraction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float ExecutionerThreshold = 0.30f;

	// Stalwart applies below this health fraction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float StalwartHealthThreshold = 0.50f;

	// Thick Hide never reduces a hit below this (or below the hit itself when it is already smaller).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float ThickHideMinDamage = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float SecondWindHealFraction = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float SecondWindHealSeconds = 3.0f;

	// Seconds without damage before Recovery heals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float RegenOutOfCombat = 5.0f;

	// Fleet: seconds removed from the stamina regen delay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float FleetRegenDelayReduction = 0.5f;
};

// Reference numbers for the pure balance model (UMDBalanceModel). BALANCE.md values; lives in UMDBalanceData::Reference.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDBalanceReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float WerewolfHealth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float WerewolfHit = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float WerewolfAttackInterval = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float BossHealth = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float BossHit = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float BossAttackInterval = 2.0f;

	// Seconds between the player's attacks (model input for DPS).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float PlayerAttackInterval = 1.0f;

	// Fraction of a fight spent with the target below the Executioner threshold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	float ExecutionerShare = 0.2f;

	// Perk points available in a full run (1 start + 4 zones + 3 milestones).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	int32 FullRunPoints = 8;
};

// The 16 perk nodes plus caps, in one asset (DA_PerkTree). The constructor fills the BALANCE defaults, so a new asset, the
// CDO and a missing asset all behave the same without any editor work.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDPerkTree : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMDPerkTree();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perks")
	TArray<FMDPerkNode> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perks")
	FMDPerkCaps Caps;

	// BALANCE section 5 numbers, index = (Path - 1) * 4 + (Tier - 1).
	static TArray<FMDPerkNode> MakeDefaultNodes();

	// Node index for a path / tier, or INDEX_NONE for None.
	static int32 GetNodeIndex(EMDPerkPath Path, EMDPerkTier Tier);

	// Index of the node with this id, or INDEX_NONE.
	int32 FindNodeIndex(FName Id) const;
};

// Everything the perk ranks add up to, after the BALANCE caps. Totals, not deltas: the empty build equals the defaults.
struct FMDPerkModifiers
{
	float DamageReductionPP = 0.0f;     // percentage points, Hardened
	float StalwartPP = 0.0f;            // percentage points, only while below the Stalwart health threshold
	float FlatDamageReduction = 0.0f;   // Thick Hide
	bool bBulwark = false;
	float AttackPowerPct = 0.0f;        // 0.12 = +12%
	float CritChance = 0.0f;            // 0..1
	float CritDamage = 1.5f;            // total multiplier
	float ExecutionerPct = 0.0f;        // 0.2 = +20% damage below the threshold
	float MaxHealthBonus = 0.0f;
	float HealthRegen = 0.0f;           // HP/s, out of combat
	float PotionHealPct = 0.0f;         // 0.2 = +20%
	bool bSecondWind = false;
	float MaxStaminaBonus = 0.0f;
	float StaminaRegenPct = 0.0f;       // 0.15 = +15%
	float StaminaCostScale = 1.0f;      // multiplier, 1 = no change
	float SprintSpeedScale = 1.0f;      // multiplier
	float RegenDelayReduction = 0.0f;   // seconds (Fleet)

	bool operator==(const FMDPerkModifiers& O) const
	{
		return DamageReductionPP == O.DamageReductionPP && StalwartPP == O.StalwartPP && FlatDamageReduction == O.FlatDamageReduction
			&& bBulwark == O.bBulwark && AttackPowerPct == O.AttackPowerPct && CritChance == O.CritChance && CritDamage == O.CritDamage
			&& ExecutionerPct == O.ExecutionerPct && MaxHealthBonus == O.MaxHealthBonus && HealthRegen == O.HealthRegen
			&& PotionHealPct == O.PotionHealPct && bSecondWind == O.bSecondWind && MaxStaminaBonus == O.MaxStaminaBonus
			&& StaminaRegenPct == O.StaminaRegenPct && StaminaCostScale == O.StaminaCostScale && SprintSpeedScale == O.SprintSpeedScale
			&& RegenDelayReduction == O.RegenDelayReduction;
	}
	bool operator!=(const FMDPerkModifiers& O) const { return !(*this == O); }
};

// Server-only victim / attacker flags pushed into the ASC by the perk component. All zero / false on non-player ASCs.
struct FMDPerkCombatState
{
	float FlatDamageReduction = 0.0f;
	bool bBulwark = false;
	bool bSecondWindAvailable = false; // owned and not yet used this life
	float ExecutionerPct = 0.0f;
};

// Result of the victim-side damage rules for one hit.
struct FMDIncomingDamageResult
{
	float DamageTaken = 0.0f;
	float NewHealth = 0.0f;
	bool bSecondWindTriggered = false;
};
