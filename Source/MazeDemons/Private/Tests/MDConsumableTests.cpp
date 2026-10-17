
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDInventoryComponent.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCanUseHealTest, "MazeDemons.Inventory.Consumable.CanUseHeal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDCanUseHealTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("hurt, alive, ready"), UMDInventoryComponent::CanUseHeal(50.0f, 100.0f, false, 10.0, 5.0));
	TestTrue(TEXT("cooldown exactly elapsed"), UMDInventoryComponent::CanUseHeal(50.0f, 100.0f, false, 5.0, 5.0));
	TestFalse(TEXT("full health is not wasted"), UMDInventoryComponent::CanUseHeal(100.0f, 100.0f, false, 10.0, 5.0));
	TestFalse(TEXT("dead cannot heal"), UMDInventoryComponent::CanUseHeal(0.0f, 100.0f, true, 10.0, 5.0));
	TestFalse(TEXT("cooldown blocks"), UMDInventoryComponent::CanUseHeal(50.0f, 100.0f, false, 4.0, 5.0));
	TestFalse(TEXT("unreplicated zero max blocks"), UMDInventoryComponent::CanUseHeal(0.0f, 0.0f, false, 10.0, 5.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
