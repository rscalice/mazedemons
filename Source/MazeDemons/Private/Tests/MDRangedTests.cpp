// R0 ranged weapons: data rows, slot 9 loadout, mode gating, stances, grip IK alpha and socket validation.
#include "Misc/AutomationTest.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MDCombatStatics.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedWeaponData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MDRangedTestsLocal
{
	// The three guns R0 ships. EmberStaff / GraveBolter come with R1+.
	const TCHAR* const GunRows[] = { TEXT("HollowPistol"), TEXT("CatacombRepeater"), TEXT("DuskrayPistol") };
	const TCHAR* const AmmoRows[] = { TEXT("AmmoLight"), TEXT("AmmoHeavy") };

	UDataTable* LoadTable(const TCHAR* Path)
	{
		return LoadObject<UDataTable>(nullptr, Path);
	}

	UMDRangedWeaponData* LoadGunData(FAutomationTestBase& Test, const TCHAR* Row)
	{
		UDataTable* Stats = LoadTable(TEXT("/Game/Blueprints/Items/DT_ItemStats.DT_ItemStats"));
		if (!Stats)
		{
			Test.AddError(TEXT("DT_ItemStats does not load"));
			return nullptr;
		}
		const FMDItemStatsRow* StatsRow = Stats->FindRow<FMDItemStatsRow>(FName(Row), TEXT("RangedTests"), false);
		if (!StatsRow)
		{
			Test.AddError(FString::Printf(TEXT("DT_ItemStats has no row %s"), Row));
			return nullptr;
		}
		UMDRangedWeaponData* Data = StatsRow->RangedData.LoadSynchronous();
		if (!Data)
		{
			Test.AddError(FString::Printf(TEXT("Row %s has no loadable RangedData"), Row));
		}
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedDataRowsTest, "MazeDemons.Ranged.Data.RowsValidate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedDataRowsTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedTestsLocal;
	UDataTable* Items = LoadTable(TEXT("/Game/Blueprints/Items/DT_Items.DT_Items"));
	TestNotNull(TEXT("DT_Items loads"), Items);
	for (const TCHAR* Row : GunRows)
	{
		if (Items)
		{
			TestTrue(*FString::Printf(TEXT("DT_Items has %s"), Row), Items->FindRowUnchecked(FName(Row)) != nullptr);
		}
		if (const UMDRangedWeaponData* Data = LoadGunData(*this, Row))
		{
			for (const FString& Error : Data->GetValidationErrors())
			{
				AddError(FString::Printf(TEXT("%s: %s"), Row, *Error));
			}
			if (Items && !Data->AmmoRow.IsNone())
			{
				TestTrue(*FString::Printf(TEXT("%s ammo row %s exists"), Row, *Data->AmmoRow.ToString()), Items->FindRowUnchecked(Data->AmmoRow) != nullptr);
			}
		}
	}
	for (const TCHAR* Row : AmmoRows)
	{
		if (Items)
		{
			TestTrue(*FString::Printf(TEXT("DT_Items has %s"), Row), Items->FindRowUnchecked(FName(Row)) != nullptr);
		}
	}
	TestNotNull(TEXT("DT_ElementResist loads"), LoadTable(TEXT("/Game/Blueprints/Items/DT_ElementResist.DT_ElementResist")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedSlotNineTest, "MazeDemons.Ranged.Loadout.SlotNineNoConflicts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedSlotNineTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("ten equip slots"), (int32)MDItemType::NumEquipSlots, 10);
	TestEqual(TEXT("ranged equip slot is 9"), (int32)MDItemType::RangedSlot, 9);
	TestTrue(TEXT("ranged slot is inside the equip range"), MDItemType::RangedSlot < MDItemType::NumEquipSlots);
	TestEqual(TEXT("Ranged item type ordinal 12 (append-only enum)"), (int32)MDItemType::Ranged, 12);
	TestEqual(TEXT("ranged conflicts with nothing"), UMDInventoryComponent::GetConflictingSlots(MDItemType::RangedSlot).Num(), 0);
	for (uint8 Slot = 0; Slot < MDItemType::NumEquipSlots; ++Slot)
	{
		TestFalse(*FString::Printf(TEXT("slot %d never clears the ranged slot"), Slot),
			UMDInventoryComponent::GetConflictingSlots(Slot).Contains(MDItemType::RangedSlot));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedSwapGatesTest, "MazeDemons.Ranged.Loadout.SwapGatesMeleeInputs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedSwapGatesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("melee input allowed outside ranged mode"), FMDRangedRules::IsMeleeInputAllowed(false));
	TestFalse(TEXT("melee input ignored in ranged mode"), FMDRangedRules::IsMeleeInputAllowed(true));
	TestFalse(TEXT("ranged input ignored outside ranged mode"), FMDRangedRules::IsRangedInputAllowed(false));
	TestTrue(TEXT("ranged input allowed in ranged mode"), FMDRangedRules::IsRangedInputAllowed(true));

	// CanSwapMode(bWantRanged, bGun, bDead, bAttacking, bStaggered, SinceLast, Cooldown)
	TestTrue(TEXT("enter with a gun"), FMDRangedRules::CanSwapMode(true, true, false, false, false, 5.0f, 0.4f));
	TestFalse(TEXT("cannot enter without a gun"), FMDRangedRules::CanSwapMode(true, false, false, false, false, 5.0f, 0.4f));
	TestTrue(TEXT("can always leave ranged mode"), FMDRangedRules::CanSwapMode(false, false, false, false, false, 5.0f, 0.4f));
	TestFalse(TEXT("dead blocks swap"), FMDRangedRules::CanSwapMode(true, true, true, false, false, 5.0f, 0.4f));
	TestFalse(TEXT("mid-swing blocks swap"), FMDRangedRules::CanSwapMode(true, true, false, true, false, 5.0f, 0.4f));
	TestFalse(TEXT("stagger blocks swap"), FMDRangedRules::CanSwapMode(false, true, false, false, true, 5.0f, 0.4f));
	TestFalse(TEXT("cooldown blocks swap"), FMDRangedRules::CanSwapMode(true, true, false, false, false, 0.1f, 0.4f));

	// The component flips and replicates the flag; a plain actor with authority is enough (no inventory = no gun).
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner = World->SpawnActor<AActor>();
	UMDRangedComponent* Comp = NewObject<UMDRangedComponent>(Owner);
	Comp->RegisterComponent();
	TestFalse(TEXT("starts in melee mode"), Comp->IsRangedMode());
	TestFalse(TEXT("swap needs a gun"), Comp->TrySwapMode());
	Comp->SetRangedMode(true);
	TestTrue(TEXT("forced ranged mode"), Comp->IsRangedMode());
	TestTrue(TEXT("melee actor gate sees ranged mode"), UMDRangedComponent::IsActorInRangedMode(Owner));
	Comp->SetRangedMode(false);
	TestFalse(TEXT("back to melee mode"), UMDRangedComponent::IsActorInRangedMode(Owner));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedEquipEntersTest, "MazeDemons.Ranged.Loadout.EquipEmptyHandedEntersRangedMode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedEquipEntersTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("gun equipped empty handed enters ranged mode"), FMDRangedRules::ShouldEnterRangedModeOnEquip(false, true));
	TestFalse(TEXT("gun equipped with a melee weapon stays in melee mode"), FMDRangedRules::ShouldEnterRangedModeOnEquip(true, true));
	TestFalse(TEXT("no gun, no mode change"), FMDRangedRules::ShouldEnterRangedModeOnEquip(false, false));
	TestTrue(TEXT("removing the gun ends ranged mode"), FMDRangedRules::ShouldLeaveRangedMode(true, false));
	TestFalse(TEXT("gun still held keeps ranged mode"), FMDRangedRules::ShouldLeaveRangedMode(true, true));
	TestFalse(TEXT("melee mode is untouched"), FMDRangedRules::ShouldLeaveRangedMode(false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedStanceTest, "MazeDemons.Ranged.Anim.StanceFromRangedMode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedStanceTest::RunTest(const FString& Parameters)
{
	// Chooser columns store enum values, so the new stances must be appended after Crossbow.
	TestEqual(TEXT("Crossbow stays 4"), (int32)EMDWeaponStance::Crossbow, 4);
	TestEqual(TEXT("Pistol is 5"), (int32)EMDWeaponStance::Pistol, 5);
	TestEqual(TEXT("Rifle is 6"), (int32)EMDWeaponStance::Rifle, 6);
	TestEqual(TEXT("Staff is 7"), (int32)EMDWeaponStance::Staff, 7);

	TestEqual(TEXT("melee mode keeps the melee stance"), FMDRangedRules::ResolveStance(false, EMDWeaponStance::Greatsword, EMDWeaponStance::Pistol), EMDWeaponStance::Greatsword);
	TestEqual(TEXT("ranged mode reads the gun stance"), FMDRangedRules::ResolveStance(true, EMDWeaponStance::Greatsword, EMDWeaponStance::Rifle), EMDWeaponStance::Rifle);
	TestEqual(TEXT("ranged mode reads Staff"), FMDRangedRules::ResolveStance(true, EMDWeaponStance::Unarmed, EMDWeaponStance::Staff), EMDWeaponStance::Staff);
	TestEqual(TEXT("ranged mode with no gun data is Unarmed"), FMDRangedRules::ResolveStance(true, EMDWeaponStance::DualSword, EMDWeaponStance::Unarmed), EMDWeaponStance::Unarmed);

	// The anim BP path: GetWeaponStanceForActor reads the component in ranged mode.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner = World->SpawnActor<AActor>();
	UMDRangedComponent* Comp = NewObject<UMDRangedComponent>(Owner);
	Comp->RegisterComponent();
	TestEqual(TEXT("melee mode: Unarmed with no inventory"), UMDCombatStatics::GetWeaponStanceForActor(Owner), EMDWeaponStance::Unarmed);
	Comp->SetRangedMode(true);
	TestEqual(TEXT("ranged mode without a gun is Unarmed"), UMDCombatStatics::GetWeaponStanceForActor(Owner), EMDWeaponStance::Unarmed);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedGripAlphaTest, "MazeDemons.Ranged.Anim.GripTypeAlphaRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedGripAlphaTest::RunTest(const FString& Parameters)
{
	// SupportHandIKAlpha(Grip, bAimed, DisableCurve, bRangedMode, bHasWeapon)
	TestEqual(TEXT("two-hander full alpha hip"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, false, 0.0f, true, true), 1.0f);
	TestEqual(TEXT("two-hander full alpha aimed"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 0.0f, true, true), 1.0f);
	TestEqual(TEXT("pistol hip: off"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::OneHandedSupportWhenAimed, false, 0.0f, true, true), 0.0f);
	TestEqual(TEXT("pistol aimed: on"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::OneHandedSupportWhenAimed, true, 0.0f, true, true), 1.0f);
	TestEqual(TEXT("one-hander never"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::OneHanded, true, 0.0f, true, true), 0.0f);
	TestEqual(TEXT("reload curve kills it"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 1.0f, true, true), 0.0f);
	TestEqual(TEXT("half curve halves it"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 0.5f, true, true), 0.5f);
	TestEqual(TEXT("curve above 1 clamps"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 7.0f, true, true), 0.0f);
	TestEqual(TEXT("melee mode: off"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 0.0f, false, true), 0.0f);
	TestEqual(TEXT("no weapon: off"), FMDRangedRules::SupportHandIKAlpha(EMDGripType::TwoHanded, true, 0.0f, true, false), 0.0f);
	TestFalse(TEXT("one-hander needs no support grip"), FMDRangedRules::GripNeedsSupportHand(EMDGripType::OneHanded));
	TestTrue(TEXT("two-hander needs a support grip"), FMDRangedRules::GripNeedsSupportHand(EMDGripType::TwoHanded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedSocketsTest, "MazeDemons.Ranged.Anim.SupportSocketsPresent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedSocketsTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedTestsLocal;
	for (const TCHAR* Row : GunRows)
	{
		const UMDRangedWeaponData* Data = LoadGunData(*this, Row);
		if (!Data)
		{
			continue;
		}
		// A Fab mesh without Grip_R / Grip_L is fine when the data asset carries a per-weapon offset instead.
		TestTrue(*FString::Printf(TEXT("%s: main-hand grip (Grip_R socket or HandOffset)"), Row), Data->HasUsableRightGrip());
		TestTrue(*FString::Printf(TEXT("%s: support grip (Grip_L socket or SupportHandOffset)"), Row), Data->HasUsableSupportGrip());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
