#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDInventoryTypes.h"
#include "MDStatCalculator.generated.h"

// World-free rules for durability, upgrades, repair cost and kill gold. Every function is pure so the automation tests
// need no actors and no assets; FMDEconomyTuning defaults equal the BALANCE.md values.
UCLASS()
class MAZEDEMONS_API UMDStatCalculator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Effectiveness of a worn item in [ConditionFloor, 1]. MaxDurability <= 0 (no durability) is always 1. At or above
	// FullStrengthAbove of max durability the item is at full strength, below it the multiplier lerps down to ConditionFloor.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float ConditionMultiplier(int32 Durability, int32 MaxDurability, const FMDEconomyTuning& Tuning);

	// Damage reduction percent of one armor slot: (row DR + ArmorDRPerLevel * level) * condition.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float EffectivePieceDR(float RowDR, int32 UpgradeLevel, float CondMult, const FMDEconomyTuning& Tuning);

	// 1 - product(1 - r) over BaseDefense and the pieces (all percents), clamped to [0, Cap].
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float CombineDR(float BaseDefense, const TArray<float>& PiecePercents, float Cap);

	// UMDAbilitySystemComponent::ComputeAttackPower(Base, WeaponBonuses) * (1 + WeaponAPPctPerLevel * level) * WeaponCondMult.
	static float EffectiveAttackPower(float Base, const TMap<FName, float>& WeaponBonuses, int32 WeaponUpgradeLevel,
		float WeaponCondMult, const FMDEconomyTuning& Tuning);

	// Blueprint-friendly form of EffectiveAttackPower for a single weapon: (Base + WeaponBonus) * (1 + WeaponAPPctPerLevel * level)
	// * WeaponCondMult. (The TMap overload above stays C++ only; UHT does not allow overloaded UFUNCTION names.)
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float EffectiveWeaponAttackPower(float Base, float WeaponBonus, int32 WeaponUpgradeLevel, float WeaponCondMult,
		const FMDEconomyTuning& Tuning);

	// ceil(Missing * ItemValue * RepairRate * (1 + RepairUpgradeScale * level)); 0 when nothing is missing.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static int32 RepairCost(int32 Missing, int32 ItemValue, int32 UpgradeLevel, const FMDEconomyTuning& Tuning);

	// Cost to go from CurrentLevel to CurrentLevel + 1. False (outputs 0) at or above MaxUpgradeLevel.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static bool GetUpgradeCost(int32 CurrentLevel, const FMDEconomyTuning& Tuning, int32& OutGold, int32& OutShards);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static int32 DurabilityAfterHit(int32 Durability);

	// Durability after dying: loses ceil(Max * DeathDurabilityLossPct), never below 0. Items without durability are unchanged.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static int32 DurabilityAfterDeath(int32 Durability, int32 MaxDurability, const FMDEconomyTuning& Tuning);

	// Even split of Gold among NumRecipients; the remainder goes to KillerIndex (index 0 when INDEX_NONE or out of range).
	// Returns NumRecipients shares that sum to Gold (all zero when Gold <= 0 or there are no recipients).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static TArray<int32> SplitKillGold(int32 Gold, int32 NumRecipients, int32 KillerIndex);

	// Kill gold for a list of players, given each player's distance to the victim (negative = no pawn, never a recipient).
	// Players with 0 <= distance <= Radius share the gold; the remainder goes to KillerIndex (an index into Distances) if it is
	// a recipient, otherwise to the nearest recipient. OutShares has one entry per player (0 for non-recipients).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static void DistributeKillGold(int32 Gold, const TArray<float>& Distances, float Radius, int32 KillerIndex, TArray<int32>& OutShares);
};
