#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDGameSettings.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDStation.h"
#include "MDStatCalculator.h"
#include "MDUITypes.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Friend of UMDInventoryComponent (see header): reaches the condition / upgrade state and the server *_Impl functions.
struct FMDUpgradeTestAccess
{
	static void SetEquipped(UMDInventoryComponent* C, uint8 Slot, FName Row, int32 Durability = MD_NoDurability)
	{
		FMDEquippedSlot& Entry = C->Equipped.AddDefaulted_GetRef();
		Entry.Slot = Slot;
		Entry.ItemRowName = Row;
		C->SetConditionDurability(Slot, Durability);
	}
	static int32 Condition(const UMDInventoryComponent* C, uint8 Slot) { return C->GetConditionDurability(Slot); }
	static void SetCondition(UMDInventoryComponent* C, uint8 Slot, int32 Durability) { C->SetConditionDurability(Slot, Durability); }
	static void SetLevel(UMDInventoryComponent* C, uint8 Slot, int32 Level) { C->SetSlotUpgradeLevel(Slot, Level); }
	static void SetGold(UMDInventoryComponent* C, int32 Gold) { C->GoldAmount = Gold; }
	static void AddRawStack(UMDInventoryComponent* C, FName Row, int32 Qty)
	{
		FMDItemStack& Stack = C->Items.AddDefaulted_GetRef();
		Stack.ItemRowName = Row;
		Stack.Quantity = Qty;
	}
	static void ArmorWear(UMDInventoryComponent* C) { C->ApplyArmorWear(); }
	static bool Repair(UMDInventoryComponent* C, AActor* Station, uint8 Slot) { return C->Repair_Impl(Station, Slot); }
	static bool Upgrade(UMDInventoryComponent* C, AActor* Station, uint8 Slot) { return C->Upgrade_Impl(Station, Slot); }
	static void ServerEquip(UMDInventoryComponent* C, int32 Index) { C->EquipItem_Impl(Index); }
	static void ServerUnequip(UMDInventoryComponent* C, uint8 Slot) { C->UnequipSlot_Impl(Slot); }
	static uint8 SlotOf(const UMDInventoryComponent* C, FName Row) { return C->GetSlotForItem(Row); }
	static int32 MaxStackOf(const UMDInventoryComponent* C, FName Row) { return C->GetMaxStackForItem(Row); }
};

namespace
{
	const FName TestChest(TEXT("TestChest"));
	const FName TestShield(TEXT("TestShield"));
	const FName TestSword(TEXT("TestSword"));
	const FName TestClaymore(TEXT("TestClaymore"));
	const FName TestAxe(TEXT("TestAxe"));
	const FName ShardRow(TEXT("UpgradeShard"));

	FMDItemStatsRow MakeStats(int32 Value, int32 MaxDurability, bool bUpgradeable)
	{
		FMDItemStatsRow Row;
		Row.ItemValue = Value;
		Row.MaxDurability = MaxDurability;
		Row.bUpgradeable = bUpgradeable;
		return Row;
	}

	// Transient world + authoritative owners with inventories + a DT_ItemStats stand-in. No Blueprint asset is required
	// unless a test asks for DT_Items.
	struct FUpgradeFixture
	{
		UWorld* World = nullptr;
		UDataTable* Stats = nullptr;
		UDataTable* ItemTable = nullptr;
		TArray<UMDInventoryComponent*> Invs;
		TArray<AActor*> Owners;

		bool Init(FAutomationTestBase& Test, int32 NumPlayers = 1, bool bLoadItemTable = false)
		{
			if (bLoadItemTable)
			{
				ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
				if (!ItemTable)
				{
					Test.AddError(TEXT("Could not load /Game/Blueprints/Items/DT_Items"));
					return false;
				}
			}
			Stats = NewObject<UDataTable>(GetTransientPackage());
			Stats->RowStruct = FMDItemStatsRow::StaticStruct();
			Stats->AddToRoot();
			Stats->AddRow(TestChest, MakeStats(40, 100, true));
			Stats->AddRow(TestShield, MakeStats(30, 100, false));
			Stats->AddRow(TestSword, MakeStats(40, 100, true));
			Stats->AddRow(TestClaymore, MakeStats(60, 100, true));
			Stats->AddRow(TestAxe, MakeStats(40, 100, true));

			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDUpgradeTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			for (int32 i = 0; i < NumPlayers; ++i)
			{
				AActor* Owner = World->SpawnActor<AActor>();
				if (!Owner)
				{
					Test.AddError(TEXT("Could not spawn owner actor"));
					return false;
				}
				Owner->SetRole(ROLE_Authority);
				UMDInventoryComponent* Inv = NewObject<UMDInventoryComponent>(Owner);
				Inv->ItemTable = ItemTable;
				Inv->ItemStatsTable = TSoftObjectPtr<UDataTable>(Stats);
				Inv->RegisterComponent();
				Owners.Add(Owner);
				Invs.Add(Inv);
			}
			return true;
		}

		UMDInventoryComponent* Inv(int32 Index = 0) const { return Invs[Index]; }

		~FUpgradeFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (World->IsRooted())
				{
					World->RemoveFromRoot();
				}
				World = nullptr;
			}
			if (Stats)
			{
				Stats->RemoveFromRoot();
			}
		}
	};
}

// ---- Pure rules ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDArmorDRChainTest, "MazeDemons.Upgrade.ArmorDRChain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDArmorDRChainTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;

	// Cloth set 4 / 8 / 4 / 2 (helmet, chest, legs, boots) at full condition: 1 - .96 * .92 * .96 * .98 = 16.91%.
	TestNearlyEqual(TEXT("cloth set"), UMDStatCalculator::CombineDR(0.0f, { 4.0f, 8.0f, 4.0f, 2.0f }, 90.0f), 16.91f, 0.01f);

	// Every piece is +3 percentage points per level.
	TestNearlyEqual(TEXT("L0 piece"), UMDStatCalculator::EffectivePieceDR(8.0f, 0, 1.0f, Tuning), 8.0f, 0.001f);
	TestNearlyEqual(TEXT("L1 piece +3"), UMDStatCalculator::EffectivePieceDR(8.0f, 1, 1.0f, Tuning), 11.0f, 0.001f);
	TestNearlyEqual(TEXT("L3 piece +9"), UMDStatCalculator::EffectivePieceDR(8.0f, 3, 1.0f, Tuning), 17.0f, 0.001f);
	TestNearlyEqual(TEXT("condition scales the whole piece"), UMDStatCalculator::EffectivePieceDR(8.0f, 0, 0.5f, Tuning), 4.0f, 0.001f);

	// L3 on the four cloth pieces: 13 / 17 / 13 / 11 -> 44.09%. With a gloves slot (row 0) also at L3 (9): 49.12%.
	const float L3Chest = UMDStatCalculator::EffectivePieceDR(8.0f, 3, 1.0f, Tuning);
	const float L3Helmet = UMDStatCalculator::EffectivePieceDR(4.0f, 3, 1.0f, Tuning);
	const float L3Legs = UMDStatCalculator::EffectivePieceDR(4.0f, 3, 1.0f, Tuning);
	const float L3Boots = UMDStatCalculator::EffectivePieceDR(2.0f, 3, 1.0f, Tuning);
	const float L3Gloves = UMDStatCalculator::EffectivePieceDR(0.0f, 3, 1.0f, Tuning);
	TestNearlyEqual(TEXT("four pieces at L3"), UMDStatCalculator::CombineDR(0.0f, { L3Helmet, L3Chest, L3Legs, L3Boots }, 90.0f), 44.09f, 0.01f);
	TestNearlyEqual(TEXT("five pieces at L3"), UMDStatCalculator::CombineDR(0.0f, { L3Helmet, L3Chest, L3Legs, L3Boots, L3Gloves }, 90.0f), 49.12f, 0.01f);

	// Base defense counts as another multiplicative layer.
	TestNearlyEqual(TEXT("base defense"), UMDStatCalculator::CombineDR(10.0f, { 10.0f }, 90.0f), 19.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDRCap65Test, "MazeDemons.Upgrade.DRCap65", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDRCap65Test::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	TestEqual(TEXT("default player cap is 65"), Tuning.PlayerDRCap, 65.0f);

	const TArray<float> Heavy = { 50.0f, 50.0f, 50.0f }; // 1 - .5^3 = 87.5%
	TestEqual(TEXT("player cap clamps"), UMDStatCalculator::CombineDR(0.0f, Heavy, Tuning.PlayerDRCap), 65.0f);
	TestNearlyEqual(TEXT("dummy / enemy cap 90 does not"), UMDStatCalculator::CombineDR(0.0f, Heavy, 90.0f), 87.5f, 0.001f);
	TestEqual(TEXT("90 still clamps"), UMDStatCalculator::CombineDR(0.0f, { 80.0f, 80.0f }, 90.0f), 90.0f);
	TestEqual(TEXT("never negative"), UMDStatCalculator::CombineDR(0.0f, { -20.0f }, 65.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHitDecrementsTest, "MazeDemons.Durability.HitDecrements", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDHitDecrementsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("100 -> 99"), UMDStatCalculator::DurabilityAfterHit(100), 99);
	TestEqual(TEXT("1 -> 0"), UMDStatCalculator::DurabilityAfterHit(1), 0);
	TestEqual(TEXT("0 stays 0"), UMDStatCalculator::DurabilityAfterHit(0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDEffectivenessCurveTest, "MazeDemons.Durability.EffectivenessCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDEffectivenessCurveTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	TestEqual(TEXT("full"), UMDStatCalculator::ConditionMultiplier(100, 100, Tuning), 1.0f);
	TestEqual(TEXT("25% is still full strength"), UMDStatCalculator::ConditionMultiplier(25, 100, Tuning), 1.0f);
	TestNearlyEqual(TEXT("12.5% -> 0.75"), UMDStatCalculator::ConditionMultiplier(25, 200, Tuning), 0.75f, 0.0001f);
	TestNearlyEqual(TEXT("0% -> floor 0.5"), UMDStatCalculator::ConditionMultiplier(0, 100, Tuning), 0.5f, 0.0001f);
	TestEqual(TEXT("no durability -> 1"), UMDStatCalculator::ConditionMultiplier(0, 0, Tuning), 1.0f);
	TestEqual(TEXT("no durability ignores the stored value"), UMDStatCalculator::ConditionMultiplier(50, 0, Tuning), 1.0f);
	TestEqual(TEXT("over max is clamped to full"), UMDStatCalculator::ConditionMultiplier(150, 100, Tuning), 1.0f);

	// Monotonic below the threshold.
	float Previous = 0.0f;
	for (int32 Durability = 0; Durability <= 25; ++Durability)
	{
		const float Mult = UMDStatCalculator::ConditionMultiplier(Durability, 100, Tuning);
		TestTrue(*FString::Printf(TEXT("curve rises at %d"), Durability), Mult >= Previous);
		Previous = Mult;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDeathPenaltyTest, "MazeDemons.Durability.DeathPenalty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDeathPenaltyTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	TestEqual(TEXT("100 -> 90"), UMDStatCalculator::DurabilityAfterDeath(100, 100, Tuning), 90);
	TestEqual(TEXT("95 -> 85"), UMDStatCalculator::DurabilityAfterDeath(95, 100, Tuning), 85);
	TestEqual(TEXT("5 -> 0"), UMDStatCalculator::DurabilityAfterDeath(5, 100, Tuning), 0);
	TestEqual(TEXT("0 stays 0"), UMDStatCalculator::DurabilityAfterDeath(0, 100, Tuning), 0);
	TestEqual(TEXT("loss is a share of max: 200 max loses 20"), UMDStatCalculator::DurabilityAfterDeath(200, 200, Tuning), 180);
	TestEqual(TEXT("items without durability are unchanged"), UMDStatCalculator::DurabilityAfterDeath(MD_NoDurability, 0, Tuning), static_cast<int32>(MD_NoDurability));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRepairCostTest, "MazeDemons.Repair.CostFormula", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRepairCostTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	TestEqual(TEXT("38 missing, value 40, L0"), UMDStatCalculator::RepairCost(38, 40, 0, Tuning), 16);
	TestEqual(TEXT("38 missing, value 40, L2"), UMDStatCalculator::RepairCost(38, 40, 2, Tuning), 23);
	TestEqual(TEXT("exact product is not rounded up (50 * 40 * 1%)"), UMDStatCalculator::RepairCost(50, 40, 0, Tuning), 20);
	TestEqual(TEXT("full repair of a 40 value sword"), UMDStatCalculator::RepairCost(100, 40, 0, Tuning), 40);
	TestEqual(TEXT("full repair at L3 (+75%)"), UMDStatCalculator::RepairCost(100, 40, 3, Tuning), 70);
	TestEqual(TEXT("one point is still 1 gold"), UMDStatCalculator::RepairCost(1, 1, 0, Tuning), 1);
	TestEqual(TEXT("nothing missing is free"), UMDStatCalculator::RepairCost(0, 40, 0, Tuning), 0);
	TestEqual(TEXT("negative missing is free"), UMDStatCalculator::RepairCost(-5, 40, 0, Tuning), 0);
	TestEqual(TEXT("no value is free"), UMDStatCalculator::RepairCost(50, 0, 0, Tuning), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDUpgradeCostTest, "MazeDemons.Upgrade.CostAndShards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDUpgradeCostTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	int32 Gold = -1;
	int32 Shards = -1;
	TestTrue(TEXT("L0 -> L1"), UMDStatCalculator::GetUpgradeCost(0, Tuning, Gold, Shards));
	TestEqual(TEXT("L0 gold"), Gold, 50);
	TestEqual(TEXT("L0 shards"), Shards, 1);
	TestTrue(TEXT("L1 -> L2"), UMDStatCalculator::GetUpgradeCost(1, Tuning, Gold, Shards));
	TestEqual(TEXT("L1 gold"), Gold, 100);
	TestEqual(TEXT("L1 shards"), Shards, 2);
	TestTrue(TEXT("L2 -> L3"), UMDStatCalculator::GetUpgradeCost(2, Tuning, Gold, Shards));
	TestEqual(TEXT("L2 gold"), Gold, 200);
	TestEqual(TEXT("L2 shards"), Shards, 3);
	TestFalse(TEXT("L3 is the cap"), UMDStatCalculator::GetUpgradeCost(3, Tuning, Gold, Shards));
	TestEqual(TEXT("cap reports no cost"), Gold + Shards, 0);

	FMDEconomyTuning Short;
	Short.MaxUpgradeLevel = 2;
	TestFalse(TEXT("tuning can lower the cap"), UMDStatCalculator::GetUpgradeCost(2, Short, Gold, Shards));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDurabilityColorTest, "MazeDemons.UI.DurabilityColor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDurabilityColorTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("60%"), UMDGameplayStatics::DurabilityColor(0.6f), EMDColor::Success);
	TestEqual(TEXT("50%"), UMDGameplayStatics::DurabilityColor(0.5f), EMDColor::Success);
	TestEqual(TEXT("30%"), UMDGameplayStatics::DurabilityColor(0.3f), EMDColor::Warning);
	TestEqual(TEXT("25%"), UMDGameplayStatics::DurabilityColor(0.25f), EMDColor::Warning);
	TestEqual(TEXT("10%"), UMDGameplayStatics::DurabilityColor(0.1f), EMDColor::Danger);
	TestEqual(TEXT("0%"), UMDGameplayStatics::DurabilityColor(0.0f), EMDColor::Danger);
	TestEqual(TEXT("below 0 clamps"), UMDGameplayStatics::DurabilityColor(-1.0f), EMDColor::Danger);
	TestEqual(TEXT("above 1 clamps"), UMDGameplayStatics::DurabilityColor(2.0f), EMDColor::Success);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDWeaponAPTest, "MazeDemons.Upgrade.WeaponAP", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDWeaponAPTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;

	// Base 10 + sword 2, L2 (+12%), condition 0.75: 12 * 1.12 * 0.75.
	TMap<FName, float> Bonuses;
	Bonuses.Add(FName("MainHand"), 2.0f);
	TestNearlyEqual(TEXT("sword L2 at 0.75"), UMDStatCalculator::EffectiveAttackPower(10.0f, Bonuses, 2, 0.75f, Tuning), 10.08f, 0.001f);
	TestNearlyEqual(TEXT("no upgrade, full condition = plain ComputeAttackPower"), UMDStatCalculator::EffectiveAttackPower(10.0f, Bonuses, 0, 1.0f, Tuning),
		UMDAbilitySystemComponent::ComputeAttackPower(10.0f, Bonuses), 0.001f);

	// A two-handed bonus replaces the hands (base 10 + 5), then its own level applies.
	Bonuses.Add(FName("TwoHanded"), 5.0f);
	TestNearlyEqual(TEXT("claymore L1"), UMDStatCalculator::EffectiveAttackPower(10.0f, Bonuses, 1, 1.0f, Tuning), 15.9f, 0.001f);

	// Which slot's level / condition counts: TwoHanded if equipped, else MainHand, else none.
	FUpgradeFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();
	TestEqual(TEXT("no weapon"), static_cast<int32>(Inv->GetPrimaryWeaponSlot()), 255);
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::MainHand, TestSword, 100);
	TestEqual(TEXT("main hand"), static_cast<int32>(Inv->GetPrimaryWeaponSlot()), static_cast<int32>(MDItemType::MainHand));
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::OffHand, TestAxe, 100);
	TestEqual(TEXT("off hand never leads"), static_cast<int32>(Inv->GetPrimaryWeaponSlot()), static_cast<int32>(MDItemType::MainHand));
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::TwoHanded, TestClaymore, 100);
	TestEqual(TEXT("two-handed wins"), static_cast<int32>(Inv->GetPrimaryWeaponSlot()), static_cast<int32>(MDItemType::TwoHanded));

	FMDUpgradeTestAccess::SetLevel(Inv, MDItemType::MainHand, 2);
	FMDUpgradeTestAccess::SetLevel(Inv, MDItemType::TwoHanded, 1);
	TestEqual(TEXT("levels are per slot (main hand)"), Inv->GetSlotUpgradeLevel(MDItemType::MainHand), 2);
	TestEqual(TEXT("levels are per slot (two-handed)"), Inv->GetSlotUpgradeLevel(MDItemType::TwoHanded), 1);
	TestEqual(TEXT("levels are per slot (off hand has none)"), Inv->GetSlotUpgradeLevel(MDItemType::OffHand), 0);
	return true;
}

// ---- Gold ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDGoldPerPlayerTest, "MazeDemons.Gold.PerPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDGoldPerPlayerTest::RunTest(const FString& Parameters)
{
	const float Radius = UMDGameSettings::GetEconomy().KillGoldRadius;

	// Even split, remainder to the killer.
	TArray<int32> Split = UMDStatCalculator::SplitKillGold(10, 3, INDEX_NONE);
	TestTrue(TEXT("10 / 3 with no killer: remainder to index 0"), Split == TArray<int32>({ 4, 3, 3 }));
	Split = UMDStatCalculator::SplitKillGold(10, 3, 2);
	TestTrue(TEXT("remainder to the killer"), Split == TArray<int32>({ 3, 3, 4 }));
	TestTrue(TEXT("no gold"), UMDStatCalculator::SplitKillGold(0, 2, 0) == TArray<int32>({ 0, 0 }));
	TestEqual(TEXT("no recipients"), UMDStatCalculator::SplitKillGold(5, 0, 0).Num(), 0);

	// Both near: 11 gold, killer is player 1 -> 5 / 6.
	TArray<int32> Shares;
	UMDStatCalculator::DistributeKillGold(11, { 500.0f, 1000.0f }, Radius, 1, Shares);
	TestTrue(TEXT("both in range"), Shares == TArray<int32>({ 5, 6 }));

	// Player 0 is 40 m away (checklist item 8): only player 1 is paid, all 10.
	UMDStatCalculator::DistributeKillGold(10, { 4000.0f, 500.0f }, Radius, 1, Shares);
	TestTrue(TEXT("far player gets nothing"), Shares == TArray<int32>({ 0, 10 }));

	// The killer is out of range: the remainder goes to the nearest recipient.
	UMDStatCalculator::DistributeKillGold(7, { 4000.0f, 800.0f, 300.0f }, Radius, 0, Shares);
	TestTrue(TEXT("remainder to the nearest when the killer is out of range"), Shares == TArray<int32>({ 0, 3, 4 }));

	// No pawn (-1) is never a recipient; the radius itself is inclusive; nobody in range pays nobody.
	UMDStatCalculator::DistributeKillGold(10, { -1.0f, Radius }, Radius, INDEX_NONE, Shares);
	TestTrue(TEXT("no pawn / on the radius"), Shares == TArray<int32>({ 0, 10 }));
	UMDStatCalculator::DistributeKillGold(10, { Radius + 1.0f }, Radius, 0, Shares);
	TestTrue(TEXT("nobody in range"), Shares == TArray<int32>({ 0 }));

	// Gold is per player: two inventories receive their own share.
	FUpgradeFixture Fx;
	if (!Fx.Init(*this, 2))
	{
		return false;
	}
	UMDStatCalculator::DistributeKillGold(11, { 500.0f, 1000.0f }, Radius, 1, Shares);
	Fx.Inv(0)->AddGold(Shares[0]);
	Fx.Inv(1)->AddGold(Shares[1]);
	TestEqual(TEXT("player 0 gold"), Fx.Inv(0)->GetGold(), 5);
	TestEqual(TEXT("player 1 gold"), Fx.Inv(1)->GetGold(), 6);
	Fx.Inv(1)->AddGold(10);
	TestEqual(TEXT("player 0 unaffected by player 1"), Fx.Inv(0)->GetGold(), 5);
	return true;
}

// ---- Inventory-level rules ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDUpgradeRejectsTest, "MazeDemons.Upgrade.RejectsInsufficient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDUpgradeRejectsTest::RunTest(const FString& Parameters)
{
	// Range is a pure function of two points.
	TestTrue(TEXT("in range"), AMDStation::IsWithinRange(FVector::ZeroVector, FVector(300.0f, 0.0f, 0.0f), 400.0f));
	TestTrue(TEXT("exactly on the range"), AMDStation::IsWithinRange(FVector::ZeroVector, FVector(400.0f, 0.0f, 0.0f), 400.0f));
	TestFalse(TEXT("out of range"), AMDStation::IsWithinRange(FVector::ZeroVector, FVector(401.0f, 0.0f, 0.0f), 400.0f));

	FUpgradeFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();
	AMDStation* Station = Fx.World->SpawnActor<AMDStation>();
	if (!Station)
	{
		AddError(TEXT("Could not spawn AMDStation"));
		return false;
	}
	const uint8 Chest = MDItemType::Chest;
	int32 Gold = 0;
	int32 Shards = 0;

	TestEqual(TEXT("no station"), Inv->GetUpgradeReject(nullptr, Chest, Gold, Shards), EMDStationReject::NoStation);
	TestEqual(TEXT("not a station"), Inv->GetUpgradeReject(Fx.Owners[0], Chest, Gold, Shards), EMDStationReject::NoStation);
	TestEqual(TEXT("empty slot"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::EmptySlot);
	TestEqual(TEXT("invalid slot"), Inv->GetUpgradeReject(Station, 255, Gold, Shards), EMDStationReject::EmptySlot);

	FMDUpgradeTestAccess::SetEquipped(Inv, Chest, TestChest, 62);
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::Shield, TestShield, 100);
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::OffHand, TestAxe, 100);

	// Gold is checked before shards.
	TestEqual(TEXT("no gold, no shards -> gold first"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::NotEnoughGold);
	TestEqual(TEXT("costs are reported for the UI (gold)"), Gold, 50);
	TestEqual(TEXT("costs are reported for the UI (shards)"), Shards, 1);

	FMDUpgradeTestAccess::SetGold(Inv, 50);
	TestEqual(TEXT("gold but no shards"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::NotEnoughShards);
	TestEqual(TEXT("shard count starts at 0"), Inv->GetShardCount(), 0);

	// Not upgradeable: the shield has no upgrades, nor does the off hand (main weapon only).
	TestEqual(TEXT("shield"), Inv->GetUpgradeReject(Station, MDItemType::Shield, Gold, Shards), EMDStationReject::NotUpgradeable);
	TestEqual(TEXT("off hand"), Inv->GetUpgradeReject(Station, MDItemType::OffHand, Gold, Shards), EMDStationReject::NotUpgradeable);

	FMDUpgradeTestAccess::AddRawStack(Inv, ShardRow, 1);
	TestEqual(TEXT("shard count"), Inv->GetShardCount(), 1);
	TestEqual(TEXT("affordable"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::None);

	// Out of range / wrong service.
	Station->SetActorLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestEqual(TEXT("out of range"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::OutOfRange);
	Station->SetActorLocation(FVector::ZeroVector);
	Station->Services = static_cast<int32>(EMDStationService::Repair);
	TestEqual(TEXT("station without the upgrade service"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::NoStation);
	Station->Services = 7;

	// A successful upgrade spends gold and shards, raises the level and restores the item.
	TestTrue(TEXT("upgrade succeeds"), FMDUpgradeTestAccess::Upgrade(Inv, Station, Chest));
	TestEqual(TEXT("level 1"), Inv->GetSlotUpgradeLevel(Chest), 1);
	TestEqual(TEXT("gold spent"), Inv->GetGold(), 0);
	TestEqual(TEXT("shard spent"), Inv->GetShardCount(), 0);
	TestEqual(TEXT("durability restored"), FMDUpgradeTestAccess::Condition(Inv, Chest), 100);
	TestFalse(TEXT("a second upgrade is refused (no funds)"), FMDUpgradeTestAccess::Upgrade(Inv, Station, Chest));
	TestEqual(TEXT("refusal changes nothing"), Inv->GetSlotUpgradeLevel(Chest), 1);

	FMDUpgradeTestAccess::SetLevel(Inv, Chest, 3);
	FMDUpgradeTestAccess::SetGold(Inv, 1000);
	TestEqual(TEXT("max level"), Inv->GetUpgradeReject(Station, Chest, Gold, Shards), EMDStationReject::MaxLevel);

	// Reject texts exist for every refusal.
	for (const EMDStationReject Reason : { EMDStationReject::NoStation, EMDStationReject::OutOfRange, EMDStationReject::Dead, EMDStationReject::EmptySlot,
		EMDStationReject::NotUpgradeable, EMDStationReject::MaxLevel, EMDStationReject::FullDurability, EMDStationReject::NotEnoughGold, EMDStationReject::NotEnoughShards })
	{
		TestFalse(*FString::Printf(TEXT("text for reason %d"), static_cast<int32>(Reason)), UMDInventoryComponent::GetStationRejectText(Reason, 3, 1).IsEmpty());
	}
	TestTrue(TEXT("None has no text"), UMDInventoryComponent::GetStationRejectText(EMDStationReject::None).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRepairRejectsTest, "MazeDemons.Repair.RejectsAndRepairAll", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRepairRejectsTest::RunTest(const FString& Parameters)
{
	FUpgradeFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();
	AMDStation* Station = Fx.World->SpawnActor<AMDStation>();
	if (!Station)
	{
		AddError(TEXT("Could not spawn AMDStation"));
		return false;
	}
	const uint8 Chest = MDItemType::Chest;
	const uint8 Hand = MDItemType::MainHand;
	int32 Cost = 0;

	TestEqual(TEXT("repair all with nothing equipped"), Inv->GetRepairReject(Station, 255, Cost), EMDStationReject::EmptySlot);
	TestEqual(TEXT("repair an empty slot"), Inv->GetRepairReject(Station, Chest, Cost), EMDStationReject::EmptySlot);

	FMDUpgradeTestAccess::SetEquipped(Inv, Chest, TestChest, 100);
	FMDUpgradeTestAccess::SetEquipped(Inv, Hand, TestSword, 100);
	TestEqual(TEXT("full durability"), Inv->GetRepairReject(Station, Chest, Cost), EMDStationReject::FullDurability);
	TestEqual(TEXT("repair all at full durability"), Inv->GetRepairReject(Station, 255, Cost), EMDStationReject::FullDurability);

	// Chest 62/100 (38 missing, value 40, L0 -> 16) + sword 90/100 (10 missing, value 40 -> 4).
	FMDUpgradeTestAccess::SetCondition(Inv, Chest, 62);
	FMDUpgradeTestAccess::SetCondition(Inv, Hand, 90);
	FMDUpgradeTestAccess::SetGold(Inv, 15);
	TestEqual(TEXT("chest: not enough gold"), Inv->GetRepairReject(Station, Chest, Cost), EMDStationReject::NotEnoughGold);
	TestEqual(TEXT("chest cost"), Cost, 16);
	FMDUpgradeTestAccess::SetGold(Inv, 20);
	TestEqual(TEXT("repair all with 20 gold"), Inv->GetRepairReject(Station, 255, Cost), EMDStationReject::None);
	TestEqual(TEXT("repair all cost 16 + 4"), Cost, 20);

	// Repair All is all-or-nothing: short by one gold repairs nothing, even though the cheaper item alone would be affordable.
	FMDUpgradeTestAccess::SetGold(Inv, 19);
	TestEqual(TEXT("repair all one gold short"), Inv->GetRepairReject(Station, 255, Cost), EMDStationReject::NotEnoughGold);
	TestFalse(TEXT("refused"), FMDUpgradeTestAccess::Repair(Inv, Station, 255));
	TestEqual(TEXT("chest untouched"), FMDUpgradeTestAccess::Condition(Inv, Chest), 62);
	TestEqual(TEXT("gold untouched"), Inv->GetGold(), 19);
	TestEqual(TEXT("sword alone is affordable"), Inv->GetRepairReject(Station, Hand, Cost), EMDStationReject::None);
	TestEqual(TEXT("sword cost"), Cost, 4);

	FMDUpgradeTestAccess::SetGold(Inv, 20);
	TestTrue(TEXT("repair all succeeds"), FMDUpgradeTestAccess::Repair(Inv, Station, 255));
	TestEqual(TEXT("chest repaired"), FMDUpgradeTestAccess::Condition(Inv, Chest), 100);
	TestEqual(TEXT("sword repaired"), FMDUpgradeTestAccess::Condition(Inv, Hand), 100);
	TestEqual(TEXT("gold spent"), Inv->GetGold(), 0);

	// Upgrade level raises the price (+25% per level): 38 missing at L1 = ceil(38 * 40 * 1% * 1.25) = 19.
	FMDUpgradeTestAccess::SetLevel(Inv, Chest, 1);
	FMDUpgradeTestAccess::SetCondition(Inv, Chest, 62);
	Inv->GetRepairReject(Station, Chest, Cost);
	TestEqual(TEXT("upgraded item costs more to repair"), Cost, 19);

	// Out of range.
	Station->SetActorLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestEqual(TEXT("out of range"), Inv->GetRepairReject(Station, Chest, Cost), EMDStationReject::OutOfRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDWearTargetsTest, "MazeDemons.Durability.WearTargets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDWearTargetsTest::RunTest(const FString& Parameters)
{
	FUpgradeFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();
	AActor* Wearer = Fx.Owners[0];
	AActor* Dummy = Fx.World->SpawnActor<AActor>();

	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::Chest, TestChest, 50);
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::Shield, TestShield, 50);
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::MainHand, TestSword, 50);

	// Being hit wears armor and the shield, never the weapon.
	FMDUpgradeTestAccess::ArmorWear(Inv);
	TestEqual(TEXT("chest wears"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Chest), 49);
	TestEqual(TEXT("shield wears"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Shield), 49);
	TestEqual(TEXT("weapon does not wear when hit"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 50);

	// Landing a hit wears the weapon only; self damage does not count.
	UMDInventoryComponent::NotifyHitLanded(Wearer, Dummy);
	TestEqual(TEXT("sword wears on a landed hit"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 49);
	TestEqual(TEXT("armor does not wear when hitting"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Chest), 49);
	UMDInventoryComponent::NotifyHitLanded(Wearer, Wearer);
	TestEqual(TEXT("self damage does not wear the weapon"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 49);
	UMDInventoryComponent::NotifyHitLanded(nullptr, Dummy);
	TestEqual(TEXT("environment damage does not wear the weapon"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 49);

	// The two-handed weapon leads when equipped.
	FMDUpgradeTestAccess::SetEquipped(Inv, MDItemType::TwoHanded, TestClaymore, 50);
	UMDInventoryComponent::NotifyHitLanded(Wearer, Dummy);
	TestEqual(TEXT("claymore wears"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::TwoHanded), 49);
	TestEqual(TEXT("sword rests while the claymore is in use"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 49);

	// Durability stops at 0.
	FMDUpgradeTestAccess::SetCondition(Inv, MDItemType::Chest, 0);
	FMDUpgradeTestAccess::ArmorWear(Inv);
	TestEqual(TEXT("broken stays at 0"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Chest), 0);

	// Death: every equipped item with durability loses 10% of its max.
	Inv->ApplyDeathPenalty();
	TestEqual(TEXT("broken item stays broken"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Chest), 0);
	TestEqual(TEXT("shield -10 (it also wore once more with the chest break above)"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::Shield), 38);
	TestEqual(TEXT("sword -10"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::MainHand), 39);
	TestEqual(TEXT("claymore -10"), FMDUpgradeTestAccess::Condition(Inv, MDItemType::TwoHanded), 39);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDConditionTravelsTest, "MazeDemons.Durability.ConditionTravels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDConditionTravelsTest::RunTest(const FString& Parameters)
{
	FUpgradeFixture Fx;
	if (!Fx.Init(*this, 1, true))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();

	// Any equippable DT_Items row; give it durability via the stats stand-in.
	FName Row = NAME_None;
	for (const FName Candidate : Fx.ItemTable->GetRowNames())
	{
		const uint8 Slot = FMDUpgradeTestAccess::SlotOf(Inv, Candidate);
		if (Slot >= MDItemType::Helmet && Slot <= MDItemType::Boots)
		{
			Row = Candidate;
			break;
		}
	}
	if (Row.IsNone())
	{
		AddWarning(TEXT("No armor row in DT_Items; skipping"));
		return true;
	}
	Fx.Stats->AddRow(Row, MakeStats(20, 100, true));
	const uint8 Slot = FMDUpgradeTestAccess::SlotOf(Inv, Row);

	TestTrue(TEXT("add"), Inv->AddItem(Row, 1));
	if (Inv->GetItems().Num() != 1)
	{
		AddError(TEXT("expected one stack"));
		return false;
	}
	int32 Cur = 0;
	int32 Max = 0;
	Inv->GetItemDurability(0, Cur, Max);
	TestEqual(TEXT("new items are at full durability"), Cur, 100);
	TestEqual(TEXT("max from the stats table"), Max, 100);

	FMDUpgradeTestAccess::ServerEquip(Inv, 0);
	Inv->GetSlotDurability(Slot, Cur, Max);
	TestEqual(TEXT("equipped at 100"), Cur, 100);

	FMDUpgradeTestAccess::SetCondition(Inv, Slot, 37);
	FMDUpgradeTestAccess::ServerUnequip(Inv, Slot);
	if (Inv->GetItems().Num() != 1)
	{
		AddError(TEXT("unequip should return the item to the bag"));
		return false;
	}
	Inv->GetItemDurability(0, Cur, Max);
	TestEqual(TEXT("durability travels back to the bag"), Cur, 37);
	TestEqual(TEXT("the slot no longer tracks condition"), FMDUpgradeTestAccess::Condition(Inv, Slot), static_cast<int32>(MD_NoDurability));

	FMDUpgradeTestAccess::ServerEquip(Inv, 0);
	Inv->GetSlotDurability(Slot, Cur, Max);
	TestEqual(TEXT("durability travels back to the slot"), Cur, 37);

	// Upgrades belong to the slot, not the item.
	FMDUpgradeTestAccess::SetLevel(Inv, Slot, 2);
	FMDUpgradeTestAccess::ServerUnequip(Inv, Slot);
	TestEqual(TEXT("the slot keeps its level without an item"), Inv->GetSlotUpgradeLevel(Slot), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDShardStackTest, "MazeDemons.Items.ShardStack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDShardStackTest::RunTest(const FString& Parameters)
{
	FUpgradeFixture Fx;
	if (!Fx.Init(*this, 1, true))
	{
		return false;
	}
	UMDInventoryComponent* Inv = Fx.Inv();

	// A row with durability never stacks: three units are three stacks of one, each at full durability.
	FName DurableRow = NAME_None;
	FName StackableRow = NAME_None;
	for (const FName Candidate : Fx.ItemTable->GetRowNames())
	{
		const uint8 Slot = FMDUpgradeTestAccess::SlotOf(Inv, Candidate);
		if (DurableRow.IsNone() && Slot < MDItemType::Gold)
		{
			DurableRow = Candidate;
		}
		else if (StackableRow.IsNone() && Slot != MDItemType::Gold && FMDUpgradeTestAccess::MaxStackOf(Inv, Candidate) >= 2)
		{
			StackableRow = Candidate;
		}
	}
	if (DurableRow.IsNone())
	{
		AddWarning(TEXT("No equippable row in DT_Items; skipping the durable-stack check"));
	}
	else
	{
		Fx.Stats->AddRow(DurableRow, MakeStats(40, 100, true));
		TestEqual(TEXT("durable rows have max stack 1"), FMDUpgradeTestAccess::MaxStackOf(Inv, DurableRow), 1);
		TestTrue(TEXT("add 3"), Inv->AddItem(DurableRow, 3));
		TestEqual(TEXT("three stacks"), Inv->GetItems().Num(), 3);
		for (const FMDItemStack& Stack : Inv->GetItems())
		{
			TestEqual(TEXT("one unit per stack"), Stack.Quantity, 1);
			TestEqual(TEXT("full durability"), static_cast<int32>(Stack.Durability), 100);
		}
	}

	// A plain stackable row still stacks and carries no durability.
	if (StackableRow.IsNone())
	{
		AddWarning(TEXT("No stackable row in DT_Items; skipping the stackable check"));
	}
	else if (Fx.Stats->FindRowUnchecked(StackableRow) == nullptr)
	{
		const int32 Before = Inv->GetItems().Num();
		TestTrue(TEXT("add 2 stackable"), Inv->AddItem(StackableRow, 2));
		TestEqual(TEXT("merged into one stack"), Inv->GetItems().Num(), Before + 1);
		TestEqual(TEXT("no durability"), static_cast<int32>(Inv->GetItems().Last().Durability), static_cast<int32>(MD_NoDurability));
	}

	// The shard row (created in the editor) must stack to 20 once it exists.
	if (Fx.ItemTable->FindRowUnchecked(ShardRow))
	{
		TestEqual(TEXT("UpgradeShard max stack"), FMDUpgradeTestAccess::MaxStackOf(Inv, ShardRow), 20);
		TestEqual(TEXT("UpgradeShard is a material"), static_cast<int32>(FMDUpgradeTestAccess::SlotOf(Inv, ShardRow)), static_cast<int32>(MDItemType::Material));
	}
	else
	{
		AddInfo(TEXT("DT_Items has no UpgradeShard row yet (WP2); shard stack size not checked"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
