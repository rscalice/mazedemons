#pragma once

#include "CoreMinimal.h"
#include "AI/MDAIQueries.h"
#include "AI/MDAITypes.h"

// V1 WP5 advanced-behaviour rules (v1-design 1.2 / 1.3 / 3.2). Plain structs and functions, no world and no actors, so every
// rule runs in MazeDemons.AI.Behaviour.* with no level. The scorer gates (MDAIRules.cpp), the pack director and the thin
// StateTree tasks (MDSTEnemyTasks) call these; UMDEnemyBehaviourComponent holds the per-enemy timers.
// Distances in cm, times in seconds.

namespace MDBehaviour
{
	// ---- Swarm (T1+) ----
	constexpr int32 SwarmMinPack = 3;
	constexpr double BarkInterval = 15.0; // one pack bark (swarm howl / fall back) per this many seconds per pack

	// ---- Bait (T2+) ----
	constexpr float BaitLeash = 800.0f;          // 8 m from the pack
	constexpr float BaitHalfStep = 90.0f;        // a half-step back
	constexpr float BaitApproachDistance = 550.0f; // the player closer than this makes the bait step back
	constexpr double BaitTauntInterval = 6.0;
	constexpr double FakeFlinchInterval = 8.0;   // T3+

	// ---- Feint (T3+) ----
	constexpr float FeintMinDelay = 0.3f;        // real attack 0.3 - 0.6 s after the visible jerk
	constexpr float FeintMaxDelay = 0.6f;
	constexpr float FeintCancelAfter = 0.35f;    // the wind-up is cancelled this long after it started (before the trace window)

	// ---- Blink (T3+) ----
	constexpr double BlinkPreDelay = 0.4;
	constexpr double BlinkCooldown = 6.0;
	constexpr double BlinkArrivalGrace = 0.3;
	constexpr float BlinkMinTargetDistance = 200.0f; // arrival >= 2 m from the target
	constexpr float BlinkMinRange = 600.0f;          // only blinks to close a gap
	constexpr float BlinkMaxRange = 2000.0f;
	constexpr float BlinkArrivalRadius = 350.0f;

	// ---- Ambush (T2+) ----
	constexpr double AmbushCueLead = 0.75;
	constexpr double AmbushArrivalGrace = 0.5;
	constexpr float AmbushSafeClearance = 1500.0f;  // never ambush within 15 m of a checkpoint or a station
	constexpr float AmbushMinPlayerDistance = 150.0f; // never appear inside a player's capsule

	// ---- Kite (T1+) ----
	constexpr double KiteRepositionCooldown = 1.0;
	constexpr float KiteMinRangeFraction = 0.55f;   // closer than this share of the preferred range = too close
	constexpr float KiteMaxRangeSlack = 300.0f;     // further than preferred + this = too far
	constexpr float KiteDrawTime = 1.2f;            // arrow draw at T0 (tier scaled, floor 0.35 s)

	// ---- Charge (T0+, Brute) ----
	constexpr float ChargeLineUpDistance = 600.0f;  // backs up to this distance from the target
	constexpr float ChargeMinDistance = 350.0f;
	constexpr float ChargeMaxDistance = 1400.0f;
	constexpr float ChargeSidestepLateral = 140.0f; // the target slid this far off the charge line = cancelled
	constexpr float ChargeSpeedMultiplier = 2.4f;
	constexpr double ChargeCooldown = 5.0;
	constexpr double ChargeRecovery = 0.8;
	constexpr double ChargeCancelRecovery = 1.6;    // a dodged charge leaves the brute open longer

	// ---- Punish greed (T3+) ----
	constexpr double GreedWindow = 3.0;             // absorbed hits further apart than this do not chain
	constexpr double BraceDuration = 2.5;
	constexpr double CounterWindow = 4.0;           // the counter must come within this after bracing

	// ---- Guard / protect the caster (T2+) ----
	constexpr float GuardMinDistance = 200.0f;      // the line stands this far (at least) from the caster
	constexpr float GuardMaxDistance = 450.0f;
	constexpr float GuardSpacing = 150.0f;
	constexpr int32 MaxGuards = 2;                  // defence tokens per caster

	// ---- Roles and tokens ----
	// Bait and Guard never hold an attack token (Guards hold defence tokens: a slot on the guard line).
	MAZEDEMONS_API bool CanHoldAttackToken(EMDRole Role);

	// ---- Barks ----
	// A pack bark is allowed when Interval has passed since the last one (LastBark < 0 = never barked).
	MAZEDEMONS_API bool BarkAllowed(double Now, double LastBark, double Interval = BarkInterval);

	// ---- Swarm ----
	MAZEDEMONS_API bool ShouldSwarm(EMDIntelligenceTier Tier, int32 PackAlive, bool bHasSlot, float DistanceToTarget, float ThreatRange);

	// The ring slots N attackers take so they are evenly spaced around the 6-slot ring (N=2 -> 0,3; N=3 -> 0,2,4), rotated by
	// Rotation slots. N >= 6 uses every slot.
	MAZEDEMONS_API void EvenSlotSet(int32 NumAttackers, int32 Rotation, TArray<int32>& OutSlots);

	// ---- Bait ----
	MAZEDEMONS_API bool IsWithinBaitLeash(const FVector& BaitLocation, const FVector& PackCentre);

	// The point the bait steps to when the player at PlayerLocation closes in: a half-step straight away from the player, pulled
	// back inside the 8 m leash around PackCentre. False when the bait should stay (player not close, or no legal step).
	MAZEDEMONS_API bool ComputeBaitStep(const FVector& BaitLocation, const FVector& PlayerLocation, const FVector& PackCentre, FVector& OutPoint);

	// The bait may act when its own area and the player's area are revealed (never across a door the player has not seen).
	MAZEDEMONS_API bool CanBaitAcross(const FMDQueryEnvironment& Env, const FVector& BaitLocation, const FVector& PlayerLocation);

	MAZEDEMONS_API bool FakeFlinchUnlocked(EMDIntelligenceTier Tier);

	// ---- Feint ----
	MAZEDEMONS_API float FeintChance(EMDIntelligenceTier Tier);
	MAZEDEMONS_API float FeintDelay(float Roll01);

	// One attack opportunity of a token holder. The feint roll is made once; a feint happens at most once per opportunity.
	struct MAZEDEMONS_API FMDFeintOpportunity
	{
		bool bEvaluated = false;
		bool bWillFeint = false;
		bool bUsed = false;

		// True when the holder should feint now: T3+, holds a token, rolled "feint", has not feinted this opportunity.
		bool ShouldFeintNow(EMDIntelligenceTier Tier, bool bHasToken, float Roll01);
		void MarkFeinted() { bUsed = true; }
		void Reset() { *this = FMDFeintOpportunity(); }
	};

	// ---- Blink ----
	MAZEDEMONS_API bool BlinkOffCooldown(double Now, double LastBlinkTime);

	struct FMDBlinkParams
	{
		FVector TargetLocation = FVector::ZeroVector;
		FVector TargetFacing = FVector::ForwardVector;
		FVector FromLocation = FVector::ZeroVector;
		float Radius = BlinkArrivalRadius;
		float MinTargetDistance = BlinkMinTargetDistance;
	};

	// Arrival point checks: navmesh-valid (projected), >= MinTargetDistance from the target, in a revealed area.
	MAZEDEMONS_API bool IsBlinkArrivalValid(const FMDBlinkParams& Params, const FMDQueryEnvironment& Env, const FVector& Candidate, FVector& OutProjected);

	// Best arrival: behind the target when possible, every candidate passing IsBlinkArrivalValid. False when none.
	MAZEDEMONS_API bool FindBlinkArrival(const FMDBlinkParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint);

	// ---- Ambush ----
	struct FMDAmbushTimeline
	{
		double TriggerTime = 0.0;
		double CueTime = 0.0;    // the audio cue plays
		double ArriveTime = 0.0; // the ambushers appear (cue + 0.75 s)
		double GraceEnd = 0.0;   // no attacks until here (arrival + 0.5 s)
	};
	MAZEDEMONS_API FMDAmbushTimeline MakeAmbushTimeline(double TriggerTime);
	MAZEDEMONS_API bool IsInArrivalGrace(double Now, double GraceEnd);

	// True when Location is within Clearance of any safe spot (checkpoint / station).
	MAZEDEMONS_API bool IsNearSafeSpot(const FVector& Location, const TArray<FVector>& SafeSpots, float Clearance = AmbushSafeClearance);

	// An ambush may fire at the tier (T2+ unless the marker ignores tiers) and never at a checkpoint / station.
	MAZEDEMONS_API bool CanAmbush(EMDIntelligenceTier Tier, bool bIgnoreTier, bool bNearSafeSpot);

	// ---- Kite ----
	enum class EKiteAction : uint8 { Hold, Reposition, Fire };
	MAZEDEMONS_API bool KiteRepositionReady(double Now, double LastReposition);
	MAZEDEMONS_API EKiteAction DecideKite(float Distance, float PreferredRange, bool bLineOfSight, bool bRepositionReady, bool bCanFire);
	MAZEDEMONS_API float KiteDrawTimeFor(const FMDTierParams& Tier);

	// ---- Charge ----
	// Where the brute backs up to: ChargeLineUpDistance from the target on the far side from the target, from its own spot.
	MAZEDEMONS_API FVector ChargeLineUpPoint(const FVector& TargetLocation, const FVector& FromLocation);
	// Sideways distance (XY) of Point from the charge line that starts at Origin along Dir.
	MAZEDEMONS_API float ChargeLateralOffset(const FVector& Origin, const FVector& Dir, const FVector& Point);
	// The charge is cancelled when the target slid sideways off the line (a dodge-slide).
	MAZEDEMONS_API bool IsChargeCancelledBySidestep(const FVector& Origin, const FVector& Dir, const FVector& TargetNow);
	MAZEDEMONS_API bool IsChargeDistanceOk(float Distance);

	// ---- Punish greed ----
	struct MAZEDEMONS_API FMDGreedTracker
	{
		int32 Hits = 0;
		double LastHitTime = -1000.0;
		bool bBraced = false;
		double BraceStart = 0.0;

		// An absorbed hit (damage with no stagger). Hits further apart than GreedWindow start a new chain.
		void RegisterAbsorbedHit(double Now);
		// Braced counter due: T3+, Threshold > 0 and Hits >= Threshold.
		bool ShouldBrace(EMDIntelligenceTier Tier, int32 Threshold) const;
		void StartBrace(double Now) { bBraced = true; BraceStart = Now; }
		// The counter expired unused (or was thrown): back to a clean slate.
		bool BraceExpired(double Now) const { return bBraced && Now - BraceStart > CounterWindow; }
		void Reset() { *this = FMDGreedTracker(); }
	};

	// ---- Guard line ----
	// Points for NumGuards guards on the segment between the caster and the player: the line stands in front of the caster
	// (clamped GuardMinDistance .. GuardMaxDistance, never further than 60% of the way), guards spaced across it.
	MAZEDEMONS_API void ComputeGuardLine(const FVector& CasterLocation, const FVector& PlayerLocation, int32 NumGuards, TArray<FVector>& OutPoints);

	// ---- Console ----
	// md.AI.ForceIntent value -> intent. False for "", "none", "off" and unknown names.
	MAZEDEMONS_API bool ParseForcedIntent(const FString& Text, EMDIntent& OutIntent);
}
