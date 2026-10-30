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

	// Autonomous soft lock (attacking picks the best enemy in view; never touches the camera).
	float SoftRange = 1200.0f;             // 12 m (md.LockOn.SoftRange)
	float SoftBreakRange = 1500.0f;        // retarget at attack start beyond 15 m (md.LockOn.SoftBreakRange)
	float SoftTimeout = 4.0f;              // seconds without an attack before the soft lock drops (md.LockOn.SoftTimeout)
	float SoftViewMargin = 0.10f;          // fraction of the viewport the "in view" test is widened by (md.LockOn.SoftViewMargin)
	float SoftRetargetInputDeg = 60.0f;    // move input pointing further than this from the target may retarget
	float SoftInputConeDeg = 45.0f;        // ...when another candidate lies within this of the input
	float SoftInputFullDeg = 90.0f;        // alignment term reaches 0 at this angle from the input
	// Soft score weights: distance dominates, input alignment (only while moving) wins close calls, centre breaks ties.
	float SoftWeightDistance = 0.60f;
	float SoftWeightCentre = 0.15f;
	float SoftWeightInput = 0.35f;
};

// One soft-lock candidate. Only level-geometry LOS-passing, hostile, alive pawns are fed in.
struct MAZEDEMONS_API FMDSoftCandidate
{
	int32 Id = 0;
	float DistanceCm = 0.0f;
	float CentreOffset = 0.0f;     // 0 = screen centre, 1 = at the screen edge (see FMDLockOnRules::CentreOffset)
	float InputAngleDeg = -1.0f;   // planar angle between the move input and the direction to the candidate; < 0 = no input
	bool bInView = true;           // in the camera view (viewport widened by SoftViewMargin)
};

// What the soft retarget check looks at, evaluated at the start of every attack.
struct MAZEDEMONS_API FMDSoftRetargetInput
{
	bool bTargetValid = true;          // exists, alive, hostile, targetable
	bool bInView = true;
	float DistanceCm = 0.0f;
	bool bHasMoveInput = false;
	float TargetInputAngleDeg = 0.0f;  // angle between the move input and the direction to the current target
	bool bOtherInInputCone = false;    // another candidate lies within SoftInputConeDeg of the input
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

	// Line-of-sight: a blocking hit occludes only when it is level geometry (not another pawn, not attached to the player or the target).
	static bool IsLineOfSightOccluder(bool bHitIsPawn, bool bAttachedToPlayerOrTarget) { return !bHitIsPawn && !bAttachedToPlayerOrTarget; }

	// Server check of Server_SetLockTarget: alive, hostile, within range (+ tolerance) and seen within the LOS window.
	// A soft lock uses SoftBreakRange (15 m) + tolerance instead of MaxRange.
	static bool ValidateServerTarget(bool bAlive, bool bHostile, float DistanceCm, float SecondsSinceLOS, const FMDLockOnTuning& Tuning, bool bSoft = false);

	// ---- Autonomous soft lock ----

	// Soft score: SoftWeightDistance x close (1 at 0, 0 at SoftRange) + SoftWeightCentre x centred + (moving) SoftWeightInput x alignment.
	static float SoftScore(const FMDSoftCandidate& Candidate, const FMDLockOnTuning& Tuning, bool bHasMoveInput);

	// Best in-view candidate within SoftRange (ties: nearest, then lower Id); INDEX_NONE when none. Returns the candidate Id.
	static int32 PickSoftBest(const TArray<FMDSoftCandidate>& Candidates, const FMDLockOnTuning& Tuning, bool bHasMoveInput, float* OutScore = nullptr);

	// In view = projected in front of the camera and inside the viewport widened by Margin (fraction of the size) on every side.
	static bool IsInView(const FVector2D& Pixel, bool bProjected, const FVector2D& ViewSize, float Margin);

	// 0 at the screen centre, 1 at the nearest-axis screen edge (max of |dx| / half width and |dy| / half height; may exceed 1).
	static float CentreOffset(const FVector2D& Pixel, const FVector2D& ViewSize);

	// True when a candidate other than ExcludeId (and in view, within SoftRange) lies within SoftInputConeDeg of the move input.
	static bool HasCandidateInInputCone(const TArray<FMDSoftCandidate>& Candidates, int32 ExcludeId, const FMDLockOnTuning& Tuning);

	// At attack start: retarget when the target is invalid, out of view, beyond SoftBreakRange, or the move input points further than
	// SoftRetargetInputDeg from it while another candidate lies inside the input cone.
	static bool ShouldSoftRetarget(const FMDSoftRetargetInput& Input, const FMDLockOnTuning& Tuning);

	// The soft lock drops after SoftTimeout seconds without an attack.
	static bool IsSoftExpired(float SecondsSinceAttack, const FMDLockOnTuning& Tuning) { return SecondsSinceAttack > Tuning.SoftTimeout; }

	// An attack soft-locks only with the option on and no hard lock (a hard lock overrides the soft one).
	static bool ShouldSoftAcquire(bool bSoftEnabled, bool bHardLockActive) { return bSoftEnabled && !bHardLockActive; }

	// Only a hard lock drives the camera assist; a soft lock never touches the camera.
	static bool ShouldAssistCamera(bool bHardLock) { return bHardLock; }

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
