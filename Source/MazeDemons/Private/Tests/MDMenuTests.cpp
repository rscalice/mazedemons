#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDGameMenuWidget.h"
#include "MDRunStats.h"
#include "MDUITypes.h"

#include "Misc/AutomationTest.h"

namespace MDMenuTestsLocal
{
	// TestEqual on a scoped enum has no string form; compare the underlying value.
	static int32 TabValue(EMDMenuTab Tab)
	{
		return static_cast<int32>(Tab);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMenuTabNavigationTest, "MazeDemons.Menu.TabNavigation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMenuTabNavigationTest::RunTest(const FString& Parameters)
{
	// Wrap-around forward and back.
	TestEqual(TEXT("Inventory -> Perks"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Inventory, 1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Perks));
	TestEqual(TEXT("Perks -> Run"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Perks, 1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Run));
	TestEqual(TEXT("Run wraps to Inventory"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Run, 1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Inventory));
	TestEqual(TEXT("Inventory wraps back to Run"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Inventory, -1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Run));
	TestEqual(TEXT("Run -> Perks"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Run, -1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Perks));
	TestEqual(TEXT("direction 0 stays"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::Perks, 0)), MDMenuTestsLocal::TabValue(EMDMenuTab::Perks));

	// A full lap returns to the start.
	EMDMenuTab Tab = EMDMenuTab::Perks;
	for (int32 i = 0; i < UMDGameMenuWidget::NumTabs; ++i)
	{
		Tab = UMDGameMenuWidget::StepTab(Tab, 1);
	}
	TestEqual(TEXT("three steps is a full lap"), MDMenuTestsLocal::TabValue(Tab), MDMenuTestsLocal::TabValue(EMDMenuTab::Perks));

	// None never comes out of a step and every real tab is selectable.
	TestFalse(TEXT("None is not selectable"), UMDGameMenuWidget::IsTabSelectable(EMDMenuTab::None));
	TestTrue(TEXT("Inventory selectable"), UMDGameMenuWidget::IsTabSelectable(EMDMenuTab::Inventory));
	TestTrue(TEXT("Perks selectable"), UMDGameMenuWidget::IsTabSelectable(EMDMenuTab::Perks));
	TestTrue(TEXT("Run selectable"), UMDGameMenuWidget::IsTabSelectable(EMDMenuTab::Run));
	TestEqual(TEXT("None steps forward to the first tab"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::None, 1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Inventory));
	TestEqual(TEXT("None steps back to the last tab"), MDMenuTestsLocal::TabValue(UMDGameMenuWidget::StepTab(EMDMenuTab::None, -1)), MDMenuTestsLocal::TabValue(EMDMenuTab::Run));
	for (int32 i = 0; i < 20; ++i)
	{
		Tab = UMDGameMenuWidget::StepTab(Tab, (i % 3 == 0) ? -1 : 1);
		TestNotEqual(TEXT("a step never lands on None"), MDMenuTestsLocal::TabValue(Tab), MDMenuTestsLocal::TabValue(EMDMenuTab::None));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMenuTabBadgesTest, "MazeDemons.Menu.TabBadges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMenuTabBadgesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Perks shows its points"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::Perks, 3), 3);
	TestEqual(TEXT("Perks with no points"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::Perks, 0), 0);
	TestEqual(TEXT("negative points clamp to 0"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::Perks, -2), 0);
	TestEqual(TEXT("Inventory has no badge"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::Inventory, 5), 0);
	TestEqual(TEXT("Run has no badge"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::Run, 5), 0);
	TestEqual(TEXT("None has no badge"), UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab::None, 5), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRunStatsAccumulateTest, "MazeDemons.Run.StatsAccumulate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRunStatsAccumulateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("adds"), FMDPlayerRunStats::AddAmount(10.0f, 2.5f), 12.5f);
	TestEqual(TEXT("negative delta adds nothing"), FMDPlayerRunStats::AddAmount(10.0f, -4.0f), 10.0f);
	TestEqual(TEXT("NaN delta adds nothing"), FMDPlayerRunStats::AddAmount(10.0f, TNumericLimits<float>::QuietNaN()), 10.0f);
	TestEqual(TEXT("infinite delta adds nothing"), FMDPlayerRunStats::AddAmount(10.0f, TNumericLimits<float>::Infinity()), 10.0f);
	TestEqual(TEXT("negative current clamps to 0"), FMDPlayerRunStats::AddAmount(-5.0f, 0.0f), 0.0f);
	TestEqual(TEXT("clamps at the cap"), FMDPlayerRunStats::AddAmount(FMDPlayerRunStats::MaxAmount - 1.0f, 1.0e9f), FMDPlayerRunStats::MaxAmount);

	TestEqual(TEXT("count adds"), FMDPlayerRunStats::AddCount(4, 3), 7);
	TestEqual(TEXT("count ignores negative delta"), FMDPlayerRunStats::AddCount(4, -3), 4);
	TestEqual(TEXT("count negative current clamps to 0"), FMDPlayerRunStats::AddCount(-9, 0), 0);
	TestEqual(TEXT("count clamps at the cap without overflow"), FMDPlayerRunStats::AddCount(FMDPlayerRunStats::MaxCount - 1, MAX_int32), FMDPlayerRunStats::MaxCount);
	TestEqual(TEXT("count at the cap stays"), FMDPlayerRunStats::AddCount(MAX_int32, 5), FMDPlayerRunStats::MaxCount);

	const FMDPlayerRunStats Fresh;
	TestTrue(TEXT("fresh stats are zero"), Fresh == FMDPlayerRunStats() && Fresh.Kills == 0 && Fresh.GoldEarned == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
