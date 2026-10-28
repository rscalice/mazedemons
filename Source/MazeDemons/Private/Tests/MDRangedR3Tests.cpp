// R3 tests: reload, ammo (reserve = bag stack, magazine per gun), pity, drops, pickups, station purchases, starter kit and the ranged
// slot in repair / death penalty. The test world does not tick, so reload timers never fire: tests call CompleteReload() directly.
#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDGameSettings.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDStation.h"
#include "Ranged/MDAmmoDrops.h"
#include "Ranged/MDAmmoPickup.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedWeaponData.h"

// Reaches the private bits of the inventory and the player state for the R3 tests (declared a friend in both).
struct FMDAmmoTestAccess
{
	static void SetGold(UMDInventoryComponent* C, int32 Gold) { C->GoldAmount = Gold; }
	static bool BuyAmmo(UMDInventoryComponent* C, AActor* Station, FName Row) { return C->BuyAmmo_Impl(Station, Row); }
	static void SetDurability(UMDInventoryComponent* C, uint8 Slot, int32 Value) { C->DebugSetDurability(Slot, Value); }
	static void GrantKit(AMDPlayerState* State) { State->GrantStarterKit(); }
};

namespace MDRangedR3TestsLocal
{
	const FName Light(TEXT("AmmoLight"));
	const FName Heavy(TEXT("AmmoHeavy"));
	const FName Pistol(TEXT("HollowPistol"));
	const FName Repeater(TEXT("CatacombRepeater"));

	// A deterministic gun that uses Light Rounds: mag 8, 1 s reload, no spread.
	UMDRangedWeaponData* MakeAmmoGun(int32 MagSize = 8)
	{
		UMDRangedWeaponData* Data = NewObject<UMDRangedWeaponData>(GetTransientPackage());
		Data->FireMode = EMDFireMode::Semi;
		Data->HitModel = EMDRangedHitModel::Projectile;
		Data->RoundsPerSecond = 3.0f;
		Data->DamagePerShot = 10.0f;
		Data->SpreadHip = 0.0f;
		Data->SpreadAimed = 0.0f;
		Data->SpreadBloom = 0.0f;
		Data->MagSize = MagSize;
		Data->AmmoRow = Light;
		Data->ReloadSeconds = 1.0f;
		Data->Projectile.Speed = 20000.0f;
		Data->Projectile.Radius = 10.0f;
		Data->Projectile.MaxRange = 4000.0f;
		return Data;
	}

	struct FR3Fixture
	{
		UWorld* World = nullptr;
		AMDGameState* GameState = nullptr;
		APawn* Pawn = nullptr;
		AMDPlayerState* State = nullptr;
		UMDRangedComponent* Ranged = nullptr;
		UMDInventoryComponent* Inv = nullptr;
		UMDRangedWeaponData* Gun = nullptr;

		bool Init(FAutomationTestBase& Test, bool bUseTestGun = true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRangedR3TestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			GameState = World->SpawnActor<AMDGameState>();
			if (GameState)
			{
				World->SetGameState(GameState);
			}
			Pawn = World->SpawnActor<APawn>();
			State = World->SpawnActor<AMDPlayerState>();
			if (!Pawn || !State)
			{
				Test.AddError(TEXT("Could not spawn the pawn or its player state"));
				return false;
			}
			State->SetRole(ROLE_Authority);
			Pawn->SetPlayerState(State);
			UMDAbilitySystemComponent* ASC = State->GetMDAbilitySystemComponent();
			if (!ASC)
			{
				Test.AddError(TEXT("Player state has no ASC"));
				return false;
			}
			ASC->InitAbilityActorInfo(State, Pawn);
			ASC->RegisterAttributeSet(State->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			Ranged = State->RangedComponent;
			if (!Ranged)
			{
				Test.AddError(TEXT("Player state has no ranged component"));
				return false;
			}
			UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
			if (!Items)
			{
				Test.AddError(TEXT("Could not load DT_Items"));
				return false;
			}
			Inv = NewObject<UMDInventoryComponent>(State);
			Inv->ItemTable = Items;
			Inv->RegisterComponent();
			Ranged->SetPawnForTests(Pawn);
			if (bUseTestGun)
			{
				Gun = MakeAmmoGun();
				Ranged->SetWeaponDataForTests(Gun);
				Ranged->SetRangedMode(true);
				Ranged->SetMagRounds(MDItemType::RangedSlot, 0);
			}
			return true;
		}

		int32 BagIndexOf(FName Row) const
		{
			return Inv->GetItems().IndexOfByPredicate([Row](const FMDItemStack& Stack) { return Stack.ItemRowName == Row; });
		}

		~FR3Fixture()
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
		}
	};

	int32 Mag(const FR3Fixture& Fx) { return Fx.Ranged->GetMagRounds(MDItemType::RangedSlot); }
}

// ---------------------------------------------------------------------------------------------------------------------
// Reload
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3ReloadMovesTest, "MazeDemons.Ranged.Ammo.ReloadMovesMinOfMagAndReserve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3ReloadMovesTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	// The pure rule first.
	TestEqual(TEXT("plenty of reserve fills the magazine"), FMDAmmoRules::ReloadAmount(0, 8, 40), 8);
	TestEqual(TEXT("a short reserve moves what it has"), FMDAmmoRules::ReloadAmount(0, 8, 3), 3);
	TestEqual(TEXT("a full magazine moves nothing"), FMDAmmoRules::ReloadAmount(8, 8, 40), 0);
	TestEqual(TEXT("never negative"), FMDAmmoRules::ReloadAmount(9, 8, 40), 0);

	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	TestTrue(TEXT("40 Light Rounds into the bag"), Fx.Inv->AddItem(Light, 40));
	TestEqual(TEXT("reserve counts the bag stack"), Fx.Ranged->GetReserveAmmo(), 40);
	TestEqual(TEXT("empty magazine"), Mag(Fx), 0);

	TestEqual(TEXT("the reload starts"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	TestTrue(TEXT("reloading"), Fx.Ranged->IsReloading());
	TestEqual(TEXT("nothing moves before the reload notify"), Mag(Fx), 0);
	TestEqual(TEXT("the bag is untouched before the notify"), Fx.Inv->GetAmmoCount(Light), 40);
	TestEqual(TEXT("a second reload is refused while one runs"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::AlreadyReloading);

	TestEqual(TEXT("the notify moves a full magazine"), Fx.Ranged->CompleteReload(), 8);
	TestFalse(TEXT("reload over"), Fx.Ranged->IsReloading());
	TestEqual(TEXT("magazine full"), Mag(Fx), 8);
	TestEqual(TEXT("reserve down by what moved"), Fx.Inv->GetAmmoCount(Light), 32);
	TestEqual(TEXT("a full magazine cannot reload"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::MagFull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3PartialReloadTest, "MazeDemons.Ranged.Ammo.PartialReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3PartialReloadTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	// Mid-magazine top-up: 3 in the gun, 40 in the bag -> moves 5, not 8.
	Fx.Inv->AddItem(Light, 40);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 3);
	TestEqual(TEXT("begin"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	TestEqual(TEXT("top-up moves MagSize - Mag"), Fx.Ranged->CompleteReload(), 5);
	TestEqual(TEXT("magazine full"), Mag(Fx), 8);
	TestEqual(TEXT("reserve 35"), Fx.Inv->GetAmmoCount(Light), 35);

	// Short reserve: 5 in the gun, only 2 in the bag -> moves 2 and the bag is empty.
	Fx.Inv->ConsumeAmmo(Light, 35);
	Fx.Inv->AddItem(Light, 2);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 5);
	TestEqual(TEXT("begin"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	TestEqual(TEXT("partial reload moves the reserve"), Fx.Ranged->CompleteReload(), 2);
	TestEqual(TEXT("magazine 7 of 8"), Mag(Fx), 7);
	TestEqual(TEXT("reserve empty"), Fx.Inv->GetAmmoCount(Light), 0);
	TestEqual(TEXT("no reserve, no reload"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::NoReserve);

	// The other refusals.
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 0);
	Fx.Inv->AddItem(Light, 10);
	Fx.Ranged->SetRangedMode(false);
	TestEqual(TEXT("melee mode: no reload"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::NotRangedMode);
	Fx.Ranged->SetRangedMode(true);
	Fx.Gun->AmmoRow = NAME_None;
	TestEqual(TEXT("no ammo type (a beam): no reload"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::NoAmmoType);
	Fx.Gun->AmmoRow = Light;
	TestEqual(TEXT("and with the ammo row back it works"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3CancelTest, "MazeDemons.Ranged.Reload.CancelKeepsReserve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3CancelTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	Fx.Inv->AddItem(Light, 40);
	UMDAbilitySystemComponent* ASC = Fx.State->GetMDAbilitySystemComponent();

	// A dodge / stagger / death all end in CancelReload.
	TestEqual(TEXT("begin"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	TestTrue(TEXT("the Reloading tag is up"), ASC->HasMatchingGameplayTag(TAG_State_Reloading));
	Fx.Ranged->CancelReload();
	TestFalse(TEXT("no longer reloading"), Fx.Ranged->IsReloading());
	TestFalse(TEXT("the Reloading tag is gone"), ASC->HasMatchingGameplayTag(TAG_State_Reloading));
	TestEqual(TEXT("the reserve is untouched"), Fx.Inv->GetAmmoCount(Light), 40);
	TestEqual(TEXT("the magazine is untouched"), Mag(Fx), 0);
	TestEqual(TEXT("a late notify moves nothing"), Fx.Ranged->CompleteReload(), 0);
	TestEqual(TEXT("still 40"), Fx.Inv->GetAmmoCount(Light), 40);

	// Holstering the gun cancels too, and the reload can start again afterwards.
	TestEqual(TEXT("begin again"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	Fx.Ranged->SetRangedMode(false);
	TestFalse(TEXT("holstering cancels"), Fx.Ranged->IsReloading());
	TestEqual(TEXT("reserve still 40"), Fx.Inv->GetAmmoCount(Light), 40);
	Fx.Ranged->SetRangedMode(true);
	TestEqual(TEXT("begin a third time"), Fx.Ranged->BeginReloadServer(), EMDReloadReject::None);
	TestEqual(TEXT("and it completes"), Fx.Ranged->CompleteReload(), 8);
	TestEqual(TEXT("reserve 32"), Fx.Inv->GetAmmoCount(Light), 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3AutoReloadTest, "MazeDemons.Ranged.Reload.AutoReloadOnEmptyAndNoFireDuringReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3AutoReloadTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	Fx.Inv->AddItem(Light, 40);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 1);
	FVector Muzzle = FVector::ZeroVector;
	Fx.Ranged->GetMuzzleWorld(Muzzle);
	FMDShotRequest Request;
	Request.ShotId = 1;
	Request.ClientTime = -2.0f;
	Request.Origin = Muzzle;
	Request.Dir = Fx.Pawn->GetBaseAimRotation().Vector();

	TestTrue(TEXT("auto-reload defaults on"), Fx.Ranged->IsAutoReloadEnabled());
	TestEqual(TEXT("the last round is fired"), Fx.Ranged->ProcessShot(Request, 0.0f), EMDShotReject::None);
	TestEqual(TEXT("the magazine is empty"), Mag(Fx), 0);
	TestTrue(TEXT("the empty magazine starts a reload by itself"), Fx.Ranged->IsReloading());
	TestEqual(TEXT("one shot counted for the pity"), Fx.Ranged->GetShotsSinceAmmoDrop(), 1);

	// Firing mid-reload is refused on the server too.
	Request.ShotId = 2;
	Request.ClientTime = -1.0f;
	TestNotEqual(TEXT("no shot while reloading"), Fx.Ranged->ProcessShot(Request, 0.0f), EMDShotReject::None);
	TestEqual(TEXT("the reload finishes"), Fx.Ranged->CompleteReload(), 8);

	// An empty magazine with no reserve cannot fire and cannot reload.
	Fx.Inv->ConsumeAmmo(Light, 100);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 0);
	Request.ShotId = 3;
	Request.ClientTime = 0.0f;
	TestEqual(TEXT("empty cannot fire"), Fx.Ranged->ProcessShot(Request, 0.0f), EMDShotReject::NoAmmo);
	TestEqual(TEXT("and nothing to reload"), Fx.Ranged->GetReloadReject(), EMDReloadReject::NoReserve);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3MagPersistsTest, "MazeDemons.Ranged.Ammo.MagPersistsAcrossSwap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3MagPersistsTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this, false))
	{
		return false;
	}
	TestTrue(TEXT("pistol in the bag"), Fx.Inv->AddItem(Pistol, 1));
	TestTrue(TEXT("repeater in the bag"), Fx.Inv->AddItem(Repeater, 1));

	Fx.Inv->RequestEquip(Fx.BagIndexOf(Pistol));
	Fx.Ranged->HandleEquipmentChanged();
	const UMDRangedWeaponData* PistolData = Fx.Inv->GetEquippedRangedData();
	if (!TestNotNull(TEXT("the pistol is equipped"), PistolData))
	{
		return false;
	}
	TestEqual(TEXT("a never-held gun starts full"), Mag(Fx), PistolData->MagSize);

	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 3); // three rounds left in the pistol
	Fx.Inv->RequestEquip(Fx.BagIndexOf(Repeater));
	Fx.Ranged->HandleEquipmentChanged();
	const UMDRangedWeaponData* RepeaterData = Fx.Inv->GetEquippedRangedData();
	if (!TestNotNull(TEXT("the repeater is equipped"), RepeaterData))
	{
		return false;
	}
	TestEqual(TEXT("the repeater starts full"), Mag(Fx), RepeaterData->MagSize);
	TestEqual(TEXT("the pistol's rounds are remembered"), Fx.Ranged->GetStashedMag(Pistol), 3);

	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 10);
	Fx.Inv->RequestEquip(Fx.BagIndexOf(Pistol));
	Fx.Ranged->HandleEquipmentChanged();
	TestEqual(TEXT("the pistol comes back with its 3 rounds, not a free full magazine"), Mag(Fx), 3);
	TestEqual(TEXT("the repeater's rounds are remembered"), Fx.Ranged->GetStashedMag(Repeater), 10);

	Fx.Inv->RequestEquip(Fx.BagIndexOf(Repeater));
	Fx.Ranged->HandleEquipmentChanged();
	TestEqual(TEXT("and the repeater has its 10"), Mag(Fx), 10);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drops, pity, pickups
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3PityTest, "MazeDemons.Ranged.Ammo.PityCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3PityTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	TestNearlyEqual(TEXT("0 shots: base chance"), FMDAmmoRules::PityDropChance(0.15f, 0), 0.15f, 0.0001f);
	TestNearlyEqual(TEXT("19 shots: still the base"), FMDAmmoRules::PityDropChance(0.15f, 19), 0.15f, 0.0001f);
	TestNearlyEqual(TEXT("20 shots: +10%"), FMDAmmoRules::PityDropChance(0.15f, 20), 0.25f, 0.0001f);
	TestNearlyEqual(TEXT("40 shots: +20%"), FMDAmmoRules::PityDropChance(0.15f, 40), 0.35f, 0.0001f);
	TestNearlyEqual(TEXT("99 shots: +40%"), FMDAmmoRules::PityDropChance(0.15f, 99), 0.55f, 0.0001f);
	TestNearlyEqual(TEXT("clamped to 1"), FMDAmmoRules::PityDropChance(0.15f, 5000), 1.0f, 0.0001f);
	TestNearlyEqual(TEXT("negative shots count as zero"), FMDAmmoRules::PityDropChance(0.15f, -50), 0.15f, 0.0001f);
	TestTrue(TEXT("roll below the chance drops"), FMDAmmoRules::RollDrops(0.15f, 0.149f));
	TestFalse(TEXT("roll at the chance does not"), FMDAmmoRules::RollDrops(0.15f, 0.15f));
	TestFalse(TEXT("chance 0 never drops"), FMDAmmoRules::RollDrops(0.0f, 0.0f));

	// The drop roll uses the curve: a 0.2 roll misses a fresh Ghoul but hits once 20 shots went without a drop.
	FMDAmmoDropRow Ghoul;
	Ghoul.AmmoRow = Light;
	Ghoul.Quantity = 4;
	Ghoul.Chance = 0.15f;
	const FMDEconomyTuning Economy;
	TestEqual(TEXT("fresh: no drop at roll 0.2"), FMDAmmoDrops::DropQuantity(Ghoul, 0, Economy, 0.2f), 0);
	TestEqual(TEXT("20 dry shots: the drop lands"), FMDAmmoDrops::DropQuantity(Ghoul, 20, Economy, 0.2f), 4);
	TestEqual(TEXT("the economy defaults are +10% per 20 shots"), Economy.AmmoPityShotsPerStep, 20);
	TestNearlyEqual(TEXT("the pity bonus default"), Economy.AmmoPityBonus, 0.10f, 0.0001f);

	// The counter lives on the ranged component and resets on a drop.
	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	TestEqual(TEXT("starts at 0"), Fx.Ranged->GetShotsSinceAmmoDrop(), 0);
	Fx.Ranged->ResetShotsSinceAmmoDrop();
	TestEqual(TEXT("reset stays 0"), Fx.Ranged->GetShotsSinceAmmoDrop(), 0);

	// The table the game ships with.
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, FMDAmmoDrops::TablePath());
	if (!TestNotNull(TEXT("DT_AmmoDrops exists"), Table))
	{
		return false;
	}
	struct FExpect { FGameplayTag Tag; FName Ammo; int32 Qty; float Chance; };
	const FExpect Expected[] = {
		{ TAG_Enemy_Ghoul.GetTag(), Light, 4, 0.15f },
		{ TAG_Enemy_Cultist.GetTag(), Light, 8, 0.50f },
		{ TAG_Enemy_BoneArcher.GetTag(), Heavy, 6, 0.70f },
		{ TAG_Enemy_GraveBrute.GetTag(), Heavy, 8, 0.40f },
	};
	for (const FExpect& E : Expected)
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(E.Tag);
		const FMDAmmoDropRow* Row = FMDAmmoDrops::FindRow(Table, Tags);
		if (TestNotNull(*FString::Printf(TEXT("a drop row for %s"), *E.Tag.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("%s ammo"), *E.Tag.ToString()), Row->AmmoRow, E.Ammo);
			TestEqual(*FString::Printf(TEXT("%s quantity"), *E.Tag.ToString()), Row->Quantity, E.Qty);
			TestNearlyEqual(*FString::Printf(TEXT("%s chance"), *E.Tag.ToString()), Row->Chance, E.Chance, 0.0001f);
		}
	}
	FGameplayTagContainer NoTags;
	TestNull(TEXT("an archetype with no row drops nothing"), FMDAmmoDrops::FindRow(Table, NoTags));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3PickupTest, "MazeDemons.Ranged.Ammo.PickupFirstTouchOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3PickupTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	APawn* Second = Fx.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("second pawn"), Second))
	{
		return false;
	}
	UMDInventoryComponent* SecondInv = NewObject<UMDInventoryComponent>(Second);
	SecondInv->ItemTable = Fx.Inv->ItemTable;
	SecondInv->RegisterComponent();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMDAmmoPickup* Pickup = Fx.World->SpawnActorDeferred<AMDAmmoPickup>(AMDAmmoPickup::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("pickup"), Pickup))
	{
		return false;
	}
	Pickup->ItemRowName = Light;
	Pickup->Quantity = 4;
	Pickup->FinishSpawning(FTransform::Identity);

	// The first pawn to touch it takes the whole pouch; the second finds it gone.
	// (The bag inventory lives on the player state; the pickup looks on the pawn, its controller and the player state.)
	TestFalse(TEXT("a non-pawn touching does nothing"), Pickup->Touch(Fx.State));
	TestTrue(TEXT("the first touch takes it"), Pickup->Touch(Fx.Pawn));
	TestEqual(TEXT("the first player got the rounds"), Fx.Inv->GetAmmoCount(Light), 4);
	TestFalse(TEXT("the second touch gets nothing"), Pickup->Touch(Second));
	TestEqual(TEXT("the second player has none"), SecondInv->GetAmmoCount(Light), 0);
	TestEqual(TEXT("and the first still has exactly 4"), Fx.Inv->GetAmmoCount(Light), 4);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Station purchases and the starter kit
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3PurchaseTest, "MazeDemons.Ranged.Ammo.StationPurchaseCapAndCost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3PurchaseTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	// The pure parts.
	TestEqual(TEXT("a full bundle fits"), FMDAmmoRules::PurchaseQuantity(50, 120, 10), 10);
	TestEqual(TEXT("only the room left"), FMDAmmoRules::PurchaseQuantity(115, 120, 10), 5);
	TestEqual(TEXT("a full stack buys nothing"), FMDAmmoRules::PurchaseQuantity(120, 120, 10), 0);
	TestEqual(TEXT("10 Light = 2 gold"), FMDAmmoRules::PurchaseCost(10, 10, 2), 2);
	TestEqual(TEXT("10 Heavy = 5 gold"), FMDAmmoRules::PurchaseCost(10, 10, 5), 5);
	TestEqual(TEXT("a partial bundle is pro rata, rounded up, at least 1"), FMDAmmoRules::PurchaseCost(3, 10, 2), 1);
	TestEqual(TEXT("5 Heavy = 3 gold"), FMDAmmoRules::PurchaseCost(5, 10, 5), 3);

	FR3Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDStation* Station = Fx.World->SpawnActor<AMDStation>();
	if (!TestNotNull(TEXT("station"), Station))
	{
		return false;
	}
	TestTrue(TEXT("stations offer the Ammo service by default"), Station->Supports(EMDStationService::Ammo));

	int32 Qty = 0;
	int32 Gold = 0;
	TestEqual(TEXT("no station"), Fx.Inv->GetBuyAmmoReject(nullptr, Light, Qty, Gold), EMDStationReject::NoStation);
	TestEqual(TEXT("broke"), Fx.Inv->GetBuyAmmoReject(Station, Light, Qty, Gold), EMDStationReject::NotEnoughGold);
	TestEqual(TEXT("a bundle of 10"), Qty, 10);
	TestEqual(TEXT("costs 2 gold"), Gold, 2);
	TestEqual(TEXT("not an ammo row"), Fx.Inv->GetBuyAmmoReject(Station, Pistol, Qty, Gold), EMDStationReject::NotForSale);

	FMDAmmoTestAccess::SetGold(Fx.Inv, 100);
	TestTrue(TEXT("buy 10 Light"), FMDAmmoTestAccess::BuyAmmo(Fx.Inv, Station, Light));
	TestEqual(TEXT("10 Light in the bag"), Fx.Inv->GetAmmoCount(Light), 10);
	TestEqual(TEXT("98 gold left"), Fx.Inv->GetGold(), 98);
	TestTrue(TEXT("buy 10 Heavy"), FMDAmmoTestAccess::BuyAmmo(Fx.Inv, Station, Heavy));
	TestEqual(TEXT("10 Heavy in the bag"), Fx.Inv->GetAmmoCount(Heavy), 10);
	TestEqual(TEXT("93 gold left"), Fx.Inv->GetGold(), 93);

	// The cap: one full stack. Fill Light to 115 -> the next purchase is 5 rounds for 1 gold, then it is refused.
	const int32 Cap = Fx.Inv->GetAmmoCap(Light);
	TestEqual(TEXT("one full Light stack is 120"), Cap, 120);
	Fx.Inv->AddItem(Light, Cap - 5 - Fx.Inv->GetAmmoCount(Light));
	TestEqual(TEXT("115 in the bag"), Fx.Inv->GetAmmoCount(Light), Cap - 5);
	TestEqual(TEXT("affordable"), Fx.Inv->GetBuyAmmoReject(Station, Light, Qty, Gold), EMDStationReject::None);
	TestEqual(TEXT("only the 5 that fit"), Qty, 5);
	TestEqual(TEXT("for 1 gold"), Gold, 1);
	TestTrue(TEXT("buy the top-up"), FMDAmmoTestAccess::BuyAmmo(Fx.Inv, Station, Light));
	TestEqual(TEXT("exactly a full stack"), Fx.Inv->GetAmmoCount(Light), Cap);
	TestEqual(TEXT("92 gold left"), Fx.Inv->GetGold(), 92);
	TestEqual(TEXT("the cap refuses more"), Fx.Inv->GetBuyAmmoReject(Station, Light, Qty, Gold), EMDStationReject::AmmoFull);
	TestFalse(TEXT("buying at the cap fails"), FMDAmmoTestAccess::BuyAmmo(Fx.Inv, Station, Light));
	TestEqual(TEXT("and costs nothing"), Fx.Inv->GetGold(), 92);

	// Service flag and range.
	Station->Services = static_cast<int32>(EMDStationService::Repair);
	TestEqual(TEXT("a station without the Ammo service"), Fx.Inv->GetBuyAmmoReject(Station, Heavy, Qty, Gold), EMDStationReject::NoStation);
	Station->Services = 15;
	Station->SetActorLocation(FVector(9000.0, 0.0, 0.0));
	TestEqual(TEXT("out of range"), Fx.Inv->GetBuyAmmoReject(Station, Heavy, Qty, Gold), EMDStationReject::OutOfRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3StarterKitTest, "MazeDemons.Ranged.Ammo.StarterKit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3StarterKitTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	const FMDEconomyTuning Tuning;
	bool bPistol = false;
	int32 Rounds = 0;
	for (const FMDStarterItem& Item : Tuning.StarterKit)
	{
		if (Item.ItemRow == Pistol && Item.bEquip)
		{
			bPistol = true;
		}
		if (Item.ItemRow == Light)
		{
			Rounds += Item.Quantity;
		}
	}
	TestTrue(TEXT("the default kit equips the Hollow Pistol"), bPistol);
	TestEqual(TEXT("and carries 40 Light Rounds"), Rounds, 40);

	FR3Fixture Fx;
	if (!Fx.Init(*this, false))
	{
		return false;
	}
	FMDAmmoTestAccess::GrantKit(Fx.State);
	TestEqual(TEXT("the pistol is in the ranged slot"), Fx.Inv->GetEquippedRangedRow(), Pistol);
	TestEqual(TEXT("40 Light Rounds in the bag"), Fx.Inv->GetAmmoCount(Light), 40);
	TestFalse(TEXT("the run starts in melee mode"), Fx.Ranged->IsRangedMode());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Durability: the ranged slot (9) repairs and takes the death penalty like every other slot
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3DurabilityTest, "MazeDemons.Ranged.Durability.RepairAndDeathPenaltyIncludeRangedSlot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3DurabilityTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR3TestsLocal;
	FR3Fixture Fx;
	if (!Fx.Init(*this, false))
	{
		return false;
	}
	Fx.Inv->AddItem(Pistol, 1);
	Fx.Inv->RequestEquip(Fx.BagIndexOf(Pistol));
	int32 Cur = 0;
	int32 Max = 0;
	Fx.Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	if (!TestTrue(TEXT("the equipped gun has durability"), Max > 0))
	{
		return false;
	}
	TestEqual(TEXT("it starts at full"), Cur, Max);

	// Death: ceil(10% of max) off the gun, like every equipped item.
	Fx.Inv->ApplyDeathPenalty();
	Fx.Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestEqual(TEXT("the death penalty wears the ranged slot"), Cur, Max - FMath::CeilToInt(Max * UMDGameSettings::GetEconomy().DeathDurabilityLossPct));

	// Repair at a station, slot 9 on its own and as part of Repair All.
	AMDStation* Station = Fx.World->SpawnActor<AMDStation>();
	if (!TestNotNull(TEXT("station"), Station))
	{
		return false;
	}
	int32 Cost = 0;
	TestEqual(TEXT("broke: the gun repair is priced but refused"), Fx.Inv->GetRepairReject(Station, MDItemType::RangedSlot, Cost), EMDStationReject::NotEnoughGold);
	TestTrue(TEXT("and it has a price"), Cost > 0);
	FMDAmmoTestAccess::SetGold(Fx.Inv, 500);
	TestEqual(TEXT("with gold, slot 9 repairs"), Fx.Inv->GetRepairReject(Station, MDItemType::RangedSlot, Cost), EMDStationReject::None);
	int32 AllCost = 0;
	TestEqual(TEXT("Repair All covers it too"), Fx.Inv->GetRepairReject(Station, 255, AllCost), EMDStationReject::None);
	TestEqual(TEXT("the same price when it is the only damaged item"), AllCost, Cost);

	Fx.Inv->RequestRepair(Station, MDItemType::RangedSlot);
	Fx.Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestEqual(TEXT("repaired to full"), Cur, Max);
	TestEqual(TEXT("the gold was spent"), Fx.Inv->GetGold(), 500 - Cost);
	TestEqual(TEXT("nothing left to repair"), Fx.Inv->GetRepairReject(Station, MDItemType::RangedSlot, Cost), EMDStationReject::FullDurability);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR3HudRulesTest, "MazeDemons.Ranged.Ammo.HudThresholds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR3HudRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("8 of 8 is not low"), FMDAmmoRules::IsMagLow(8, 8));
	TestFalse(TEXT("3 of 8 is not low"), FMDAmmoRules::IsMagLow(3, 8));
	TestTrue(TEXT("1 of 8 (12%) is low"), FMDAmmoRules::IsMagLow(1, 8));
	TestTrue(TEXT("0 is low"), FMDAmmoRules::IsMagLow(0, 8));
	TestTrue(TEXT("exactly 20% is low"), FMDAmmoRules::IsMagLow(5, 25));
	TestFalse(TEXT("6 of 25 (24%) is not"), FMDAmmoRules::IsMagLow(6, 25));
	TestFalse(TEXT("no gun, no red"), FMDAmmoRules::IsMagLow(0, 0));
	return true;
}
