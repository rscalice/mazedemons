// Armor replication regression tests (listen server + clients): the starter kit must not run before the pawn is dressed, must not
// clobber what is already equipped, and the Equipped array and its client-side diff must cover all ten equip slots.
#include "Misc/AutomationTest.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "TimerManager.h"

// Reaches the protected bits of the player state and the inventory (declared a friend in both).
struct FMDArmorTestAccess
{
	static void PawnSet(AMDPlayerState* State, APawn* Pawn) { State->HandlePawnSet(State, Pawn, nullptr); }
	static TArray<FMDEquippedSlot>& Equipped(UMDInventoryComponent* C) { return C->Equipped; }
	static const TArray<FMDEquippedSlot>& Prev(const UMDInventoryComponent* C) { return C->PrevEquipped; }
	static void OnRepEquipped(UMDInventoryComponent* C) { C->OnRep_Equipped(); }
	static void GrantKit(AMDPlayerState* State) { State->GrantStarterKit(); }
};

namespace MDArmorTestsLocal
{
	struct FArmorFixture
	{
		UWorld* World = nullptr;
		APawn* Pawn = nullptr;
		AMDPlayerState* State = nullptr;
		UMDInventoryComponent* Inv = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDArmorTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			if (AMDGameState* GameState = World->SpawnActor<AMDGameState>())
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
			UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
			if (!ASC || !Items)
			{
				Test.AddError(TEXT("Missing ASC or DT_Items"));
				return false;
			}
			ASC->InitAbilityActorInfo(State, Pawn);
			ASC->RegisterAttributeSet(State->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			Inv = NewObject<UMDInventoryComponent>(State);
			Inv->ItemTable = Items;
			Inv->RegisterComponent();
			return true;
		}

		bool Has(uint8 Slot, FName Row = NAME_None) const
		{
			return Inv->GetEquipped().ContainsByPredicate([Slot, Row](const FMDEquippedSlot& E) { return E.Slot == Slot && (Row.IsNone() || E.ItemRowName == Row); });
		}

		~FArmorFixture()
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDArmorStarterKitOrderTest, "MazeDemons.Armor.StarterKitWaitsForPawnSetup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDArmorStarterKitOrderTest::RunTest(const FString& Parameters)
{
	using namespace MDArmorTestsLocal;
	FArmorFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	// First pawn: OnPawnSet fires inside Possess, before BP_Player's Possessed event dresses the player (only if Equipped is empty).
	FMDArmorTestAccess::PawnSet(Fx.State, Fx.Pawn);
	TestEqual(TEXT("nothing is equipped while the pawn is still being set up"), Fx.Inv->GetEquipped().Num(), 0);

	// Next tick: the kit arrives.
	Fx.World->GetTimerManager().Tick(0.1f);
	TestTrue(TEXT("the pistol is equipped one tick later"), Fx.Has(MDItemType::RangedSlot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDArmorStarterKitKeepsArmorTest, "MazeDemons.Armor.StarterKitKeepsEquippedArmor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDArmorStarterKitKeepsArmorTest::RunTest(const FString& Parameters)
{
	using namespace MDArmorTestsLocal;
	FArmorFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Chest(TEXT("ClothChestPiece"));
	const FName Legs(TEXT("ClothLegs"));
	const FName Boots(TEXT("ClothBoots"));
	for (const FName Row : { Chest, Legs, Boots })
	{
		if (!Fx.Inv->AddItem(Row, 1))
		{
			AddWarning(FString::Printf(TEXT("DT_Items has no %s row; skipping"), *Row.ToString()));
			return true;
		}
		Fx.Inv->RequestEquip(Fx.Inv->GetItems().Num() - 1);
	}
	TestEqual(TEXT("three cloth pieces equipped"), Fx.Inv->GetEquipped().Num(), 3);

	FMDArmorTestAccess::GrantKit(Fx.State);
	TestTrue(TEXT("the pistol joined the cloth set"), Fx.Has(MDItemType::RangedSlot));
	TestEqual(TEXT("four pieces in total"), Fx.Inv->GetEquipped().Num(), 4);
	TestTrue(TEXT("chest kept"), Fx.Has(MDItemType::Chest, Chest));
	TestTrue(TEXT("legs kept"), Fx.Has(MDItemType::Legs, Legs));
	TestTrue(TEXT("boots kept"), Fx.Has(MDItemType::Boots, Boots));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDArmorTenSlotsTest, "MazeDemons.Armor.AllTenEquipSlotsReplicate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDArmorTenSlotsTest::RunTest(const FString& Parameters)
{
	using namespace MDArmorTestsLocal;
	FArmorFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	// The array the server replicates, one entry per equip slot 0..9, as a client receives it.
	TArray<FMDEquippedSlot>& Equipped = FMDArmorTestAccess::Equipped(Fx.Inv);
	for (uint8 Slot = 0; Slot < MDItemType::NumEquipSlots; ++Slot)
	{
		FMDEquippedSlot Entry;
		Entry.Slot = Slot;
		Entry.ItemRowName = FName(*FString::Printf(TEXT("Row%d"), Slot));
		Equipped.Add(Entry);
	}
	FMDArmorTestAccess::OnRepEquipped(Fx.Inv);
	TestEqual(TEXT("the client snapshot holds all ten slots"), FMDArmorTestAccess::Prev(Fx.Inv).Num(), 10);
	for (uint8 Slot = 0; Slot < MDItemType::NumEquipSlots; ++Slot)
	{
		TestTrue(*FString::Printf(TEXT("slot %d is in the replicated list"), Slot), Fx.Has(Slot));
	}

	// Armor pieces and the gun come off again: the snapshot follows.
	Equipped.RemoveAll([](const FMDEquippedSlot& E) { return E.Slot >= MDItemType::Chest; });
	FMDArmorTestAccess::OnRepEquipped(Fx.Inv);
	TestEqual(TEXT("slots 0-4 remain"), FMDArmorTestAccess::Prev(Fx.Inv).Num(), 5);
	return true;
}
