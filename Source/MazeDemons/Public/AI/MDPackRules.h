#pragma once

#include "CoreMinimal.h"
#include "AI/MDAITypes.h"

// V1 WP2 pack rules (v1-design 2.4 / 3.2 / 1.3). Plain structs and functions: no world, no actors, so every rule runs in
// MazeDemons.AI.Pack.* without a level. UMDPackDirectorSubsystem is the thin wrapper that feeds these from the world.
// Distances in cm, angles in degrees, times in seconds.

namespace MDPack
{
	// Ring slots around the focus target (2.4): N = 6, melee distance 1.8 - 2.6 m.
	constexpr int32 NumRingSlots = 6;
	constexpr float SlotRadiusMin = 180.0f;
	constexpr float SlotRadiusMax = 260.0f;

	// Threat ring for enemies without a token: 4 - 6 m.
	constexpr float ThreatRadiusMin = 400.0f;
	constexpr float ThreatRadiusMax = 600.0f;

	// Flankers prefer ring slots at least this far from the player's facing.
	constexpr float FlankMinAngle = 120.0f;

	// Within this distance of the rally point a retreating enemy has arrived.
	constexpr float RallyAcceptance = 350.0f;

	// Packs merge when two of their members are this close and both have seen the same target (T2+).
	constexpr float MergeDistance = 1000.0f;

	// How long a pack stays at the rally point before it comes back.
	constexpr double RegroupTime = 8.0;

	// Focus target scoring weights (2.4).
	constexpr float WeightIsolation = 0.35f;
	constexpr float WeightHealth = 0.25f;
	constexpr float WeightStaggered = 0.15f;
	constexpr float WeightThreat = 0.15f;
	constexpr float WeightCrowding = -0.10f;

	// A new focus target must beat the current one by this much (anti flip-flop).
	constexpr float RetargetMargin = 0.05f;

	// Damage the pack took from a player counts toward its threat score for this long.
	constexpr double ThreatWindow = 10.0;

	// A member that took this fraction of its max health from one player within DamageOverrideWindow targets that player
	// whatever the pack's focus is (2.4 "damage-taken override").
	constexpr float DamageOverrideFraction = 0.30f;
	constexpr double DamageOverrideWindow = 3.0;
	constexpr double DamageOverrideHold = 5.0;

	// Token priority: role rank dominates, closeness breaks ties, a seeded per-enemy noise breaks exact ties. A requester
	// evicts an uncommitted holder only when it beats it by this much (so the token does not flap between two ghouls).
	constexpr float TokenEvictMargin = 50.0f;

	constexpr uint32 RoleBit(EMDRole Role) { return 1u << static_cast<uint32>(Role); }
}

// One pack member as the role assignment sees it.
struct FMDRoleCandidate
{
	int32 Id = 0;
	// Bit mask of EMDRole (MDPack::RoleBit). 0 = the archetype lists no roles: treated as Striker only.
	uint32 AllowedMask = 0;
};

// Candidates for the focus target, already reduced to the five inputs of the score.
struct FMDFocusInputs
{
	// 0..1: 1 = no ally within 15 m (see NormalizeIsolation).
	float Isolation = 0.0f;
	float HealthFraction = 1.0f;
	// Staggered or in attack recovery.
	bool bStaggered = false;
	// 0..1: damage this player dealt to the pack in the last ThreatWindow seconds.
	float Threat = 0.0f;
	// 0..1: share of the pack already targeting this player.
	float Crowding = 0.0f;
};

// One enemy's request for a ring slot.
struct FMDSlotMember
{
	int32 Id = 0;
	EMDRole Role = EMDRole::None;
	// The enemy's current angle around the target (degrees), so it takes the nearest free slot instead of crossing the ring.
	float AngleDeg = 0.0f;
	int32 CurrentSlot = -1;
};

namespace MDPackRules
{
	// ---- Tier table (3.2) ----
	// Flankers per player: T0/T1 0, T2/T3 1, T4 2.
	MAZEDEMONS_API int32 FlankersFor(EMDIntelligenceTier Tier);

	// Minimum seconds between focus re-targets; < 0 = the pack never re-scores once it has a valid focus (T0).
	MAZEDEMONS_API float RetargetInterval(EMDIntelligenceTier Tier);

	// A focus target may be (re)chosen when none is valid, or when the tier's interval has elapsed.
	MAZEDEMONS_API bool CanRetarget(EMDIntelligenceTier Tier, double SecondsSinceLastRetarget, bool bFocusValid);

	// Pack alive fraction at or under which the pack retreats; < 0 = never (T0, T1). T2 0.4, T3/T4 0.5.
	MAZEDEMONS_API float RetreatFraction(EMDIntelligenceTier Tier);

	// Retreat when the tier allows it, a rally point exists and alive <= fraction x spawn count (3 of 5 dead at T2 = 2 alive).
	MAZEDEMONS_API bool ShouldRetreat(EMDIntelligenceTier Tier, int32 Alive, int32 SpawnCount, bool bHasRally);

	// Merge two packs (T2+) when their closest members are within MergeDistance and both have seen the same target.
	MAZEDEMONS_API bool ShouldMerge(EMDIntelligenceTier Tier, float MinMemberDistance, bool bSharedTarget);

	// The next pack state (Engage / Pressing / Regroup / Routed). RetreatDone: this pack already retreated once and has not
	// recovered above the threshold since.
	MAZEDEMONS_API EMDPackState ResolvePackState(EMDPackState Current, EMDIntelligenceTier Tier, int32 Alive, int32 SpawnCount, bool bHasRally,
		bool bRetreatDone, bool bRegroupElapsed, int32 TokensHeldOnFocus, int32 MaxTokens);

	// ---- Roles ----
	// Roles for Candidates (Out has the same order). Deterministic: members are ranked by Id.
	//   - every member starts as Striker (when allowed, else its first allowed role);
	//   - T2+: one Leader (first member that allows it), one Bait only when the pack has 3 or more alive,
	//     FlankersFor(Tier) Flankers among the members that allow it;
	//   - with 2 or fewer alive, everyone who can becomes a Striker.
	MAZEDEMONS_API void AssignRoles(const TArray<FMDRoleCandidate>& Candidates, EMDIntelligenceTier Tier, TArray<EMDRole>& OutRoles);

	// 3 Striker, 2 Flanker, 1 everything else.
	MAZEDEMONS_API int32 RoleRank(EMDRole Role);

	// Token priority (higher wins): RoleRank x 1000 + closeness x 100 + seeded per-enemy noise in [0, 10).
	MAZEDEMONS_API float TokenPriority(EMDRole Role, float DistanceToTarget, int32 RunSeed, int32 EnemyId);

	// ---- Focus target ----
	// 0..1 from the distance (cm) to the nearest other player: 0 at 0, 1 at 1500 and beyond or with no other player (< 0).
	MAZEDEMONS_API float NormalizeIsolation(float DistanceToNearestAlly);

	MAZEDEMONS_API float FocusScore(const FMDFocusInputs& Inputs);

	// Index of the best candidate; Current (if valid) is kept unless the best beats it by Margin. INDEX_NONE for no candidates.
	MAZEDEMONS_API int32 PickFocus(const TArray<float>& Scores, int32 CurrentIndex, float Margin = MDPack::RetargetMargin);

	// ---- Ring slots ----
	// World angle of a slot: BaseYaw + Slot x 360 / NumRingSlots, in [0, 360).
	MAZEDEMONS_API float SlotAngle(int32 Slot, float BaseYawDeg);

	// The point on the ring: Target + (cos, sin) x Radius at the slot's angle (Z of Target).
	MAZEDEMONS_API FVector SlotLocation(const FVector& TargetLocation, int32 Slot, float BaseYawDeg, float Radius);

	// Melee slot radius: the archetype's preferred distance clamped to 1.8-2.6 m and kept inside the attack reach (x0.9) so an
	// enemy standing in its slot can swing without another step.
	MAZEDEMONS_API float SlotRadiusFor(float PreferredRange, float AttackRange);

	// Threat ring radius: the archetype's ThreatRange clamped to 4-6 m.
	MAZEDEMONS_API float ThreatRadiusFor(float ThreatRange);

	// Smallest absolute difference of two angles, 0..180.
	MAZEDEMONS_API float AngleDiff(float A, float B);

	// Angle between the player's facing and the direction to the slot, 0..180 (>= FlankMinAngle = behind the player).
	MAZEDEMONS_API float SlotAngleFromFacing(int32 Slot, float BaseYawDeg, float FacingYawDeg);

	// Gives each member one of the NumRingSlots slots (OutSlots parallel to Members, -1 for the overflow). Flankers pick first
	// and prefer slots >= FlankMinAngle from FacingYaw, then Strikers, then the rest; each takes the free slot nearest to its
	// current angle (its current slot keeps a small bonus so the ring does not shuffle).
	MAZEDEMONS_API void AssignSlots(const TArray<FMDSlotMember>& Members, float BaseYawDeg, float FacingYawDeg, TArray<int32>& OutSlots);
}

// Attack tokens (1.3 / 2.4), per player: at most MaxPerTarget valid tokens per target. A token is void at its expiry (grant +
// 4 s timeout), is released at once on stagger / death, and expires 0.5 s after the attack ended. Priority: a requester that
// outranks an uncommitted holder (an enemy that has not started its attack) takes its token; a committed (attacking) token
// is never taken. Keyed on plain ids so the rule is testable.
struct MAZEDEMONS_API FMDPackTokenPool
{
	struct FEntry
	{
		int32 Holder = 0;
		int32 Target = 0;
		double ExpireTime = 0.0;
		float Priority = 0.0f;
		bool bCommitted = false;
	};

	struct FGrantResult
	{
		bool bGranted = false;
		// The holder whose token was taken (0 = none).
		int32 EvictedHolder = 0;
	};

	FGrantResult TryGrant(int32 Holder, int32 Target, double Now, int32 MaxPerTarget, float Priority, double Timeout = 4.0);

	bool Holds(int32 Holder, double Now) const;
	int32 GetTarget(int32 Holder, double Now) const;
	int32 CountForTarget(int32 Target, double Now) const;

	// The holder started its attack: the token can no longer be taken by a higher priority requester.
	void Commit(int32 Holder);

	void Release(int32 Holder);

	// The attack ended: the token expires in Delay seconds (or earlier if its timeout is sooner).
	void ReleaseAfter(int32 Holder, double Now, double Delay = 0.5);

	void Purge(double Now);

	int32 Num() const { return Entries.Num(); }

private:
	TArray<FEntry> Entries;
};
