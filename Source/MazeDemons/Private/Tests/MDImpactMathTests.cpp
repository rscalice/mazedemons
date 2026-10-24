#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDImpactMath.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDImpactMathTest, "MazeDemons.Combat.ImpactMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDImpactMathTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full scale"), MDImpactMath::VFXScaleForLevel(1.5f, EMDVFXLevel::Full), 1.5f);
	TestEqual(TEXT("Reduced scale"), MDImpactMath::VFXScaleForLevel(1.0f, EMDVFXLevel::Reduced), 0.6f);
	TestEqual(TEXT("Negative scale clamps"), MDImpactMath::VFXScaleForLevel(-2.0f, EMDVFXLevel::Full), 0.0f);
	TestTrue(TEXT("Decals on Full"), MDImpactMath::AllowDecals(EMDVFXLevel::Full));
	TestFalse(TEXT("No decals on Reduced"), MDImpactMath::AllowDecals(EMDVFXLevel::Reduced));

	TestEqual(TEXT("Under cap evicts none"), MDImpactMath::DecalsToEvict(5, 20), 0);
	TestEqual(TEXT("At cap evicts one"), MDImpactMath::DecalsToEvict(20, 20), 1);
	TestEqual(TEXT("Over cap evicts extras"), MDImpactMath::DecalsToEvict(23, 20), 4);
	TestEqual(TEXT("Zero cap evicts all"), MDImpactMath::DecalsToEvict(3, 0), 3);

	TestTrue(TEXT("Normal wins"), MDImpactMath::ImpactDirection(FVector(0, 0, 2), FVector::ZeroVector, FVector(100, 0, 0)).Equals(FVector::UpVector));
	TestTrue(TEXT("Fallback attacker to victim"), MDImpactMath::ImpactDirection(FVector::ZeroVector, FVector::ZeroVector, FVector(100, 0, 0)).Equals(FVector::ForwardVector));
	TestTrue(TEXT("Degenerate is up"), MDImpactMath::ImpactDirection(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector).Equals(FVector::UpVector));

	TestFalse(TEXT("Cooldown not elapsed"), MDImpactMath::CooldownElapsed(1.1, 1.0, 0.2));
	TestTrue(TEXT("Cooldown elapsed"), MDImpactMath::CooldownElapsed(1.3, 1.0, 0.2));
	return true;
}

#endif
