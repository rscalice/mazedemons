// R4 slice 1: the ranged shoulder camera's pure rules.
#include "Misc/AutomationTest.h"

#include "Ranged/MDRangedCameraModifier.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedCameraBlendTest, "MazeDemons.Ranged.Camera.ShoulderBlend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedCameraBlendTest::RunTest(const FString& Parameters)
{
	float A = 0.0f;
	A = FMDRangedCameraRules::StepAlpha(A, 1.0f, 0.1f, 0.2f);
	TestEqual(TEXT("half way after half the blend time"), A, 0.5f);
	A = FMDRangedCameraRules::StepAlpha(A, 1.0f, 0.5f, 0.2f);
	TestEqual(TEXT("never overshoots"), A, 1.0f);
	A = FMDRangedCameraRules::StepAlpha(A, 0.0f, 0.1f, 0.2f);
	TestEqual(TEXT("blends back out"), A, 0.5f);
	TestEqual(TEXT("zero blend snaps"), FMDRangedCameraRules::StepAlpha(0.3f, 1.0f, 0.016f, 0.0f), 1.0f);
	TestEqual(TEXT("ease start"), FMDRangedCameraRules::Ease(0.0f), 0.0f);
	TestEqual(TEXT("ease mid"), FMDRangedCameraRules::Ease(0.5f), 0.5f);
	TestEqual(TEXT("ease end"), FMDRangedCameraRules::Ease(1.0f), 1.0f);
	TestEqual(TEXT("ease clamps"), FMDRangedCameraRules::Ease(2.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedCameraOffsetTest, "MazeDemons.Ranged.Camera.OffsetAndCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedCameraOffsetTest::RunTest(const FString& Parameters)
{
	const FVector Right(0, 1, 0), Up(0, 0, 1);
	const FVector Full = FMDRangedCameraRules::ComputeOffset(1.0f, 1.0f, 50.0f, 10.0f, Right, Up);
	TestTrue(TEXT("right shoulder: +50 right, +10 up"), Full.Equals(FVector(0, 50, 10), 0.01));
	const FVector Left = FMDRangedCameraRules::ComputeOffset(1.0f, -1.0f, 50.0f, 10.0f, Right, Up);
	TestTrue(TEXT("left shoulder mirrors the sideways part only"), Left.Equals(FVector(0, -50, 10), 0.01));
	TestTrue(TEXT("alpha 0 is no offset"), FMDRangedCameraRules::ComputeOffset(0.0f, 1.0f, 50.0f, 10.0f, Right, Up).IsNearlyZero());
	TestTrue(TEXT("side 0 falls back to right"), FMDRangedCameraRules::ComputeOffset(1.0f, 0.0f, 50.0f, 0.0f, Right, Up).Equals(FVector(0, 50, 0), 0.01));

	const FVector Pivot(0, 0, 0), Desired(0, 100, 0);
	TestTrue(TEXT("no hit leaves the spot"), FMDRangedCameraRules::ApplyCollision(Pivot, Desired, false, 1.0f).Equals(Desired));
	const FVector Pulled = FMDRangedCameraRules::ApplyCollision(Pivot, Desired, true, 0.5f);
	TestTrue(TEXT("a hit pulls the camera in, short of the wall"), Pulled.Y > 0.0 && Pulled.Y < 50.0);
	return true;
}
