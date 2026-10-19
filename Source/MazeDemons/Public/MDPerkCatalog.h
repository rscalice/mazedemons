#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDPerkTypes.h"
#include "MDPerkCatalog.generated.h"

// World-free perk rules (U3, rule D7): commit validation, modifier maths, points, crit, executioner, incoming-damage rules.
// Every function is pure so the automation tests need no actors and no assets. Ranks are always a TArray<uint8> of length
// MD_NumPerkNodes indexed by (Path - 1) * 4 + (Tier - 1).
UCLASS()
class MAZEDEMONS_API UMDPerkCatalog : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Tree / points ----
	static int32 PointsInPath(const TArray<uint8>& Ranks, EMDPerkPath Path);
	static int32 TotalRanks(const TArray<uint8>& Ranks);

	// StartingPoints + zones (clamped to 0..MaxZones) * PointsPerZone + milestones reached + DebugBonus (never negative).
	static int32 ComputePointsEarned(int32 ZonesCompleted, int32 SoulsSaved, int32 DebugBonusPoints, const FMDPerkTuning& Tuning = FMDPerkTuning());

	// ---- Commit rules ----
	// The whole Desired array is validated as one transaction. EMDPerkReject::None = OK. Check order: InvalidRanks, Dead, InCombat,
	// WouldReduceRank, NoPoints, PathCap, PathLocked.
	static EMDPerkReject ValidateCommit(const TArray<uint8>& Current, const TArray<uint8>& Desired, int32 PointsEarned,
		double TimeSinceDamage, bool bDead, const UMDPerkTree& Tree, const FMDPerkTuning& Tuning = FMDPerkTuning());

	// Same, also reporting what the toast needs: InCombat -> OutArg = whole seconds still to wait (rounded up);
	// PathLocked -> OutArg = points required, OutPath = the path; PathCap -> OutArg = cap, OutPath = the path.
	static EMDPerkReject ValidateCommitEx(const TArray<uint8>& Current, const TArray<uint8>& Desired, int32 PointsEarned,
		double TimeSinceDamage, bool bDead, const UMDPerkTree& Tree, const FMDPerkTuning& Tuning, int32& OutArg, EMDPerkPath& OutPath);

	// ---- Pending (client-side) model for the Perks panel ----
	// Owned + Pending per node, each clamped to the node's MaxRank; a missing / short array counts as zeros. Always MD_NumPerkNodes long.
	static TArray<uint8> MergeRanks(const TArray<uint8>& Owned, const TArray<uint8>& Pending, const UMDPerkTree& Tree);

	// Panel state of Node when Pending ranks sit on top of Owned. Priority: Pending (has uncommitted ranks) > Maxed > Owned >
	// Locked (path prerequisite unmet, counting pending ranks of the other nodes) > BranchCapped (path holds the cap) > Available.
	// An owned node below MaxRank stays Owned even when its path is capped. None for a bad index or arrays of the wrong size.
	static EMDPerkNodeState GetNodeState(int32 Node, const TArray<uint8>& Owned, const TArray<uint8>& Pending, const UMDPerkTree& Tree);

	// Why one more rank in Node would not be a legal commit (None = it would be). Time and death are ignored (those only block
	// Confirm); points, path cap and prerequisites are checked on Owned + Pending + 1. A node already at MaxRank, or a bad
	// index, gives InvalidRanks. OutArg / OutPath as in ValidateCommitEx.
	static EMDPerkReject GetAddRankReject(const TArray<uint8>& Owned, const TArray<uint8>& Pending, int32 Node, int32 PointsEarned,
		const UMDPerkTree& Tree, const FMDPerkTuning& Tuning, int32& OutArg, EMDPerkPath& OutPath);

	// Drops pending ranks until Owned + Pending is a legal commit apart from time / death: ranks above MaxRank first, then
	// (highest node first, so capstones go before the tier below) whatever breaks points, the path cap or a prerequisite.
	static void SanitizePending(const TArray<uint8>& Owned, TArray<uint8>& Pending, int32 PointsEarned, const UMDPerkTree& Tree,
		const FMDPerkTuning& Tuning = FMDPerkTuning());

	// Seconds until committing is allowed: max(0, SpendDelay - TimeSinceDamage).
	static float GetSpendLockSeconds(double TimeSinceDamage, const FMDPerkTuning& Tuning = FMDPerkTuning());

	// ---- Modifiers ----
	// Sums the ranks into totals and applies the BALANCE section 7 caps. Order-independent: depends only on the ranks.
	static void ComputeModifiers(const TArray<uint8>& Ranks, const UMDPerkTree& Tree, FMDPerkModifiers& Out,
		const FMDPerkTuning& Tuning = FMDPerkTuning());

	// Current health after the maximum moved from OldMax to NewMax: a rise adds the difference (never above NewMax), a fall
	// clamps to NewMax (at least 1, so a living character is never killed by this). A dead one (Current <= 0) stays as it is.
	static float CurrentAfterMaxChange(float OldMax, float NewMax, float Current);

	// ---- Stamina ----
	// Cost * clamp(Scale, MinScale, 1).
	static float ScaleStaminaCost(float Cost, float Scale, float MinScale = 0.7f);

	// max(0, Base - Reduction).
	static float EffectiveRegenDelay(float BaseDelay, float Reduction);

	// ---- Damage ----
	// Crit when 0 <= Roll < Chance (Roll in [0, 1)). Chance <= 0 never crits.
	static bool IsCrit(float Chance, float Roll);

	// Damage * CritMult (at least 1) when the roll crits, else Damage.
	static float ApplyCrit(float Damage, float Chance, float CritMult, float Roll);

	// Damage * (1 + Pct) when the target's health fraction BEFORE the hit is strictly below Threshold.
	static float ApplyExecutioner(float Damage, float TargetHealthFraction, float Pct, float Threshold);

	// Victim side, after the Defense % reduction: Thick Hide (flat, never below ThickHideMinDamage nor raising a smaller hit),
	// then Bulwark (never more than BulwarkMaxHitFraction of MaxHealth). All-default arguments return DamageAfterDR unchanged.
	static float ApplyIncomingDamageRules(float DamageAfterDR, float MaxHealth, float FlatReduction, bool bBulwark,
		const FMDPerkTuning& Tuning = FMDPerkTuning());

	// ApplyIncomingDamageRules, then the health result: a hit that would kill while Second Wind is available leaves 1 HP.
	static FMDIncomingDamageResult ResolveIncomingDamage(float DamageAfterDR, float Health, float MaxHealth,
		const FMDPerkCombatState& State, const FMDPerkTuning& Tuning = FMDPerkTuning());

	// True when Max > 0 and Health / Max is strictly below Threshold (Stalwart, Executioner).
	static bool IsBelowHealthFraction(float Health, float MaxHealth, float Threshold);

	// ---- Text ----
	static FText GetPathText(EMDPerkPath Path);

	// Toast text for a reject. Arg / Path as reported by ValidateCommitEx.
	static FText GetPerkRejectText(EMDPerkReject Reject, int32 Arg = 0, EMDPerkPath Path = EMDPerkPath::None);

	// The node's description with {Value} (per rank, scaled for percent effects: 0.06 -> "6") and {Total} (Rank ranks, at least 1) filled in.
	static FText GetNodeDescription(const FMDPerkNode& Node, int32 Rank);

	// "+6 pp damage reduction" for a node at Rank (0 = empty text).
	static FText GetEffectDetailText(const FMDPerkNode& Node, int32 Rank);
};
