#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LockOn/MDLockOnComponent.h"
#include "LockOn/MDLockOnRules.h"
#include "LockOn/MDPawnSeparationSubsystem.h"
#include "MDWarpTargeting.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnScoreWeightsTest, "MazeDemons.LockOn.ScoreWeights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnCycleOrderTest, "MazeDemons.LockOn.CycleOrderScreenX", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnBreakTest, "MazeDemons.LockOn.BreakOnDistanceLOSDeath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnRetargetTest, "MazeDemons.LockOn.AutoRetargetWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnWarpTest, "MazeDemons.LockOn.WarpOverrideWithinRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnServerTest, "MazeDemons.LockOn.ServerValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnAssistTest, "MazeDemons.LockOn.CameraAssist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSeparationTest, "MazeDemons.LockOn.PawnSeparation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftAcquireTest, "MazeDemons.LockOn.SoftAcquireOnAttack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftInputTest, "MazeDemons.LockOn.SoftInputSteer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftChainTest, "MazeDemons.LockOn.SoftChainOnKill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftTimeoutTest, "MazeDemons.LockOn.SoftTimeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnHardOverridesSoftTest, "MazeDemons.LockOn.HardOverridesSoft", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftNoCameraTest, "MazeDemons.LockOn.SoftNoCameraAssist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLockOnSoftServerTest, "MazeDemons.LockOn.SoftServerRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static FMDLockOnCandidate MakeLockCandidate(int32 Id, float ScreenX, float AngleDeg, float DistanceCm, bool bOnScreen = true)
{
	FMDLockOnCandidate C;
	C.Id = Id;
	C.ScreenX = ScreenX;
	C.AngleFromCentreDeg = AngleDeg;
	C.DistanceCm = DistanceCm;
	C.bOnScreen = bOnScreen;
	return C;
}

bool FMDLockOnScoreWeightsTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	TestTrue(TEXT("Weights sum to 1"), FMath::IsNearlyEqual(T.WeightCentre + T.WeightDistance + T.WeightDamagedMe + T.WeightThreat, 1.0f, 1.0e-4f));

	// Dead centre and touching scores the full 0.75 of the two geometric terms; add both flags for 1.0.
	FMDLockOnCandidate Perfect = MakeLockCandidate(0, 0, 0.0f, 0.0f);
	TestTrue(TEXT("Centre + close = 0.75"), FMath::IsNearlyEqual(FMDLockOnRules::Score(Perfect, T), 0.75f, 1.0e-4f));
	Perfect.bRecentlyDamagedMe = true;
	TestTrue(TEXT("+ damaged me = 0.90"), FMath::IsNearlyEqual(FMDLockOnRules::Score(Perfect, T), 0.90f, 1.0e-4f));
	Perfect.bThreatening = true;
	TestTrue(TEXT("+ threatening = 1.00"), FMath::IsNearlyEqual(FMDLockOnRules::Score(Perfect, T), 1.0f, 1.0e-4f));

	// Half-way angle and half-way distance: 0.45 x 0.5 + 0.30 x 0.5.
	TestTrue(TEXT("30 deg / 15 m = 0.375"), FMath::IsNearlyEqual(FMDLockOnRules::Score(MakeLockCandidate(0, 0, 30.0f, 1500.0f), T), 0.375f, 1.0e-4f));
	// Terms clamp at zero: beyond 60 deg and beyond range score nothing instead of going negative.
	TestEqual(TEXT("Far off-axis and far away clamp to 0"), FMDLockOnRules::Score(MakeLockCandidate(0, 0, 170.0f, 9000.0f), T), 0.0f);

	// Centred beats close: A centred at 24 m (0.45 + 0.06) over B 30 deg off at 3 m (0.225 + 0.27).
	TArray<FMDLockOnCandidate> C;
	C.Add(MakeLockCandidate(7, 0, 0.0f, 2400.0f));
	C.Add(MakeLockCandidate(9, 0, 30.0f, 300.0f));
	TestEqual(TEXT("Centred target wins"), FMDLockOnRules::PickBest(C, T, false), 7);
	// The threatening token holder nudges a tie-ish pair over: same geometry, one threatens.
	C.Reset();
	C.Add(MakeLockCandidate(1, 0, 10.0f, 1000.0f));
	C.Add(MakeLockCandidate(2, 0, 10.0f, 1000.0f));
	C[1].bThreatening = true;
	TestEqual(TEXT("Threat breaks the tie"), FMDLockOnRules::PickBest(C, T, false), 2);
	// Exact tie: nearest, then lower id.
	C.Reset();
	C.Add(MakeLockCandidate(5, 0, 0.0f, 1000.0f));
	C.Add(MakeLockCandidate(3, 0, 0.0f, 1000.0f));
	TestEqual(TEXT("Exact tie: lower id"), FMDLockOnRules::PickBest(C, T, false), 3);

	// Range and screen filters; the first lock accepts targets just past the screen edge.
	C.Reset();
	C.Add(MakeLockCandidate(1, 0, 0.0f, 3100.0f)); // beyond 30 m
	C.Add(MakeLockCandidate(2, 0, 40.0f, 500.0f, false));
	TestEqual(TEXT("Nothing in range and on screen"), FMDLockOnRules::PickBest(C, T, false), (int32)INDEX_NONE);
	C[1].bWithinEdgeSlack = true;
	TestEqual(TEXT("Still nothing when not the first lock"), FMDLockOnRules::PickBest(C, T, false), (int32)INDEX_NONE);
	TestEqual(TEXT("First lock accepts the edge slack"), FMDLockOnRules::PickBest(C, T, true), 2);
	return true;
}

bool FMDLockOnCycleOrderTest::RunTest(const FString& Parameters)
{
	// Screen X order: id 1 (100) < id 2 (500) < id 0 (900). id 3 is off screen and never picked.
	TArray<FMDLockOnCandidate> C;
	C.Add(MakeLockCandidate(0, 900, 0, 100));
	C.Add(MakeLockCandidate(1, 100, 0, 100));
	C.Add(MakeLockCandidate(2, 500, 0, 100));
	C.Add(MakeLockCandidate(3, 300, 0, 100, false));

	TestEqual(TEXT("Right of the middle one is the right-most"), FMDLockOnRules::PickCycle(C, 2, +1), 0);
	TestEqual(TEXT("Right of the right-most wraps to the left-most"), FMDLockOnRules::PickCycle(C, 0, +1), 1);
	TestEqual(TEXT("Left of the middle one"), FMDLockOnRules::PickCycle(C, 2, -1), 1);
	TestEqual(TEXT("Left of the left-most wraps to the right-most"), FMDLockOnRules::PickCycle(C, 1, -1), 0);
	TestEqual(TEXT("No current: right starts at the left-most"), FMDLockOnRules::PickCycle(C, INDEX_NONE, +1), 1);
	TestEqual(TEXT("No current: left starts at the right-most"), FMDLockOnRules::PickCycle(C, INDEX_NONE, -1), 0);
	TestEqual(TEXT("An off-screen current id behaves like none"), FMDLockOnRules::PickCycle(C, 3, +1), 1);

	// Full lap visits every on-screen target once and never the off-screen one.
	TSet<int32> Seen;
	int32 Current = 1;
	for (int32 i = 0; i < 3; ++i)
	{
		Current = FMDLockOnRules::PickCycle(C, Current, +1);
		Seen.Add(Current);
	}
	TestEqual(TEXT("A lap visits the three on-screen targets"), Seen.Num(), 3);
	TestFalse(TEXT("Never the off-screen target"), Seen.Contains(3));

	TestEqual(TEXT("A lone target stays"), FMDLockOnRules::PickCycle({ MakeLockCandidate(4, 10, 0, 0) }, 4, +1), 4);
	TestEqual(TEXT("No candidates"), FMDLockOnRules::PickCycle(TArray<FMDLockOnCandidate>(), 0, +1), (int32)INDEX_NONE);

	int32 Left = 0;
	int32 Right = 0;
	FMDLockOnRules::FindNeighbours(C, 2, Left, Right);
	TestEqual(TEXT("Left neighbour tick"), Left, 1);
	TestEqual(TEXT("Right neighbour tick"), Right, 0);
	FMDLockOnRules::FindNeighbours(C, 1, Left, Right);
	TestEqual(TEXT("No neighbour left of the left-most"), Left, (int32)INDEX_NONE);
	TestEqual(TEXT("Neighbour right of the left-most"), Right, 2);

	// Right-stick flick: one cycle per flick, re-armed by centring the stick and 0.25 s.
	FMDLockOnRules::FFlick Flick;
	TestEqual(TEXT("Below the deadzone"), Flick.Update(0.5f, 0.016f), 0);
	TestEqual(TEXT("Flick right"), Flick.Update(0.9f, 0.016f), 1);
	TestEqual(TEXT("Held stick does not repeat"), Flick.Update(1.0f, 0.5f), 0);
	Flick.Update(0.0f, 0.016f);
	TestEqual(TEXT("Flick left after centring"), Flick.Update(-0.9f, 0.016f), -1);

	FMDLockOnRules::FFlick Quick;
	TestEqual(TEXT("Quick: first flick"), Quick.Update(0.9f, 0.016f), 1);
	TestEqual(TEXT("Quick: centred but inside the 0.25 s re-arm time"), Quick.Update(0.0f, 0.05f), 0);
	TestEqual(TEXT("Quick: a second flick this soon is ignored"), Quick.Update(-0.9f, 0.016f), 0);
	Quick.Update(0.0f, 0.3f);
	TestEqual(TEXT("Quick: flick left after the re-arm time"), Quick.Update(-0.9f, 0.016f), -1);
	return true;
}

bool FMDLockOnBreakTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	float NoLOS = 0.0f;
	FMDLockOnBreakInput In;
	In.DistanceCm = 2000.0f;
	TestTrue(TEXT("Healthy lock holds"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::None);

	// Distance: > 35 m breaks, 34 m does not.
	In.DistanceCm = 3400.0f;
	TestTrue(TEXT("34 m holds"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::None);
	In.DistanceCm = 3501.0f;
	TestTrue(TEXT("35.01 m breaks"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::TooFar);

	// No LOS for 2.5 s: accumulates, resets while visible.
	In.DistanceCm = 1000.0f;
	In.bHasLOS = false;
	float Total = 0.0f;
	EMDLockBreak Result = EMDLockBreak::None;
	while (Total < 2.4f)
	{
		Result = FMDLockOnRules::EvaluateBreak(In, 0.1f, T, NoLOS);
		Total += 0.1f;
	}
	TestTrue(TEXT("2.4 s without LOS holds"), (Result) == EMDLockBreak::None);
	In.bHasLOS = true;
	TestTrue(TEXT("Seeing the target again holds"), (FMDLockOnRules::EvaluateBreak(In, 0.1f, T, NoLOS)) == EMDLockBreak::None);
	TestEqual(TEXT("...and resets the timer"), NoLOS, 0.0f);
	In.bHasLOS = false;
	NoLOS = 0.0f;
	FMDLockOnRules::EvaluateBreak(In, 2.4f, T, NoLOS);
	TestTrue(TEXT("2.5 s without LOS breaks"), (FMDLockOnRules::EvaluateBreak(In, 0.2f, T, NoLOS)) == EMDLockBreak::NoLineOfSight);

	// Death, validity, player death, menu.
	In = FMDLockOnBreakInput();
	NoLOS = 0.0f;
	In.bTargetAlive = false;
	TestTrue(TEXT("Target died"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::TargetDied);
	In = FMDLockOnBreakInput();
	In.bTargetValid = false;
	TestTrue(TEXT("Target no longer targetable"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::TargetInvalid);
	In = FMDLockOnBreakInput();
	In.bPlayerDead = true;
	TestTrue(TEXT("Player died"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::PlayerDied);
	In = FMDLockOnBreakInput();
	In.bMenuOpen = true;
	TestTrue(TEXT("Menu opened"), (FMDLockOnRules::EvaluateBreak(In, 0.016f, T, NoLOS)) == EMDLockBreak::MenuOpened);
	return true;
}

bool FMDLockOnRetargetTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	TestTrue(TEXT("Attacked 1 s ago, next enemy at 8 m"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 1.0f, 800.0f, T));
	TestTrue(TEXT("Exactly at the window and range edges"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 2.0f, 1200.0f, T));
	TestFalse(TEXT("Attacked 2.5 s ago"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 2.5f, 800.0f, T));
	TestFalse(TEXT("Next enemy 13 m away"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 1.0f, 1300.0f, T));
	TestFalse(TEXT("No candidate"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 1.0f, -1.0f, T));
	TestFalse(TEXT("Only a death retargets (distance break)"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TooFar, 1.0f, 800.0f, T));
	TestFalse(TEXT("Only a death retargets (menu)"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::MenuOpened, 1.0f, 800.0f, T));
	TestFalse(TEXT("Never attacked"), FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak::TargetDied, 1.0e6f, 800.0f, T));
	return true;
}

bool FMDLockOnWarpTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	const FVector Forward(1, 0, 0);

	// Within WarpRange x 1.25 (500 -> 625) overrides; beyond does not.
	TestTrue(TEXT("In range, no input"), FMDLockOnRules::UseLockedTargetForWarp(false, FVector::ZeroVector, FVector(600, 0, 0), 500.0f, T));
	TestFalse(TEXT("Beyond 1.25 x range"), FMDLockOnRules::UseLockedTargetForWarp(false, FVector::ZeroVector, FVector(650, 0, 0), 500.0f, T));
	TestTrue(TEXT("Z is ignored"), FMDLockOnRules::UseLockedTargetForWarp(false, FVector::ZeroVector, FVector(600, 0, 900), 500.0f, T));

	// Moving: the target must be within 90 degrees of the input.
	TestTrue(TEXT("Input toward the target"), FMDLockOnRules::UseLockedTargetForWarp(true, Forward, FVector(300, 100, 0), 500.0f, T));
	TestTrue(TEXT("Input exactly sideways (90 deg)"), FMDLockOnRules::UseLockedTargetForWarp(true, FVector(0, 1, 0), FVector(300, 0, 0), 500.0f, T));
	TestFalse(TEXT("Input away from the target falls back to the cone"), FMDLockOnRules::UseLockedTargetForWarp(true, FVector(-1, 0, 0), FVector(300, 0, 0), 500.0f, T));
	TestTrue(TEXT("Target on top of the player"), FMDLockOnRules::UseLockedTargetForWarp(true, FVector(-1, 0, 0), FVector(0.1, 0, 0), 500.0f, T));

	// Snap to target only when locked and not moving.
	TestTrue(TEXT("Locked, standing: snap"), FMDLockOnRules::ShouldSnapToTarget(true, false));
	TestFalse(TEXT("Locked, moving: snap to input"), FMDLockOnRules::ShouldSnapToTarget(true, true));
	TestFalse(TEXT("Not locked: snap to input"), FMDLockOnRules::ShouldSnapToTarget(false, false));

	// The warp point respects both capsule radii: centres end at least radiusA + radiusB + stop distance apart.
	const float AttackerRadius = 34.0f;
	const float TargetRadius = 42.0f;
	const float Stop = 90.0f;
	const FVector Origin(0, 0, 0);
	const FVector Target(400, 300, 0);
	const FVector Point = MDWarpTargeting::ComputeWarpPoint(Origin, Target, TargetRadius + AttackerRadius, Stop);
	TestTrue(TEXT("Warp ends radiusA + radiusB + stop short of the target"),
		FMath::IsNearlyEqual(static_cast<float>(MDWarpTargeting::PlanarDistance(Point, Target)), TargetRadius + AttackerRadius + Stop, 0.01f));
	TestTrue(TEXT("Never inside the combined radii"), MDWarpTargeting::PlanarDistance(Point, Target) >= TargetRadius + AttackerRadius);
	// Already closer than that: the attacker stays put rather than warping through the target.
	const FVector Close(120, 0, 0);
	const FVector Stay = MDWarpTargeting::ComputeWarpPoint(Origin, Close, TargetRadius + AttackerRadius, Stop);
	TestTrue(TEXT("Too close: no forward motion"), FMath::IsNearlyEqual(static_cast<float>(Stay.X), 0.0f) && FMath::IsNearlyEqual(static_cast<float>(Stay.Y), 0.0f));
	return true;
}

bool FMDLockOnServerTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	TestTrue(TEXT("Alive, hostile, 10 m, seen now"), FMDLockOnRules::ValidateServerTarget(true, true, 1000.0f, 0.0f, T));
	TestTrue(TEXT("Up to range + tolerance (33 m)"), FMDLockOnRules::ValidateServerTarget(true, true, 3300.0f, 0.0f, T));
	TestFalse(TEXT("Beyond range + tolerance"), FMDLockOnRules::ValidateServerTarget(true, true, 3301.0f, 0.0f, T));
	TestTrue(TEXT("Seen 1.5 s ago still counts"), FMDLockOnRules::ValidateServerTarget(true, true, 1000.0f, 1.5f, T));
	TestTrue(TEXT("Seen exactly 2 s ago"), FMDLockOnRules::ValidateServerTarget(true, true, 1000.0f, 2.0f, T));
	TestFalse(TEXT("Not seen for 2.5 s"), FMDLockOnRules::ValidateServerTarget(true, true, 1000.0f, 2.5f, T));
	TestFalse(TEXT("Dead"), FMDLockOnRules::ValidateServerTarget(false, true, 1000.0f, 0.0f, T));
	TestFalse(TEXT("Not hostile (a teammate)"), FMDLockOnRules::ValidateServerTarget(true, false, 1000.0f, 0.0f, T));

	// Line of sight: only level geometry occludes (a weapon attached to the target or player, or another pawn, never does).
	TestTrue(TEXT("A wall occludes"), FMDLockOnRules::IsLineOfSightOccluder(false, false));
	TestFalse(TEXT("Another pawn does not occlude"), FMDLockOnRules::IsLineOfSightOccluder(true, false));
	TestFalse(TEXT("An attached weapon does not occlude"), FMDLockOnRules::IsLineOfSightOccluder(false, true));

	// The component: no world, no pawn, no target - every hint is refused, nothing sticks.
	UMDLockOnComponent* Lock = NewObject<UMDLockOnComponent>(GetTransientPackage());
	TestNotNull(TEXT("Component created"), Lock);
	if (Lock)
	{
		TestFalse(TEXT("A null hint is refused"), Lock->ValidateTargetOnServer(nullptr));
		Lock->Server_SetLockTarget_Implementation(nullptr, false);
		TestNull(TEXT("Server copy empty"), Lock->GetServerLockTarget());
		TestFalse(TEXT("No lock"), Lock->HasLockTarget());
		FVector Point;
		TestFalse(TEXT("No aim point without a lock"), Lock->GetLockAimPoint(Point));
		TestNull(TEXT("No warp override without a lock"), Lock->GetWarpOverrideTarget(false, FVector::ZeroVector, 500.0f));
	}
	return true;
}

bool FMDLockOnAssistTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;

	// Never fights the player: zero within 0.4 s of camera input, and at strength 0.
	TestEqual(TEXT("Paused right after input"), FMDLockOnRules::AssistStep(10.0f, 0.6f, 0.1f, 0.016f, T), 0.0f);
	TestEqual(TEXT("Paused just before 0.4 s"), FMDLockOnRules::AssistStep(10.0f, 0.6f, 0.39f, 0.016f, T), 0.0f);
	TestTrue(TEXT("Active after 0.4 s"), FMDLockOnRules::AssistStep(10.0f, 0.6f, 0.4f, 0.016f, T) > 0.0f);
	TestEqual(TEXT("Strength 0 is reticle only"), FMDLockOnRules::AssistStep(10.0f, 0.0f, 5.0f, 0.016f, T), 0.0f);

	// Soft: moves toward the error, never past it, a stronger setting moves more.
	const float Weak = FMDLockOnRules::AssistStep(10.0f, 0.3f, 5.0f, 0.016f, T);
	const float Strong = FMDLockOnRules::AssistStep(10.0f, 1.0f, 5.0f, 0.016f, T);
	TestTrue(TEXT("Stronger moves more"), Strong > Weak && Weak > 0.0f);
	TestTrue(TEXT("Never overshoots, even on a long frame"), FMDLockOnRules::AssistStep(10.0f, 1.0f, 5.0f, 5.0f, T) <= 10.0f);
	TestTrue(TEXT("Negative errors turn the other way"), FMDLockOnRules::AssistStep(-10.0f, 0.6f, 5.0f, 0.016f, T) < 0.0f);
	float Error = 20.0f;
	for (int32 i = 0; i < 120; ++i)
	{
		Error -= FMDLockOnRules::AssistStep(Error, 0.6f, 5.0f, 1.0f / 60.0f, T);
	}
	TestTrue(TEXT("Converges in about two seconds"), FMath::Abs(Error) < 0.5f);

	// Screen error: 1920x1080, 90 degree horizontal FOV. Goal is 50 % width, 35 % height.
	const FVector2D View(1920, 1080);
	const FVector2D AtGoal = FMDLockOnRules::ScreenErrorDegrees(FVector2D(960, 378), View, 90.0f, T);
	TestTrue(TEXT("At the goal pixel: no error"), FMath::Abs(AtGoal.X) < 0.01f && FMath::Abs(AtGoal.Y) < 0.01f);
	TestTrue(TEXT("Right of centre: turn right (+yaw)"), FMDLockOnRules::ScreenErrorDegrees(FVector2D(1500, 378), View, 90.0f, T).X > 5.0f);
	TestTrue(TEXT("Left of centre: turn left"), FMDLockOnRules::ScreenErrorDegrees(FVector2D(400, 378), View, 90.0f, T).X < -5.0f);
	TestTrue(TEXT("Above the goal: look up (+pitch)"), FMDLockOnRules::ScreenErrorDegrees(FVector2D(960, 100), View, 90.0f, T).Y > 5.0f);
	TestTrue(TEXT("Below the goal: look down"), FMDLockOnRules::ScreenErrorDegrees(FVector2D(960, 900), View, 90.0f, T).Y < -5.0f);
	const FVector2D EdgeRight = FMDLockOnRules::ScreenErrorDegrees(FVector2D(1920, 378), View, 90.0f, T);
	TestTrue(TEXT("Right screen edge is 45 degrees at a 90 degree FOV"), FMath::IsNearlyEqual(EdgeRight.X, 45.0f, 0.05f));

	// Pitch clamp +-35, but it never yanks a camera the player already tilted further.
	TestEqual(TEXT("Inside the clamp"), FMDLockOnRules::ClampAssistPitch(10.0f, 20.0f, T), 20.0f);
	TestEqual(TEXT("Clamped above"), FMDLockOnRules::ClampAssistPitch(30.0f, 40.0f, T), 35.0f);
	TestEqual(TEXT("Clamped below"), FMDLockOnRules::ClampAssistPitch(-30.0f, -40.0f, T), -35.0f);
	TestEqual(TEXT("Player already past the clamp keeps their pitch"), FMDLockOnRules::ClampAssistPitch(50.0f, 55.0f, T), 50.0f);
	TestEqual(TEXT("...and may ease back toward it"), FMDLockOnRules::ClampAssistPitch(50.0f, 45.0f, T), 45.0f);
	return true;
}

bool FMDLockOnSeparationTest::RunTest(const FString& Parameters)
{
	// Planar penetration of two 34 cm capsules 50 cm apart is 18 cm.
	TestTrue(TEXT("Penetration"), FMath::IsNearlyEqual(FMDPawnSeparation::Penetration(FVector(0, 0, 0), 34.0f, FVector(50, 0, 80), 34.0f), 18.0f, 0.001f));
	TestTrue(TEXT("A gap is negative"), FMDPawnSeparation::Penetration(FVector(0, 0, 0), 34.0f, FVector(100, 0, 0), 34.0f) < 0.0f);

	// No push without overlap (the skin counts: 70 cm apart leaves exactly the skin; 69.5 overlaps it by 0.5 cm, under the 1 cm tolerance).
	TestTrue(TEXT("Apart: no push"), FMDPawnSeparation::ComputePush(FVector(0, 0, 0), 34.0f, FVector(100, 0, 0), 34.0f, 2.0f, 1.0f, 0.5f, 100.0f, 1.0f).IsNearlyZero());
	TestTrue(TEXT("Within the tolerance: no push"), FMDPawnSeparation::ComputePush(FVector(0, 0, 0), 34.0f, FVector(69.5, 0, 0), 34.0f, 2.0f, 1.0f, 0.5f, 100.0f, 1.0f).IsNearlyZero());

	// Overlapping: A is pushed directly away from B, B the other way, in the plane.
	const FVector PushA = FMDPawnSeparation::ComputePush(FVector(0, 0, 0), 34.0f, FVector(50, 0, 40), 34.0f, 2.0f, 1.0f, 0.5f, 100.0f, 0.5f);
	const FVector PushB = FMDPawnSeparation::ComputePush(FVector(50, 0, 40), 34.0f, FVector(0, 0, 0), 34.0f, 2.0f, 1.0f, 0.5f, 100.0f, 0.5f);
	TestTrue(TEXT("A is pushed to -X"), PushA.X < 0.0 && FMath::IsNearlyZero(PushA.Y) && FMath::IsNearlyZero(PushA.Z));
	TestTrue(TEXT("B is pushed to +X, equally"), PushB.X > 0.0 && FMath::IsNearlyEqual(static_cast<float>(PushA.X), -static_cast<float>(PushB.X), 1.0e-4f));
	// Pen = 34 + 34 + 2 - 50 = 20; x strength 0.5 x share 0.5 = 5.
	TestTrue(TEXT("Magnitude = overlap x strength x share"), FMath::IsNearlyEqual(static_cast<float>(PushA.Size()), 5.0f, 1.0e-3f));
	TestTrue(TEXT("The push is capped"), FMDPawnSeparation::ComputePush(FVector(0, 0, 0), 34.0f, FVector(10, 0, 0), 34.0f, 2.0f, 1.0f, 1.0f, 3.0f, 1.0f).Size() <= 3.0001f);
	TestTrue(TEXT("A zero share means no push"), FMDPawnSeparation::ComputePush(FVector(0, 0, 0), 34.0f, FVector(10, 0, 0), 34.0f, 2.0f, 1.0f, 1.0f, 50.0f, 0.0f).IsNearlyZero());
	TestFalse(TEXT("Exactly stacked still separates"), FMDPawnSeparation::ComputePush(FVector::ZeroVector, 34.0f, FVector::ZeroVector, 34.0f, 2.0f, 1.0f, 0.5f, 100.0f, 0.5f).IsNearlyZero());

	// Who gives way: an enemy steps out of a player's way, the player itself is not pushed; the shares of a pair add up to 1.
	TestEqual(TEXT("Enemy gives way to a player"), UMDPawnSeparationSubsystem::PushShare(false, true), 1.0f);
	TestEqual(TEXT("A player is not pushed by an enemy"), UMDPawnSeparationSubsystem::PushShare(true, false), 0.0f);
	TestEqual(TEXT("Players share"), UMDPawnSeparationSubsystem::PushShare(true, true), 0.5f);
	TestEqual(TEXT("Enemies share"), UMDPawnSeparationSubsystem::PushShare(false, false), 0.5f);

	// Simulated: an enemy dropped inside a player's capsule eases out within half a second at 10 / s, and the player does not move.
	FVector Player(0, 0, 0);
	FVector Enemy(20, 0, 0);
	const float Dt = 1.0f / 60.0f;
	float LastPen = FMDPawnSeparation::Penetration(Player, 34.0f, Enemy, 34.0f);
	bool bMonotonic = true;
	for (int32 i = 0; i < 30; ++i)
	{
		const FVector PA = FMDPawnSeparation::ComputePush(Player, 34.0f, Enemy, 34.0f, 2.0f, 1.0f, 10.0f * Dt, 350.0f * Dt, UMDPawnSeparationSubsystem::PushShare(true, false));
		const FVector PB = FMDPawnSeparation::ComputePush(Enemy, 34.0f, Player, 34.0f, 2.0f, 1.0f, 10.0f * Dt, 350.0f * Dt, UMDPawnSeparationSubsystem::PushShare(false, true));
		Player += PA;
		Enemy += PB;
		const float Pen = FMDPawnSeparation::Penetration(Player, 34.0f, Enemy, 34.0f);
		bMonotonic &= Pen <= LastPen + 1.0e-4f;
		LastPen = Pen;
	}
	TestTrue(TEXT("The overlap only ever shrinks"), bMonotonic);
	TestTrue(TEXT("Bodies no longer interpenetrate after half a second"), LastPen <= 1.0f);
	TestTrue(TEXT("The player stayed where it was"), Player.IsNearlyZero());
	return true;
}

// ---- Autonomous soft lock ----

namespace MDLockOnSoftTestLocal
{
	const FVector2D View(1920, 1080);

	// A candidate as the component builds it: view test and centre offset from a projected pixel.
	FMDSoftCandidate Make(int32 Id, float DistCm, const FVector2D& Pixel, bool bProjected = true, float InputAngle = -1.0f, float Margin = 0.10f)
	{
		FMDSoftCandidate C;
		C.Id = Id;
		C.DistanceCm = DistCm;
		C.CentreOffset = FMDLockOnRules::CentreOffset(Pixel, View);
		C.bInView = FMDLockOnRules::IsInView(Pixel, bProjected, View, Margin);
		C.InputAngleDeg = InputAngle;
		return C;
	}
	const FVector2D Centre(960, 540);
}

bool FMDLockOnSoftAcquireTest::RunTest(const FString& Parameters)
{
	using namespace MDLockOnSoftTestLocal;
	const FMDLockOnTuning T;
	TestEqual(TEXT("Soft range defaults to 12 m"), T.SoftRange, 1200.0f);
	TestEqual(TEXT("Soft break range defaults to 15 m"), T.SoftBreakRange, 1500.0f);

	// In-view test: widened by 10 % of the viewport on every side; behind the camera (not projected) never.
	TestTrue(TEXT("Screen centre"), FMDLockOnRules::IsInView(Centre, true, View, 0.10f));
	TestTrue(TEXT("Just past the left edge, inside the margin"), FMDLockOnRules::IsInView(FVector2D(-150, 540), true, View, 0.10f));
	TestFalse(TEXT("Well off the left edge"), FMDLockOnRules::IsInView(FVector2D(-300, 540), true, View, 0.10f));
	TestTrue(TEXT("Past the right edge, inside the margin"), FMDLockOnRules::IsInView(FVector2D(2050, 540), true, View, 0.10f));
	TestFalse(TEXT("Past the bottom margin"), FMDLockOnRules::IsInView(FVector2D(960, 1300), true, View, 0.10f));
	TestFalse(TEXT("Behind the camera"), FMDLockOnRules::IsInView(Centre, false, View, 0.10f));
	TestTrue(TEXT("Centre offset 0 at the centre"), FMath::IsNearlyZero(FMDLockOnRules::CentreOffset(Centre, View)));
	TestTrue(TEXT("Centre offset 1 at the right edge"), FMath::IsNearlyEqual(FMDLockOnRules::CentreOffset(FVector2D(1920, 540), View), 1.0f, 1.0e-3f));

	// Nearest in view wins; off-screen, behind and out-of-range candidates are ignored even when they are closer.
	TArray<FMDSoftCandidate> C;
	C.Add(Make(0, 800.0f, Centre));                           // in view, 8 m
	C.Add(Make(1, 500.0f, FVector2D(1500, 540)));             // in view, 5 m, right of centre: the nearest in view
	C.Add(Make(2, 300.0f, Centre, false));                    // 3 m but behind the camera
	C.Add(Make(3, 400.0f, FVector2D(3000, 540)));             // 4 m but far off-screen
	C.Add(Make(4, 1300.0f, Centre));                          // in view but beyond 12 m
	TestEqual(TEXT("Nearest in view"), FMDLockOnRules::PickSoftBest(C, T, false), 1);

	// Ties: same distance and offset -> lower id; same score but nearer wins.
	C.Reset();
	C.Add(Make(5, 600.0f, Centre));
	C.Add(Make(3, 600.0f, Centre));
	TestEqual(TEXT("Exact tie: lower id"), FMDLockOnRules::PickSoftBest(C, T, false), 3);

	// Distance dominates centring: a 4 m target at the screen edge beats a 10 m one dead centre.
	C.Reset();
	C.Add(Make(0, 1000.0f, Centre));
	C.Add(Make(1, 400.0f, FVector2D(1850, 540)));
	TestEqual(TEXT("Distance is dominant"), FMDLockOnRules::PickSoftBest(C, T, false), 1);

	// Nothing qualifies: empty, all off-screen, all too far.
	TestEqual(TEXT("No candidates"), FMDLockOnRules::PickSoftBest(TArray<FMDSoftCandidate>(), T, false), (int32)INDEX_NONE);
	C.Reset();
	C.Add(Make(0, 300.0f, FVector2D(-900, 540)));
	C.Add(Make(1, 1201.0f, Centre));
	TestEqual(TEXT("Only off-screen / out-of-range"), FMDLockOnRules::PickSoftBest(C, T, false), (int32)INDEX_NONE);
	return true;
}

bool FMDLockOnSoftInputTest::RunTest(const FString& Parameters)
{
	using namespace MDLockOnSoftTestLocal;
	const FMDLockOnTuning T;
	// A: closer (5 m) but behind the move input (150 deg). B: 7 m, straight ahead of the input (10 deg).
	TArray<FMDSoftCandidate> C;
	C.Add(Make(0, 500.0f, Centre, true, 150.0f));
	C.Add(Make(1, 700.0f, Centre, true, 10.0f));
	TestEqual(TEXT("No input: the closer one"), FMDLockOnRules::PickSoftBest(C, T, false), 0);
	TestEqual(TEXT("Moving toward B: the input wins the close call"), FMDLockOnRules::PickSoftBest(C, T, true), 1);

	// The input never drags a pick beyond the range or off screen.
	C.Reset();
	C.Add(Make(0, 500.0f, Centre, true, 150.0f));
	C.Add(Make(1, 1300.0f, Centre, true, 0.0f));
	C.Add(Make(2, 300.0f, Centre, false, 0.0f));
	TestEqual(TEXT("Input cannot pick out of range or behind"), FMDLockOnRules::PickSoftBest(C, T, true), 0);

	// A much closer enemy still beats a slightly better aligned far one (distance dominates).
	C.Reset();
	C.Add(Make(0, 150.0f, Centre, true, 120.0f));
	C.Add(Make(1, 1150.0f, Centre, true, 30.0f));
	TestEqual(TEXT("Distance still dominates"), FMDLockOnRules::PickSoftBest(C, T, true), 0);

	// Retarget at attack start.
	FMDSoftRetargetInput In;
	TestFalse(TEXT("Healthy target stays"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.bTargetValid = false;
	TestTrue(TEXT("Dead / invalid target retargets"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In = FMDSoftRetargetInput();
	In.bInView = false;
	TestTrue(TEXT("Out of view retargets"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In = FMDSoftRetargetInput();
	In.DistanceCm = 1500.0f;
	TestFalse(TEXT("Exactly 15 m stays"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.DistanceCm = 1501.0f;
	TestTrue(TEXT("Beyond 15 m retargets"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In = FMDSoftRetargetInput();
	In.bHasMoveInput = true;
	In.TargetInputAngleDeg = 90.0f;
	TestFalse(TEXT("Input 90 deg away but nobody else in the input cone: stay"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.bOtherInInputCone = true;
	TestTrue(TEXT("Input 90 deg away and another target in the cone: retarget"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.TargetInputAngleDeg = 60.0f;
	TestFalse(TEXT("Exactly 60 deg: stay"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.TargetInputAngleDeg = 30.0f;
	TestFalse(TEXT("Input toward the target: stay"), FMDLockOnRules::ShouldSoftRetarget(In, T));
	In.TargetInputAngleDeg = 120.0f;
	In.bHasMoveInput = false;
	TestFalse(TEXT("No input: never input-retargets"), FMDLockOnRules::ShouldSoftRetarget(In, T));

	// The input cone: another in-view candidate within 45 deg of the input, never the current target itself.
	C.Reset();
	C.Add(Make(0, 500.0f, Centre, true, 120.0f)); // current
	C.Add(Make(1, 800.0f, Centre, true, 46.0f));
	TestFalse(TEXT("46 deg is outside the cone"), FMDLockOnRules::HasCandidateInInputCone(C, 0, T));
	C.Add(Make(2, 900.0f, Centre, true, 45.0f));
	TestTrue(TEXT("45 deg is inside the cone"), FMDLockOnRules::HasCandidateInInputCone(C, 0, T));
	C.Reset();
	C.Add(Make(0, 500.0f, Centre, true, 10.0f));
	TestFalse(TEXT("Only the current target is in the cone"), FMDLockOnRules::HasCandidateInInputCone(C, 0, T));
	return true;
}

bool FMDLockOnSoftChainTest::RunTest(const FString& Parameters)
{
	using namespace MDLockOnSoftTestLocal;
	const FMDLockOnTuning T;
	// Three enemies in view plus one behind the player. Each kill immediately yields the next best, with no attack-window requirement
	// (the picker takes no time input at all).
	TArray<FMDSoftCandidate> C;
	C.Add(Make(0, 900.0f, Centre));
	C.Add(Make(1, 400.0f, Centre));
	C.Add(Make(2, 650.0f, Centre));
	C.Add(Make(3, 200.0f, Centre, false)); // behind
	TArray<int32> Order;
	for (int32 Guard = 0; Guard < 6; ++Guard)
	{
		const int32 Pick = FMDLockOnRules::PickSoftBest(C, T, false);
		if (Pick == INDEX_NONE)
		{
			break;
		}
		Order.Add(Pick);
		C.RemoveAll([Pick](const FMDSoftCandidate& X) { return X.Id == Pick; }); // the kill
	}
	TestEqual(TEXT("Chained through all three in-view enemies"), Order.Num(), 3);
	if (Order.Num() == 3)
	{
		TestTrue(TEXT("Closest first, then the next closest, then the last"), Order[0] == 1 && Order[1] == 2 && Order[2] == 0);
	}
	TestEqual(TEXT("Only the one behind the camera is left: chain ends"), FMDLockOnRules::PickSoftBest(C, T, false), (int32)INDEX_NONE);

	// The chain keeps steering by input too: after the kill the input-aligned enemy is next.
	C.Reset();
	C.Add(Make(0, 600.0f, Centre, true, 160.0f));
	C.Add(Make(1, 750.0f, Centre, true, 5.0f));
	TestEqual(TEXT("Chain after a kill honours the move input"), FMDLockOnRules::PickSoftBest(C, T, true), 1);
	return true;
}

bool FMDLockOnSoftTimeoutTest::RunTest(const FString& Parameters)
{
	FMDLockOnTuning T;
	TestEqual(TEXT("Default timeout is 4 s"), T.SoftTimeout, 4.0f);
	TestFalse(TEXT("Just attacked"), FMDLockOnRules::IsSoftExpired(0.0f, T));
	TestFalse(TEXT("3.9 s since the last attack"), FMDLockOnRules::IsSoftExpired(3.9f, T));
	TestFalse(TEXT("Exactly 4 s still holds"), FMDLockOnRules::IsSoftExpired(4.0f, T));
	TestTrue(TEXT("4.01 s drops"), FMDLockOnRules::IsSoftExpired(4.01f, T));
	T.SoftTimeout = 2.0f;
	TestTrue(TEXT("The timeout is tunable (md.LockOn.SoftTimeout)"), FMDLockOnRules::IsSoftExpired(2.5f, T));
	TestFalse(TEXT("...and an attack inside it keeps the lock"), FMDLockOnRules::IsSoftExpired(1.0f, T));
	return true;
}

bool FMDLockOnHardOverridesSoftTest::RunTest(const FString& Parameters)
{
	// An attack soft-locks only with the option on and no hard lock.
	TestTrue(TEXT("Soft on, no hard lock: attacks soft-lock"), FMDLockOnRules::ShouldSoftAcquire(true, false));
	TestFalse(TEXT("A hard lock overrides: attacks leave it alone"), FMDLockOnRules::ShouldSoftAcquire(true, true));
	TestFalse(TEXT("Option off: no soft lock"), FMDLockOnRules::ShouldSoftAcquire(false, false));
	TestFalse(TEXT("Option off and hard locked"), FMDLockOnRules::ShouldSoftAcquire(false, true));

	// The warp feed treats both locks the same (the target comes from GetLockTarget); snapping to a locked target needs no move input.
	TestTrue(TEXT("Locked and standing: snap to the target"), FMDLockOnRules::ShouldSnapToTarget(true, false));
	TestFalse(TEXT("Moving: input facing wins"), FMDLockOnRules::ShouldSnapToTarget(true, true));

	// The component: no lock means neither mode; a fresh component is not hard locked.
	UMDLockOnComponent* Lock = NewObject<UMDLockOnComponent>(GetTransientPackage());
	TestNotNull(TEXT("Component created"), Lock);
	if (Lock)
	{
		TestFalse(TEXT("Not locked: not hard"), Lock->IsHardLock());
		TestFalse(TEXT("Not locked: not soft"), Lock->IsSoftLock());
		TestFalse(TEXT("Not locked: no target"), Lock->HasLockTarget());
	}
	return true;
}

bool FMDLockOnSoftNoCameraTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	// Only a hard lock drives the camera assist (the component gates UpdateCameraAssist on this).
	TestFalse(TEXT("Soft lock: no camera assist"), FMDLockOnRules::ShouldAssistCamera(false));
	TestTrue(TEXT("Hard lock: camera assist"), FMDLockOnRules::ShouldAssistCamera(true));
	// The assist maths itself is unchanged for the hard lock.
	TestTrue(TEXT("Hard assist still turns toward the target"), FMDLockOnRules::AssistStep(10.0f, 0.6f, 5.0f, 0.016f, T) > 0.0f);
	return true;
}

bool FMDLockOnSoftServerTest::RunTest(const FString& Parameters)
{
	const FMDLockOnTuning T;
	TestTrue(TEXT("Soft: 15 m + 3 m tolerance (18 m)"), FMDLockOnRules::ValidateServerTarget(true, true, 1800.0f, 0.0f, T, true));
	TestFalse(TEXT("Soft: beyond 18 m"), FMDLockOnRules::ValidateServerTarget(true, true, 1801.0f, 0.0f, T, true));
	TestTrue(TEXT("Hard: the same 18.01 m is fine (30 m range)"), FMDLockOnRules::ValidateServerTarget(true, true, 1801.0f, 0.0f, T, false));
	TestFalse(TEXT("Soft: dead target"), FMDLockOnRules::ValidateServerTarget(false, true, 500.0f, 0.0f, T, true));
	TestFalse(TEXT("Soft: not seen for 2.5 s"), FMDLockOnRules::ValidateServerTarget(true, true, 500.0f, 2.5f, T, true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
