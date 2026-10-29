// V1 WP7: pure soft lock-on rules (no world access, unit tested: MazeDemons.LockOn.*). v1-design 5.1.
#pragma once

#include "CoreMinimal.h"

// Tunables of the lock-on rules. Defaults are the v1-design 5.1 numbers (distances in cm).
struct MAZEDEMONS_API FMDLockOnTuning
{
	// Candidates: hostile, alive, within MaxRange (30 m).
	float MaxRange = 3000.0f;
	// Score weights (sum 1.0): centred, close, recently damaged me, about to hit me.
	float WeightCentre = 0.45f;
	float WeightDistance = 0.30f;
	float WeightDamagedMe = 0.15f;
	float WeightThreat = 0.10f;
	// Angle from screen centre that scores 0 on the centre term.
	float CentreAngleDeg = 60.0f;
	// First lock also accepts targets this many degrees past the screen edge.
	float EdgeSlackDeg = 20.0f;

	// Break rules.
	float BreakRange = 3500.0f;            // target further than 35 m
	float NoLOSBreakSeconds = 2.5f;
	float AutoRetargetRange = 1200.0f;     // 12 m
	float AutoRetargetAttackWindow = 2.0f; // the player attacked within this long

	// Server validation (Server_SetLockTarget).
	float ServerRangeTolerance = 300.0f;   // the server position lags the client's
	float ServerLOSWindow = 2.0f;

	// Motion warp: a locked target counts when within WarpRange x this and within WarpAngleDeg of the input direction.
	float WarpRangeScale = 1.25f;
	float WarpAngleDeg = 90.0f;

	// Camera assist.
	float AssistInputPauseSeconds = 0.4f;
	float AssistPitchClampDeg = 35.0f;
	float AssistTargetScreenX = 0.5f;      // fraction of screen width
	float AssistTargetScreenY = 0.35f;     // fraction of screen height (from the top)
	float AssistFollowRate = 6.0f;         // exponential follow rate per second at strength 1
};

// One lock-on candidate as the scorer sees it.
struct MAZEDEMONS_API FMDLockOnCandidate
{
	int32 Id = 0;
	float ScreenX = 0.0f;               // pixels (or any monotonic screen X): cycle order
	float AngleFromCentreDeg = 0.0f;    // angle between the camera forward and the direction to the target
	float DistanceCm = 0.0f;
	bool bRecentlyDamagedMe = false;
	bool bThreatening = false;          // holds the attack token on the player (replicated bool on the enemy)
	bool bOnScreen = true;
	bool bWithinEdgeSlack = false;      // off screen but within EdgeSlackDeg of the screen edge (first lock only)
};

enum class EMDLockBreak : uint8
{
	None = 0,
	TargetDied,
	TargetInvalid,
	TooFar,
	NoLineOfSight,
	PlayerDied,
	MenuOpened,
	Manual
};

// What the break check looks at.
struct MAZEDEMONS_API FMDLockOnBreakInput
{
	bool bTargetValid = true;   // exists and is still hostile / targetable
	bool bTargetAlive = true;
	float DistanceCm = 0.0f;
	bool bHasLOS = true;
	bool bPlayerDead = false;
	bool bMenuOpen = false;
};

struct MAZEDEMONS_API FMDLockOnRules
{
	// 0.45 x centred + 0.30 x close + 0.15 x damaged me + 0.10 x threatening. Each term clamps to 0..1.
	static float Score(const FMDLockOnCandidate& Candidate, const FMDLockOnTuning& Tuning);

	// Id of the best candidate (ties: nearest, then lower Id); INDEX_NONE when none qualifies. bFirstLock widens the screen test by
	// EdgeSlackDeg; otherwise off-screen candidates are skipped. OutScore receives the winner's score.
	static int32 PickBest(const TArray<FMDLockOnCandidate>& Candidates, const FMDLockOnTuning& Tuning, bool bFirstLock, float* OutScore = nullptr);

	// Next target in Direction (+1 right, -1 left) by screen X, wrapping. Only on-screen candidates count. When Current is not among
	// them the result is the leftmost (Direction > 0) or rightmost (Direction < 0) candidate. INDEX_NONE with no candidates.
	static int32 PickCycle(const TArray<FMDLockOnCandidate>& Candidates, int32 CurrentId, int32 Direction);

	// Ids of the on-screen candidates left / right of Current (INDEX_NONE when there is none): the reticle hint ticks.
	static void FindNeighbours(const TArray<FMDLockOnCandidate>& Candidates, int32 CurrentId, int32& OutLeft, int32& OutRight);

	// Advances InOutNoLOSSeconds (reset while the target is visible) and returns the break reason, None when the lock holds.
	static EMDLockBreak EvaluateBreak(const FMDLockOnBreakInput& Input, float DeltaSeconds, const FMDLockOnTuning& Tuning, float& InOutNoLOSSeconds);

	// After a TargetDied break: keep fighting when the player attacked within the window and the best candidate is close enough.
	static bool ShouldAutoRetarget(EMDLockBreak Reason, float SecondsSincePlayerAttack, float BestCandidateDistanceCm, const FMDLockOnTuning& Tuning);

	// Motion warp: should the locked target replace C1's cone pick? Needs the target within WarpRange x scale and, when the
	// player is moving, within WarpAngleDeg of the input direction. Planar.
	static bool UseLockedTargetForWarp(bool bHasMoveInput, const FVector& InputDir, const FVector& ToTarget, float WarpRange, const FMDLockOnTuning& Tuning);

	// Locked and no move input: the step snaps to face the target instead of the camera.
	static bool ShouldSnapToTarget(bool bLocked, bool bHasMoveInput) { return bLocked && !bHasMoveInput; }

	// Server check of Server_SetLockTarget: alive, hostile, within range (+ tolerance) and seen within the LOS window.
	static bool ValidateServerTarget(bool bAlive, bool bHostile, float DistanceCm, float SecondsSinceLOS, const FMDLockOnTuning& Tuning);

	// Camera assist: degrees to turn this frame to bring ErrorDeg to zero. Zero while the player moved the camera within the
	// pause window (the assist never fights input) or at strength 0. Exponential, never overshoots.
	static float AssistStep(float ErrorDeg, float Strength01, float SecondsSincePlayerInput, float DeltaSeconds, const FMDLockOnTuning& Tuning);

	// Yaw / pitch (degrees) to turn the camera so the target lands at the goal pixel (35 % height, 50 % width). X: positive turns
	// right. Y: positive looks up. Pinhole camera with the horizontal FOV.
	static FVector2D ScreenErrorDegrees(const FVector2D& TargetPixel, const FVector2D& ViewSize, float HorizontalFovDeg, const FMDLockOnTuning& Tuning);

	// Camera pitch kept inside +-Clamp while the assist is turning it (never forces a pitch the player already has beyond it).
	static float ClampAssistPitch(float CurrentPitchDeg, float NewPitchDeg, const FMDLockOnTuning& Tuning);

	// Flick detector for the right stick: fires once when |X| passes Deadzone (0.6), then not again until the stick re-centred
	// and RearmSeconds (0.25) passed. Update returns -1 / 0 / +1.
	struct MAZEDEMONS_API FFlick
	{
		bool bArmed = true;
		float SinceFire = 10.0f;
		int32 Update(float StickX, float DeltaSeconds, float Deadzone = 0.6f, float RearmSeconds = 0.25f);
	};
};

// Pawn separation (V1 WP7 "players and enemies don't visibly overlap"). Pure math, planar.
struct MAZEDEMONS_API FMDPawnSeparation
{
	// Push for the pawn at A away from B when their capsules (radii RA, RB, plus Skin of breathing room) overlap by more than
	// MinPenetration. Returns zero otherwise. The vector length is min(penetration x Strength, MaxPush); it is the share of the
	// correction A takes (ShareA 0..1, B takes 1 - ShareA via the mirrored call).
	static FVector ComputePush(const FVector& A, float RA, const FVector& B, float RB, float Skin, float MinPenetration, float Strength, float MaxPush, float ShareA);

	// Overlap depth of two capsules in the plane (negative = gap).
	static float Penetration(const FVector& A, float RA, const FVector& B, float RB);
};
