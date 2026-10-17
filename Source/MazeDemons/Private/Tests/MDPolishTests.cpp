
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDUITypes.h"

#include "Misc/AutomationTest.h"

// All pure-rule tests: no world, no actors.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRarityFromByteTest, "MazeDemons.Polish.RarityFromByte", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRarityFromByteTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 Common"), UMDInventoryComponent::RarityFromByte(0), EMDRarity::Common);
	TestEqual(TEXT("1 Uncommon"), UMDInventoryComponent::RarityFromByte(1), EMDRarity::Uncommon);
	TestEqual(TEXT("2 Rare"), UMDInventoryComponent::RarityFromByte(2), EMDRarity::Rare);
	TestEqual(TEXT("3 Epic"), UMDInventoryComponent::RarityFromByte(3), EMDRarity::Epic);
	TestEqual(TEXT("4 Legendary"), UMDInventoryComponent::RarityFromByte(4), EMDRarity::Legendary);
	TestEqual(TEXT("5 out of range -> Common"), UMDInventoryComponent::RarityFromByte(5), EMDRarity::Common);
	TestEqual(TEXT("-1 out of range -> Common"), UMDInventoryComponent::RarityFromByte(-1), EMDRarity::Common);
	TestEqual(TEXT("255 out of range -> Common"), UMDInventoryComponent::RarityFromByte(255), EMDRarity::Common);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSwapDamageReductionTest, "MazeDemons.Polish.SwapDamageReduction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDSwapDamageReductionTest::RunTest(const FString& Parameters)
{
	// 16.91% total, swap a chest piece 8% -> 12%: 1 - 0.8309 * 0.88 / 0.92 = 20.52%.
	TestNearlyEqual(TEXT("swap chest 8 -> 12"), UMDGameplayStatics::SwapDamageReduction(16.91f, 8.0f, 12.0f), 20.52f, 0.01f);
	// Empty slot: old piece 0 -> the new one is simply multiplied in: 1 - 0.8309 * 0.88 = 26.88%.
	TestNearlyEqual(TEXT("empty slot"), UMDGameplayStatics::SwapDamageReduction(16.91f, 0.0f, 12.0f), 26.88f, 0.01f);
	TestNearlyEqual(TEXT("same piece is a no-op"), UMDGameplayStatics::SwapDamageReduction(16.91f, 8.0f, 8.0f), 16.91f, 0.01f);
	TestNearlyEqual(TEXT("removing a piece"), UMDGameplayStatics::SwapDamageReduction(16.91f, 8.0f, 0.0f), 9.69f, 0.05f);
	TestEqual(TEXT("clamped to 90"), UMDGameplayStatics::SwapDamageReduction(89.0f, 0.0f, 50.0f), 90.0f);
	TestEqual(TEXT("clamped to 0"), UMDGameplayStatics::SwapDamageReduction(0.0f, 10.0f, 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDWeaponBonusSumTest, "MazeDemons.Polish.WeaponBonusSum", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDWeaponBonusSumTest::RunTest(const FString& Parameters)
{
	TMap<FName, float> Bonuses;
	TestEqual(TEXT("no weapon"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 10.0f);

	Bonuses.Add(FName("MainHand"), 2.0f); // sword
	TestEqual(TEXT("base 10 + sword 2"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 12.0f);

	Bonuses.Add(FName("OffHand"), 3.0f); // axe
	TestEqual(TEXT("both hands add"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 15.0f);

	Bonuses.Add(FName("TwoHanded"), 5.0f); // claymore
	TestEqual(TEXT("two-handed clears the hands"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 15.0f);

	Bonuses.Remove(FName("TwoHanded"));
	TestEqual(TEXT("unequipping it brings the hands back"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 15.0f);

	Bonuses.Add(FName("MainHand"), -50.0f);
	TestEqual(TEXT("never negative"), UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDConsumableCompareTest, "MazeDemons.Polish.ConsumableCompare", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDConsumableCompareTest::RunTest(const FString& Parameters)
{
	const float After = UMDGameplayStatics::HealthAfterHeal(85.0f, 100.0f, 35.0f);
	TestEqual(TEXT("85/100 + 35 caps at max"), After, 100.0f);
	TestEqual(TEXT("delta +15"), After - 85.0f, 15.0f);
	TestEqual(TEXT("partial heal"), UMDGameplayStatics::HealthAfterHeal(40.0f, 100.0f, 35.0f), 75.0f);
	TestEqual(TEXT("negative heal ignored"), UMDGameplayStatics::HealthAfterHeal(40.0f, 100.0f, -10.0f), 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCooldownFractionTest, "MazeDemons.Polish.CooldownFraction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDCooldownFractionTest::RunTest(const FString& Parameters)
{
	// Used at t=10 with a 4 s cooldown -> ready at t=14.
	TestEqual(TEXT("just used"), UMDInventoryComponent::CooldownFraction(10.0, 14.0, 4.0f), 1.0f);
	TestNearlyEqual(TEXT("half way"), UMDInventoryComponent::CooldownFraction(12.0, 14.0, 4.0f), 0.5f);
	TestEqual(TEXT("exactly ready"), UMDInventoryComponent::CooldownFraction(14.0, 14.0, 4.0f), 0.0f);
	TestEqual(TEXT("after ready"), UMDInventoryComponent::CooldownFraction(20.0, 14.0, 4.0f), 0.0f);
	TestEqual(TEXT("before use (clock behind) clamps to 1"), UMDInventoryComponent::CooldownFraction(5.0, 14.0, 4.0f), 1.0f);
	TestEqual(TEXT("zero duration"), UMDInventoryComponent::CooldownFraction(10.0, 14.0, 0.0f), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
