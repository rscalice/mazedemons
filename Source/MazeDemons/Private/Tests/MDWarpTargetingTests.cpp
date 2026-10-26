#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDWarpTargeting.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDWarpTargetTest, "MazeDemons.Combat.WarpTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDWarpTargetTest::RunTest(const FString& Parameters)
{
	const FVector O = FVector::ZeroVector;
	const FVector Fwd = FVector::ForwardVector;

	// Range and cone.
	TestTrue(TEXT("In front, in range"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(300, 0, 0), 500.0f, 60.0f));
	TestFalse(TEXT("Out of range"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(600, 0, 0), 500.0f, 60.0f));
	TestFalse(TEXT("Behind"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(-300, 0, 0), 500.0f, 60.0f));
	TestTrue(TEXT("Inside the half angle (45 deg)"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(200, 200, 0), 500.0f, 60.0f));
	TestFalse(TEXT("Outside the half angle (75 deg)"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(100, 373, 0), 500.0f, 60.0f));
	TestTrue(TEXT("Z is ignored"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(300, 0, 900), 500.0f, 60.0f));
	TestTrue(TEXT("On top of the origin"), MDWarpTargeting::IsInRangeAndCone(O, Fwd, FVector(0, 0, 50), 500.0f, 60.0f));
	TestTrue(TEXT("Zero facing skips the cone"), MDWarpTargeting::IsInRangeAndCone(O, FVector::ZeroVector, FVector(-300, 0, 0), 500.0f, 60.0f));

	// Nearest first, filtered.
	TArray<MDWarpTargeting::FCandidate> C;
	C.Add({1, FVector(400, 0, 0)});
	C.Add({2, FVector(150, 50, 0)});
	C.Add({3, FVector(-100, 0, 0)});   // behind
	C.Add({4, FVector(900, 0, 0)});    // too far
	C.Add({5, FVector(150, -50, 0)});  // same distance as 2: lower Id first
	const TArray<int32> Ranked = MDWarpTargeting::RankTargets(O, Fwd, 500.0f, 60.0f, C);
	TestEqual(TEXT("Ranked count"), Ranked.Num(), 3);
	if (Ranked.Num() == 3)
	{
		TestEqual(TEXT("Nearest first"), Ranked[0], 2);
		TestEqual(TEXT("Tie by id"), Ranked[1], 5);
		TestEqual(TEXT("Farthest last"), Ranked[2], 1);
	}
	TestEqual(TEXT("No candidates"), MDWarpTargeting::RankTargets(O, Fwd, 500.0f, 60.0f, TArray<MDWarpTargeting::FCandidate>()).Num(), 0);

	// Warp point stops short by radius + stop distance.
	TestTrue(TEXT("Stops short"), MDWarpTargeting::ComputeWarpPoint(O, FVector(400, 0, 10), 40.0f, 90.0f).Equals(FVector(270, 0, 10)));
	TestTrue(TEXT("Diagonal stops short"), MDWarpTargeting::ComputeWarpPoint(O, FVector(300, 400, 0), 0.0f, 100.0f).Equals(FVector(240, 320, 0), 0.01));
	TestTrue(TEXT("Already close stays put"), MDWarpTargeting::ComputeWarpPoint(FVector(10, 20, 0), FVector(100, 20, 5), 40.0f, 90.0f).Equals(FVector(10, 20, 5)));
	TestTrue(TEXT("Coincident stays put"), MDWarpTargeting::ComputeWarpPoint(O, O, 40.0f, 90.0f).Equals(O));

	// Yaw and reach.
	TestEqual(TEXT("Yaw +X"), MDWarpTargeting::YawDegrees(FVector(1, 0, 0)), 0.0f);
	TestTrue(TEXT("Yaw +Y"), FMath::IsNearlyEqual(MDWarpTargeting::YawDegrees(FVector(0, 5, 3)), 90.0f, 0.001f));
	TestTrue(TEXT("Yaw -X"), FMath::IsNearlyEqual(FMath::Abs(MDWarpTargeting::YawDegrees(FVector(-1, 0, 0))), 180.0f, 0.001f));
	TestEqual(TEXT("Yaw degenerate"), MDWarpTargeting::YawDegrees(FVector(0, 0, 9)), 0.0f);
	TestTrue(TEXT("Server accepts within tolerance"), MDWarpTargeting::IsTargetWithinReach(O, FVector(640, 0, 0), 500.0f, 150.0f));
	TestFalse(TEXT("Server rejects beyond tolerance"), MDWarpTargeting::IsTargetWithinReach(O, FVector(660, 0, 0), 500.0f, 150.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
