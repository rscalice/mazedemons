#pragma once

#include "CoreMinimal.h"
#include "AI/MDAITypes.h"

class AActor;
class UObject;

// V1 WP3 perception, dormancy and navigation rules (v1-design 2.5 / 2.6 / 2.9, 3.2). Plain functions and structs: no world,
// no actors, so every rule runs in MazeDemons.AI.* with no level. The enemy controller, the brain, the pack director and the
// AI subsystem are thin wrappers that feed these from the world. Distances in cm, times in seconds.

// How a stimulus reached the brain. Only Sight is "live": a sighted player's real position keeps updating the brain. Hearing,
// Damage and Shared stimuli only give a position (the last-known one), never the live one.
enum class EMDStimulusKind : uint8
{
	Sight = 0,
	Hearing,
	Damage,
	Shared
};

namespace MDAIPerf
{
	// Wall-clock time a subsystem spent in its Tick, summed over frames (2.9 budgets: brain 0.6 ms, director 0.3 ms per frame).
	struct FFrameProfile
	{
		double Seconds = 0.0;
		double PeakSeconds = 0.0;
		int32 Frames = 0;

		void Add(double FrameSeconds)
		{
			Seconds += FrameSeconds;
			PeakSeconds = FMath::Max(PeakSeconds, FrameSeconds);
			++Frames;
		}
		double AverageMs() const { return Frames > 0 ? Seconds / static_cast<double>(Frames) * 1000.0 : 0.0; }
		double PeakMs() const { return PeakSeconds * 1000.0; }
	};
}

namespace MDNoise
{
	// Tags the server puts on the noise events (UAISense_Hearing::ReportNoiseEvent). The hearing sense consumes ANY tag; these
	// three only choose loudness. Gunshot is reported by the gun fire pipeline (Ranged).
	inline constexpr const TCHAR* Attack = TEXT("Attack");
	inline constexpr const TCHAR* Sprint = TEXT("Sprint");
	inline constexpr const TCHAR* Gunshot = TEXT("Gunshot");

	// Seconds between the Sprint noise events of one sprinting player.
	constexpr double SprintReportInterval = 0.5;

	struct FProfile
	{
		// Multiplies the listener's hearing range (the engine's hearing sense: range = HearingRange x Loudness).
		float Loudness = 1.0f;
		// Hard cap on how far the event travels, whatever the listener's range (cm). 0 = no cap.
		float MaxRange = 0.0f;
	};

	// Attack: a plain melee step (heard inside the listener's hearing range). Sprint: quiet footfalls (half range).
	// Gunshot: loud, carries 2.5 x the hearing range up to 50 m. Any other tag: a plain noise (loudness 1).
	MAZEDEMONS_API FProfile ProfileFor(FName Tag);

	// Loud noises also alert the hearer's pack (shared like a sighting).
	MAZEDEMONS_API bool IsLoud(FName Tag);

	// Server: reports the noise to the perception system (no-op on a client, without a world or without a source).
	MAZEDEMONS_API void Report(const UObject* WorldContext, const FVector& Location, AActor* Source, FName Tag);
}

namespace MDPerception
{
	// Seconds after the last sighting before a sensed player is forgotten (lose-sight, and the senses' MaxAge).
	constexpr double LoseSightTime = 3.0;

	// Sight keeps a seen player until it is this much beyond the sight radius.
	constexpr float LoseSightExtraRadius = 300.0f;

	// Hearing range per tier (cm): T0 12 m ... T4 24 m. A Gunshot carries 2.5 x that.
	MAZEDEMONS_API float HearingRange(EMDIntelligenceTier Tier);

	// Sight, plus Hearing when the noise is loud (Gunshot), is shared with the pack.
	MAZEDEMONS_API bool ShouldShare(EMDStimulusKind Kind, FName Tag);

	// True for stimuli that keep the real position of the source known (a seen player), false for a remembered position.
	MAZEDEMONS_API bool IsLive(EMDStimulusKind Kind);

	// When a pack member's sighting made at SightTime reaches a pack mate: SightTime + max(ShareDelay, ReactionTime), so
	// the 1.3 reaction floor holds for shared knowledge too. ShareDelay < 0 (T0) never shares: returns a negative time.
	MAZEDEMONS_API double SharedArrivalTime(double SightTime, float ShareDelay, float ReactionTime);

	// The timestamp to queue the shared stimulus with in a brain whose delayed queue releases it ReactionTime later, so that
	// it is delivered exactly at SharedArrivalTime. Negative = not shared.
	MAZEDEMONS_API double SharedQueueTime(double SightTime, float ShareDelay, float ReactionTime);
}

namespace MDDormancy
{
	// Enemies further than this from every player stop sensing and thinking (2.9).
	constexpr float SleepDistance = 4000.0f;

	// A hurt enemy stays awake this long even with no player in range (it is being shot from afar).
	constexpr double DamageAwakeTime = 10.0;

	// NetUpdateFrequency beyond NetReduceDistance (2.8).
	constexpr float NetReduceDistance = 2500.0f;
	constexpr float ReducedNetFrequency = 10.0f;

	// NearestPlayerDistance < 0 = there is no player (loading, tests): never dormant. SecondsSinceDamage < 0 = never hurt.
	MAZEDEMONS_API bool ShouldSleep(float NearestPlayerDistance, double SecondsSinceDamage);

	// The pawn's net update frequency: DefaultHz inside NetReduceDistance (or with no player known), ReducedNetFrequency beyond.
	MAZEDEMONS_API float NetUpdateFrequencyFor(float NearestPlayerDistance, float DefaultHz);
}

namespace MDNav
{
	// Repath only when the goal moved further than this (2.9): 1.5 m.
	constexpr float RepathDistance = 150.0f;

	// Path requests granted per frame across all enemies (2.9).
	constexpr int32 PathRequestsPerFrame = 10;

	// A move is (re)requested when none is running, or the goal moved more than Threshold (2D) since the last request.
	MAZEDEMONS_API bool ShouldRepath(bool bMoving, const FVector& LastGoal, const FVector& NewGoal, float Threshold = RepathDistance);

	// The per-frame path request budget. TryConsume(Frame) grants up to Limit requests per distinct frame number.
	struct MAZEDEMONS_API FPathBudget
	{
		bool TryConsume(uint64 FrameNumber, int32 Limit = PathRequestsPerFrame);
		int32 GetUsed() const { return Used; }
		int32 GetDenied() const { return Denied; }

	private:
		uint64 Frame = 0;
		int32 Used = 0;
		int32 Denied = 0;
	};
}
