#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h" // ERootMotionMode
#include "Engine/EngineTypes.h"      // ENetRole
#include "GameplayTagContainer.h"
#include "AI/MDAITypes.h"
#include "AI/MDPerceptionRules.h"

// V1 WP1 brain rules (v1-design 2.3, 1.3, 2.9). Plain structs and functions: no world, no allocations in the hot path, so
// every rule runs in MazeDemons.AI.Brain.* with no actors. The brain component is a thin wrapper around these.

namespace MDAI
{
	// A new intent must beat the current one by this much unless the current intent finished (2.3 step 4).
	constexpr float SwitchMargin = 0.15f;

	// A token is kept this long after the attack ends (1.3).
	constexpr double TokenHoldAfterAttack = 0.5;

	// A token is void this long after it was granted, whatever happened (2.4).
	constexpr double TokenTimeout = 4.0;

	// Seconds without a stimulus before the brain forgets a target (2.5 lose-sight).
	constexpr double LoseTargetTime = 3.0;

	// Scores are plain struct math: eight slots cover every intent WP1 scores, so no heap allocation per decision.
	using FScores = TArray<FMDScoredIntent, TInlineAllocator<16>>;
}

// Utility scoring (2.3 step 3). Score = base x gates (tier, token, cooldown) x curves (distance) x aggression + seeded noise.
struct MAZEDEMONS_API FMDUtilityScorer
{
	// Scores every intent WP1 knows (Idle, Chase, HoldThreat, Attack); Stagger and Dead are interrupts, never scored.
	// Deterministic: the same context (including RunSeed / EnemyId / DecisionIndex) gives the same scores.
	static MDAI::FScores Score(const FMDBrainContext& Context, const FMDArchetypeAIParams& Archetype, const FMDTierParams& Tier);

	// Noise in [-Amplitude, Amplitude] from an FRandomStream keyed on (run seed, enemy id, decision index); Slot picks the
	// intent's draw so each intent gets its own value from one stream.
	static float Noise(int32 RunSeed, int32 EnemyId, int32 DecisionIndex, int32 Slot, float Amplitude);

	// Hysteresis (2.3 step 4): the top score wins unless Current is still running and the winner does not beat it by
	// SwitchMargin. Current = Idle with bCurrentFinished = true means "no current intent".
	static EMDIntent Select(const MDAI::FScores& Scores, EMDIntent Current, bool bCurrentFinished, float SwitchMargin = MDAI::SwitchMargin);

	static const FMDScoredIntent* Find(const MDAI::FScores& Scores, EMDIntent Intent);

	// Highest score first (debug overlay, tests).
	static void SortDescending(MDAI::FScores& Scores);
};

// One perception event on its way to the brain.
struct FMDStimulus
{
	TWeakObjectPtr<AActor> Source;
	FVector Location = FVector::ZeroVector;
	bool bSensed = true; // false = lost
	double Time = 0.0;   // when it happened
	// How it reached the brain (V1 WP3): only Sight keeps the source's live position known, the rest give a last-known one.
	EMDStimulusKind Kind = EMDStimulusKind::Sight;
	// The noise tag of a Hearing stimulus (Attack / Sprint / Gunshot / any).
	FName Tag = NAME_None;
};

// Reaction floor (1.3): stimuli are timestamped and only handed to the brain once they are older than ReactionTime.
struct MAZEDEMONS_API FMDDelayedStimulusQueue
{
	void Push(const FMDStimulus& Stimulus) { Pending.Add(Stimulus); }

	// Like Push, but a stimulus of the same source and kind that is still waiting keeps its delivery time and only takes the
	// newer location. A 40-enemy pack sharing every sighting would otherwise queue one entry per mate per event (V1 WP3).
	void PushCoalesced(const FMDStimulus& Stimulus);

	// Removes and returns (through Fn, oldest first) every stimulus with Time + Delay <= Now. Returns how many were delivered.
	int32 Pop(double Now, double Delay, TFunctionRef<void(const FMDStimulus&)> Fn);

	int32 Num() const { return Pending.Num(); }
	void Reset() { Pending.Reset(); }

private:
	TArray<FMDStimulus> Pending;
};

// Attack tokens (1.3 / 2.4), keyed on plain ids so the rule is testable: at most MaxPerTarget valid tokens per target; a
// token is held until released, until Timeout, or (after the attack ended) HoldAfterAttack longer. WP1 uses one pool of
// MaxTokensPerPlayer = 1 (T0); WP2's pack director takes the same pool over.
struct MAZEDEMONS_API FMDAttackTokenPool
{
	struct FEntry
	{
		int32 Holder = 0;
		int32 Target = 0;
		double ExpireTime = 0.0;
	};

	// Grants (or refreshes) a token for Holder on Target. False when Target already has MaxPerTarget valid tokens held by others.
	bool TryGrant(int32 Holder, int32 Target, double Now, int32 MaxPerTarget, double Timeout = MDAI::TokenTimeout);

	bool Holds(int32 Holder, double Now) const;

	// The target of Holder's token, 0 when it holds none.
	int32 GetTarget(int32 Holder, double Now) const;

	int32 CountForTarget(int32 Target, double Now) const;

	// Returns the token now (stagger, death).
	void Release(int32 Holder);

	// The attack ended: the token expires in Delay seconds (or earlier if its timeout is sooner).
	void ReleaseAfter(int32 Holder, double Now, double Delay = MDAI::TokenHoldAfterAttack);

	// Drops expired entries.
	void Purge(double Now);

	int32 Num() const { return Entries.Num(); }

private:
	TArray<FEntry> Entries;
};

// Round-robin brain scheduler (2.9): all brains run at Hz on average, spread evenly over frames.
namespace MDBrainScheduling
{
	// How many brains to run this frame. Accumulator carries the fractional remainder between frames.
	MAZEDEMONS_API int32 BatchSize(int32 NumBrains, float Hz, float DeltaTime, float& Accumulator);
}

// The StateTree event tag (AI.Intent.<X>) for an intent the executor knows; an empty tag for the rest.
MAZEDEMONS_API FGameplayTag MDIntentToTag(EMDIntent Intent);

// Enemy attack timing and networking rules (shared by the attack component, the enemy anim instance and tests).
namespace MDEnemyAttackRules
{
	// Montage play rate during the wind-up so the first trace window starts TellTime after the montage started:
	// rate = TraceWindowStart / TellTime, clamped to [MinRate, 1]. Rate 1 when there is no window or no tell.
	MAZEDEMONS_API float WindupRate(float TraceWindowStart, float TellTime, float MinRate = 0.25f);

	// Who moves the attacker during a swing. Only the authority (listen server, dedicated server, standalone) applies the
	// montage root motion and the motion warp; the capsule then replicates through the movement component like any other
	// movement. A simulated proxy plays the same montage cosmetically and must NOT extract root motion or warp on its own:
	// a proxy that does fights the replicated movement with its own (differently warped) root motion, so the client sees
	// snaps, teleports at the end of the montage and, when the warp runs with a stale or coincident target, flips.
	MAZEDEMONS_API bool OwnsAttackMovement(ENetRole LocalRole);

	// The anim instance root-motion mode for a machine with LocalRole: montages drive the authority, proxies ignore root
	// motion (the root stays locked, so the pose plays in place on the replicated capsule).
	MAZEDEMONS_API ERootMotionMode::Type RootMotionModeFor(ENetRole LocalRole);

	// The actor rotation of an attacker facing TargetYaw: the capsule stays vertical whatever its current pitch/roll.
	MAZEDEMONS_API FRotator FacingRotation(float TargetYaw);

	// True when a rotation has drifted off vertical by more than ToleranceDegrees (a capsule should never pitch or roll).
	MAZEDEMONS_API bool IsTilted(const FRotator& Rotation, float ToleranceDegrees = 1.0f);
}
