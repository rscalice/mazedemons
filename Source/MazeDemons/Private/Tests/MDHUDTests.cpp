#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDHUDWidget.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHUDSafePercentTest, "MazeDemons.UI.HUDSafePercent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDHUDSafePercentTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("zero max does not divide by zero"), UMDHUDWidget::SafePercent(50.0f, 0.0f), 0.0f);
	TestEqual(TEXT("negative max is treated as empty"), UMDHUDWidget::SafePercent(50.0f, -10.0f), 0.0f);
	TestEqual(TEXT("half"), UMDHUDWidget::SafePercent(50.0f, 100.0f), 0.5f);
	TestEqual(TEXT("overfull clamps to 1"), UMDHUDWidget::SafePercent(150.0f, 100.0f), 1.0f);
	TestEqual(TEXT("negative value clamps to 0"), UMDHUDWidget::SafePercent(-5.0f, 100.0f), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
