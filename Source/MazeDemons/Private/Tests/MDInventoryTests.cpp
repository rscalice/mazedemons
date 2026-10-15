
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDInventoryComponent.h"
#include "MDPickupBase.h"
#include "MDInventoryTestListener.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Friend of UMDInventoryComponent (see MDInventoryComponent.h) so tests can reach protected RPC bodies / row lookups.
struct FMDInventoryTestAccess
{
	static uint8 GetSlot(const UMDInventoryComponent* C, FName Row) { return C->GetSlotForItem(Row); }
	static int32 GetMaxStack(const UMDInventoryComponent* C, FName Row) { return C->GetMaxStackForItem(Row); }
	static bool IsValid(const UMDInventoryComponent* C, FName Row) { return C->IsValidRow(Row); }

	static bool ValidateEquip(UMDInventoryComponent* C, int32 Index) { return C->Server_EquipItem_Validate(Index); }
	static void ServerEquip(UMDInventoryComponent* C, int32 Index) { C->Server_EquipItem_Implementation(Index); }

	static bool ValidateUnequip(UMDInventoryComponent* C, uint8 Slot) { return C->Server_UnequipSlot_Validate(Slot); }
	static void ServerUnequip(UMDInventoryComponent* C, uint8 Slot) { C->Server_UnequipSlot_Implementation(Slot); }

	static bool ValidateDrop(UMDInventoryComponent* C, int32 Index, int32 Qty) { return C->Server_DropItem_Validate(Index, Qty); }
	static void ServerDrop(UMDInventoryComponent* C, int32 Index, int32 Qty) { C->Server_DropItem_Implementation(Index, Qty); }
};

namespace
{
	constexpr uint8 TestGoldSlot = 9;
	constexpr uint8 TestMainHand = 0;
	constexpr uint8 TestTwoHanded = 3;
	constexpr uint8 TestChest = 5;

	// Transient game world + authoritative owner actor + inventory component wired to DT_Items. Cleans up on destruction.
	struct FMDInvFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UMDInventoryComponent* Inv = nullptr;
		UDataTable* Table = nullptr;

		bool Init(FAutomationTestBase& Test, int32 MaxSlots = 20)
		{
			Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
			if (!Table)
			{
				Test.AddError(TEXT("Could not load /Game/Blueprints/Items/DT_Items"));
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDInventoryTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			// A bare CreateWorld world has not been initialized for play; do so (no component code changes).
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			if (!Owner)
			{
				Test.AddError(TEXT("Could not spawn owner actor"));
				return false;
			}
			Owner->SetRole(ROLE_Authority); // explicit: AddItem & co. gate on HasAuthority()
			Inv = NewObject<UMDInventoryComponent>(Owner);
			Inv->ItemTable = Table;
			Inv->MaxSlots = MaxSlots;
			Inv->RegisterComponent();
			return true;
		}

		~FMDInvFixture()
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

		uint8 SlotOf(FName Row) const { return FMDInventoryTestAccess::GetSlot(Inv, Row); }
		int32 StackOf(FName Row) const { return FMDInventoryTestAccess::GetMaxStack(Inv, Row); }

		FName FindRow(TFunctionRef<bool(FName)> Pred) const
		{
			for (const FName Row : Table->GetRowNames())
			{
				if (Pred(Row))
				{
					return Row;
				}
			}
			return NAME_None;
		}

		// Two different rows that share an equip slot (slot 0..8). If bSecondStackable, B must have maxStack >= 2.
		bool FindPair(uint8& OutSlot, FName& A, FName& B, bool bSecondStackable) const
		{
			for (uint8 Slot = 0; Slot < TestGoldSlot; ++Slot)
			{
				TArray<FName> Rows;
				for (const FName Row : Table->GetRowNames())
				{
					if (SlotOf(Row) == Slot)
					{
						Rows.Add(Row);
					}
				}
				for (const FName Candidate : Rows)
				{
					if (bSecondStackable && StackOf(Candidate) < 2)
					{
						continue;
					}
					for (const FName Other : Rows)
					{
						if (Other != Candidate)
						{
							OutSlot = Slot;
							A = Other;
							B = Candidate;
							return true;
						}
					}
				}
			}
			return false;
		}
	};

	int32 IndexOfRow(const UMDInventoryComponent* Inv, FName Row)
	{
		return Inv->GetItems().IndexOfByPredicate([Row](const FMDItemStack& S) { return S.ItemRowName == Row; });
	}

	const FMDEquippedSlot* FindEquipped(const UMDInventoryComponent* Inv, uint8 Slot)
	{
		return Inv->GetEquipped().FindByPredicate([Slot](const FMDEquippedSlot& E) { return E.Slot == Slot; });
	}

	// Crash guards: report and return false when the arrays are not the size a test is about to index.
	bool NeedItems(FAutomationTestBase& T, const UMDInventoryComponent* Inv, int32 N, const TCHAR* Ctx)
	{
		if (Inv->GetItems().Num() < N)
		{
			T.AddError(FString::Printf(TEXT("%s: expected at least %d item stack(s), have %d (AddItem likely failed)"), Ctx, N, Inv->GetItems().Num()));
			return false;
		}
		return true;
	}

	int32 TotalOfRow(const UMDInventoryComponent* Inv, FName Row)
	{
		int32 Total = 0;
		for (const FMDItemStack& S : Inv->GetItems())
		{
			if (S.ItemRowName == Row)
			{
				Total += S.Quantity;
			}
		}
		return Total;
	}
}

// ---- Sanity ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvSanityTest, "MazeDemons.Inventory.Sanity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvSanityTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const bool bTable = Fx.Table != nullptr;
	const int32 NumRows = bTable ? Fx.Table->GetRowNames().Num() : 0;
	AddInfo(FString::Printf(TEXT("DT_Items rows: %d"), NumRows));
	TestTrue(TEXT("DT_Items loaded"), bTable);
	TestTrue(TEXT("DT_Items has > 0 rows"), NumRows > 0);

	const bool bAuth = Fx.Owner->HasAuthority();
	AddInfo(FString::Printf(TEXT("Owner LocalRole=%d RemoteRole=%d NetMode=%d WorldType=%d"),
		static_cast<int32>(Fx.Owner->GetLocalRole()), static_cast<int32>(Fx.Owner->GetRemoteRole()),
		static_cast<int32>(Fx.Owner->GetNetMode()), static_cast<int32>(Fx.World->WorldType)));
	TestTrue(TEXT("Owner->HasAuthority() is true"), bAuth);
	TestTrue(TEXT("Inv->ItemTable is valid"), IsValid(Fx.Inv->ItemTable));
	TestTrue(TEXT("Inv component is registered"), Fx.Inv->IsRegistered());
	TestTrue(TEXT("Inv->GetOwner() == Owner"), Fx.Inv->GetOwner() == Fx.Owner);
	if (NumRows <= 0 || !IsValid(Fx.Inv->ItemTable))
	{
		return false;
	}

	const TArray<FName> Names = Fx.Table->GetRowNames();
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		const FName R = Names[i];
		const uint8 Slot = Fx.SlotOf(R);
		const int32 Stack = Fx.StackOf(R);
		AddInfo(FString::Printf(TEXT("Row[%d] %s slot=%d maxStack=%d"), i, *R.ToString(), Slot, Stack));
		TestTrue(*FString::Printf(TEXT("IsValidRow(%s)"), *R.ToString()), FMDInventoryTestAccess::IsValid(Fx.Inv, R));
		TestTrue(*FString::Printf(TEXT("GetMaxStackForItem(%s) >= 1"), *R.ToString()), Stack >= 1);
	}

	const bool bAdded = Fx.Inv->AddItem(Names[0], 1);
	TestTrue(*FString::Printf(TEXT("AddItem(%s, 1) returns true"), *Names[0].ToString()), bAdded);
	TestEqual(TEXT("Items.Num() == 1 after AddItem"), Fx.Inv->GetItems().Num(), 1);
	return true;
}

// ---- AddItem ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvAddStackingTest, "MazeDemons.Inventory.AddItem.Stacking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvAddStackingTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this, 4))
	{
		return false;
	}
	const FName Row = Fx.FindRow([&Fx](FName R) { return Fx.StackOf(R) >= 2; });
	if (Row.IsNone())
	{
		AddWarning(TEXT("No DT_Items row with maxStack >= 2; skipping stacking test"));
		return true;
	}
	const int32 Max = Fx.StackOf(Row);

	TestTrue(TEXT("Add Max-1"), Fx.Inv->AddItem(Row, Max - 1));
	TestEqual(TEXT("one slot"), Fx.Inv->GetItems().Num(), 1);
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Stacking after Add Max-1")))
	{
		return false;
	}
	TestEqual(TEXT("qty Max-1"), Fx.Inv->GetItems()[0].Quantity, Max - 1);

	TestTrue(TEXT("Add 3 more"), Fx.Inv->AddItem(Row, 3));
	TestEqual(TEXT("spilled to second slot"), Fx.Inv->GetItems().Num(), 2);
	if (!NeedItems(*this, Fx.Inv, 2, TEXT("Stacking after Add 3 more")))
	{
		return false;
	}
	TestEqual(TEXT("first stack topped up to Max"), Fx.Inv->GetItems()[0].Quantity, Max);
	TestEqual(TEXT("second stack holds the rest"), Fx.Inv->GetItems()[1].Quantity, 2);
	TestEqual(TEXT("total"), TotalOfRow(Fx.Inv, Row), Max + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvAddRejectsTest, "MazeDemons.Inventory.AddItem.RejectsInvalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvAddRejectsTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Row = Fx.FindRow([](FName) { return true; });
	if (Row.IsNone())
	{
		AddError(TEXT("DT_Items has no rows"));
		return false;
	}
	// Known-good add first, so refusing everything cannot pass vacuously.
	if (!TestTrue(TEXT("known-good AddItem(Row,1) succeeds"), Fx.Inv->AddItem(Row, 1)))
	{
		return false;
	}
	TestFalse(TEXT("Qty 0"), Fx.Inv->AddItem(Row, 0));
	TestFalse(TEXT("Qty -5"), Fx.Inv->AddItem(Row, -5));
	TestFalse(TEXT("None row"), Fx.Inv->AddItem(NAME_None, 1));
	TestFalse(TEXT("Unknown row"), Fx.Inv->AddItem(FName(TEXT("MD_Test_NoSuchRow")), 1));
	TestEqual(TEXT("no state change (only the known-good unit)"), TotalOfRow(Fx.Inv, Row), 1);
	TestEqual(TEXT("no extra stacks"), Fx.Inv->GetItems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvAddFullTest, "MazeDemons.Inventory.AddItem.MaxSlotsAllOrNothing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvAddFullTest::RunTest(const FString& Parameters)
{
	{
		FMDInvFixture Fx;
		if (!Fx.Init(*this, 3))
		{
			return false;
		}
		const FName Row = Fx.FindRow([](FName) { return true; });
		if (Row.IsNone())
		{
			AddError(TEXT("DT_Items has no rows"));
			return false;
		}
		const int32 Max = Fx.StackOf(Row);

		// Known-good add first (then undo), so refusing everything cannot pass.
		if (!TestTrue(TEXT("known-good AddItem(Row,1) succeeds"), Fx.Inv->AddItem(Row, 1)) || !TestTrue(TEXT("undo known-good add"), Fx.Inv->RemoveItem(0, 1)))
		{
			return false;
		}

		// Does not fit at all: nothing may change.
		TestFalse(TEXT("3*Max+1 rejected"), Fx.Inv->AddItem(Row, Max * 3 + 1));
		TestEqual(TEXT("still empty"), Fx.Inv->GetItems().Num(), 0);

		// Exactly fills all slots.
		TestTrue(TEXT("3*Max fits"), Fx.Inv->AddItem(Row, Max * 3));
		TestEqual(TEXT("3 slots used"), Fx.Inv->GetItems().Num(), 3);
		for (const FMDItemStack& S : Fx.Inv->GetItems())
		{
			TestEqual(TEXT("full stack"), S.Quantity, Max);
		}
		TestFalse(TEXT("one more rejected"), Fx.Inv->AddItem(Row, 1));
		TestEqual(TEXT("still 3 slots"), Fx.Inv->GetItems().Num(), 3);
		TestEqual(TEXT("total unchanged"), TotalOfRow(Fx.Inv, Row), Max * 3);
	}
	{
		// Partially occupied: a request that only partly fits must be refused without touching existing stacks.
		FMDInvFixture Fx;
		if (!Fx.Init(*this, 3))
		{
			return false;
		}
		const FName Row = Fx.FindRow([](FName) { return true; });
		if (Row.IsNone())
		{
			AddError(TEXT("DT_Items has no rows"));
			return false;
		}
		const int32 Max = Fx.StackOf(Row);
		if (!TestTrue(TEXT("seed 1"), Fx.Inv->AddItem(Row, 1)))
		{
			return false;
		}
		TestFalse(TEXT("3*Max does not fit next to 1"), Fx.Inv->AddItem(Row, Max * 3));
		TestEqual(TEXT("seed untouched"), TotalOfRow(Fx.Inv, Row), 1);
		TestEqual(TEXT("slots untouched"), Fx.Inv->GetItems().Num(), 1);
	}
	return true;
}

// ---- RemoveItem / Gold ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvRemoveTest, "MazeDemons.Inventory.RemoveItem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvRemoveTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Row = Fx.FindRow([&Fx](FName R) { return Fx.StackOf(R) >= 2; });
	if (Row.IsNone())
	{
		AddWarning(TEXT("No DT_Items row with maxStack >= 2; skipping remove test"));
		return true;
	}
	const int32 Max = Fx.StackOf(Row);
	if (!TestTrue(TEXT("seed"), Fx.Inv->AddItem(Row, Max)) || !NeedItems(*this, Fx.Inv, 1, TEXT("Remove seed")))
	{
		return false;
	}

	TestFalse(TEXT("Qty 0"), Fx.Inv->RemoveItem(0, 0));
	TestFalse(TEXT("bad index"), Fx.Inv->RemoveItem(5, 1));
	TestFalse(TEXT("negative index"), Fx.Inv->RemoveItem(-1, 1));
	TestFalse(TEXT("more than held"), Fx.Inv->RemoveItem(0, Max + 1));
	TestEqual(TEXT("unchanged after rejects"), Fx.Inv->GetItems()[0].Quantity, Max);

	TestTrue(TEXT("partial"), Fx.Inv->RemoveItem(0, 1));
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Remove after partial")))
	{
		return false;
	}
	TestEqual(TEXT("qty Max-1"), Fx.Inv->GetItems()[0].Quantity, Max - 1);

	TestTrue(TEXT("full"), Fx.Inv->RemoveItem(0, Max - 1));
	TestEqual(TEXT("slot removed"), Fx.Inv->GetItems().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvGoldTest, "MazeDemons.Inventory.AddGold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvGoldTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	Fx.Inv->AddGold(50);
	TestEqual(TEXT("+50"), Fx.Inv->GetGold(), 50);
	Fx.Inv->AddGold(-20);
	TestEqual(TEXT("-20"), Fx.Inv->GetGold(), 30);
	Fx.Inv->AddGold(0);
	TestEqual(TEXT("0 is a no-op"), Fx.Inv->GetGold(), 30);
	Fx.Inv->AddGold(-1000);
	TestEqual(TEXT("clamped at 0"), Fx.Inv->GetGold(), 0);
	return true;
}

// ---- Equip ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvEquipBasicTest, "MazeDemons.Inventory.Equip.MovesOneUnitAndSwaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvEquipBasicTest::RunTest(const FString& Parameters)
{
	{
		// Moves exactly one unit out of a stack.
		FMDInvFixture Fx;
		if (!Fx.Init(*this))
		{
			return false;
		}
		const FName E = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) < TestGoldSlot && Fx.StackOf(R) >= 2; });
		if (E.IsNone())
		{
			AddWarning(TEXT("No equippable stackable row; skipping one-unit check"));
		}
		else
		{
			TestTrue(TEXT("add 2"), Fx.Inv->AddItem(E, 2));
			FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
			TestEqual(TEXT("one unit left in Items"), TotalOfRow(Fx.Inv, E), 1);
			const FMDEquippedSlot* Eq = FindEquipped(Fx.Inv, Fx.SlotOf(E));
			TestNotNull(TEXT("equipped entry"), Eq);
			if (Eq)
			{
				TestTrue(TEXT("equipped row"), Eq->ItemRowName == E);
			}
			TestEqual(TEXT("one equipped entry"), Fx.Inv->GetEquipped().Num(), 1);
		}
	}
	{
		// Swap: previous item goes back into Items.
		FMDInvFixture Fx;
		if (!Fx.Init(*this))
		{
			return false;
		}
		uint8 Slot = 0;
		FName A, B;
		if (!Fx.FindPair(Slot, A, B, false))
		{
			AddWarning(TEXT("No two rows share an equip slot; skipping swap check"));
		}
		else
		{
			TestTrue(TEXT("add A"), Fx.Inv->AddItem(A, 1));
			FMDInventoryTestAccess::ServerEquip(Fx.Inv, IndexOfRow(Fx.Inv, A));
			TestEqual(TEXT("Items empty after equipping A"), Fx.Inv->GetItems().Num(), 0);
			TestTrue(TEXT("add B"), Fx.Inv->AddItem(B, 1));
			FMDInventoryTestAccess::ServerEquip(Fx.Inv, IndexOfRow(Fx.Inv, B));
			const FMDEquippedSlot* Eq = FindEquipped(Fx.Inv, Slot);
			TestNotNull(TEXT("slot occupied"), Eq);
			if (Eq)
			{
				TestTrue(TEXT("B equipped"), Eq->ItemRowName == B);
			}
			TestEqual(TEXT("still one equipped entry"), Fx.Inv->GetEquipped().Num(), 1);
			TestEqual(TEXT("A swapped back"), TotalOfRow(Fx.Inv, A), 1);
			TestEqual(TEXT("B no longer in Items"), TotalOfRow(Fx.Inv, B), 0);
		}
	}
	{
		// Distinct slots: two-handed and chest each land in their own slot.
		FMDInvFixture Fx;
		if (!Fx.Init(*this))
		{
			return false;
		}
		for (const uint8 Slot : { TestMainHand, TestTwoHanded, TestChest })
		{
			const FName Row = Fx.FindRow([&Fx, Slot](FName R) { return Fx.SlotOf(R) == Slot; });
			if (Row.IsNone())
			{
				AddWarning(FString::Printf(TEXT("No DT_Items row for slot %d; skipping"), Slot));
				continue;
			}
			TestTrue(TEXT("add"), Fx.Inv->AddItem(Row, 1));
			FMDInventoryTestAccess::ServerEquip(Fx.Inv, IndexOfRow(Fx.Inv, Row));
			const FMDEquippedSlot* Eq = FindEquipped(Fx.Inv, Slot);
			TestNotNull(*FString::Printf(TEXT("slot %d equipped"), Slot), Eq);
			if (Eq)
			{
				TestTrue(*FString::Printf(TEXT("slot %d row"), Slot), Eq->ItemRowName == Row);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvEquipFullTest, "MazeDemons.Inventory.Equip.AbortsWhenSwapHasNoRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvEquipFullTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this, 1))
	{
		return false;
	}
	uint8 Slot = 0;
	FName A, B;
	if (!Fx.FindPair(Slot, A, B, true))
	{
		AddWarning(TEXT("No two rows share a slot with a stackable second row; skipping"));
		return true;
	}
	// Equip A (inventory has 1 slot, freed by equipping), then fill the single slot with 2x B.
	TestTrue(TEXT("add A"), Fx.Inv->AddItem(A, 1));
	FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
	TestTrue(TEXT("add 2x B"), Fx.Inv->AddItem(B, 2));
	TestEqual(TEXT("inventory full"), Fx.Inv->GetItems().Num(), 1);
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Equip-full before swap attempt")))
	{
		return false;
	}

	FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
	TestEqual(TEXT("Items unchanged"), Fx.Inv->GetItems().Num(), 1);
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Equip-full after swap attempt")))
	{
		return false;
	}
	TestEqual(TEXT("B still 2"), Fx.Inv->GetItems()[0].Quantity, 2);
	TestTrue(TEXT("Items row"), Fx.Inv->GetItems()[0].ItemRowName == B);
	const FMDEquippedSlot* Eq = FindEquipped(Fx.Inv, Slot);
	TestNotNull(TEXT("still equipped"), Eq);
	if (Eq)
	{
		TestTrue(TEXT("A still equipped"), Eq->ItemRowName == A);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvInvalidRequestsTest, "MazeDemons.Inventory.Requests.RejectInvalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvInvalidRequestsTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Row = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) < TestGoldSlot && Fx.StackOf(R) >= 2; });
	if (Row.IsNone())
	{
		AddWarning(TEXT("No equippable stackable row; skipping invalid request test"));
		return true;
	}
	if (!TestTrue(TEXT("seed"), Fx.Inv->AddItem(Row, 2)) || !NeedItems(*this, Fx.Inv, 1, TEXT("Requests seed")))
	{
		return false;
	}

	// RPC validators
	TestFalse(TEXT("Equip validate -1"), FMDInventoryTestAccess::ValidateEquip(Fx.Inv, -1));
	TestFalse(TEXT("Equip validate huge"), FMDInventoryTestAccess::ValidateEquip(Fx.Inv, 100000));
	TestTrue(TEXT("Equip validate 0"), FMDInventoryTestAccess::ValidateEquip(Fx.Inv, 0));
	TestFalse(TEXT("Drop validate qty 0"), FMDInventoryTestAccess::ValidateDrop(Fx.Inv, 0, 0));
	TestFalse(TEXT("Drop validate negative index"), FMDInventoryTestAccess::ValidateDrop(Fx.Inv, -1, 1));
	TestFalse(TEXT("Drop validate huge qty"), FMDInventoryTestAccess::ValidateDrop(Fx.Inv, 0, 100001));
	TestTrue(TEXT("Drop validate ok"), FMDInventoryTestAccess::ValidateDrop(Fx.Inv, 0, 1));
	TestFalse(TEXT("Unequip validate gold slot"), FMDInventoryTestAccess::ValidateUnequip(Fx.Inv, TestGoldSlot));
	TestTrue(TEXT("Unequip validate boots"), FMDInventoryTestAccess::ValidateUnequip(Fx.Inv, 8));

	// Bodies must also be a no-op for bad input (defence in depth if a validator is bypassed).
	FMDInventoryTestAccess::ServerEquip(Fx.Inv, 99);
	FMDInventoryTestAccess::ServerEquip(Fx.Inv, -1);
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, 99, 1);
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, -1, 1);
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, 0, 0);
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, 0, -3);
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, 0, 3); // more than held
	TestEqual(TEXT("Items unchanged"), Fx.Inv->GetItems().Num(), 1);
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Requests after invalid calls")))
	{
		return false;
	}
	TestEqual(TEXT("qty unchanged"), Fx.Inv->GetItems()[0].Quantity, 2);
	TestEqual(TEXT("nothing equipped"), Fx.Inv->GetEquipped().Num(), 0);

	// A valid drop does remove the units.
	FMDInventoryTestAccess::ServerDrop(Fx.Inv, 0, 1);
	if (!NeedItems(*this, Fx.Inv, 1, TEXT("Requests after valid drop")))
	{
		return false;
	}
	TestEqual(TEXT("valid drop removes 1"), Fx.Inv->GetItems()[0].Quantity, 1);

	// Gold is never equippable.
	const FName GoldRow = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) == TestGoldSlot; });
	if (GoldRow.IsNone())
	{
		AddWarning(TEXT("No gold (slot 9) row in DT_Items; skipping gold equip check"));
	}
	else
	{
		TestTrue(TEXT("add gold row"), Fx.Inv->AddItem(GoldRow, 1));
		const int32 Before = Fx.Inv->GetItems().Num();
		FMDInventoryTestAccess::ServerEquip(Fx.Inv, IndexOfRow(Fx.Inv, GoldRow));
		TestEqual(TEXT("gold stays in Items"), Fx.Inv->GetItems().Num(), Before);
		TestEqual(TEXT("gold not equipped"), Fx.Inv->GetEquipped().Num(), 0);
	}
	return true;
}

// ---- Unequip ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvUnequipTest, "MazeDemons.Inventory.Unequip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvUnequipTest::RunTest(const FString& Parameters)
{
	{
		FMDInvFixture Fx;
		if (!Fx.Init(*this))
		{
			return false;
		}
		const FName A = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) < TestGoldSlot; });
		if (A.IsNone())
		{
			AddWarning(TEXT("No equippable row; skipping unequip test"));
			return true;
		}
		const uint8 Slot = Fx.SlotOf(A);
		TestTrue(TEXT("add"), Fx.Inv->AddItem(A, 1));
		FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
		TestEqual(TEXT("equipped"), Fx.Inv->GetEquipped().Num(), 1);

		FMDInventoryTestAccess::ServerUnequip(Fx.Inv, static_cast<uint8>((Slot + 1) % TestGoldSlot));
		TestEqual(TEXT("wrong slot is a no-op"), Fx.Inv->GetEquipped().Num(), 1);

		FMDInventoryTestAccess::ServerUnequip(Fx.Inv, Slot);
		TestEqual(TEXT("unequipped"), Fx.Inv->GetEquipped().Num(), 0);
		TestEqual(TEXT("item returned"), TotalOfRow(Fx.Inv, A), 1);
	}
	{
		// Full inventory: unequip must abort and keep the item equipped.
		FMDInvFixture Fx;
		if (!Fx.Init(*this, 1))
		{
			return false;
		}
		const FName A = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) < TestGoldSlot; });
		if (A.IsNone())
		{
			AddWarning(TEXT("No equippable row; skipping full-inventory unequip check"));
			return true;
		}
		const FName Filler = Fx.FindRow([A](FName R) { return R != A; });
		if (Filler.IsNone())
		{
			AddWarning(TEXT("Need a second row for the full-inventory unequip check; skipping"));
			return true;
		}
		const uint8 Slot = Fx.SlotOf(A);
		TestTrue(TEXT("add A"), Fx.Inv->AddItem(A, 1));
		FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
		TestTrue(TEXT("fill the only slot"), Fx.Inv->AddItem(Filler, 1));

		FMDInventoryTestAccess::ServerUnequip(Fx.Inv, Slot);
		TestEqual(TEXT("still equipped"), Fx.Inv->GetEquipped().Num(), 1);
		TestEqual(TEXT("Items unchanged"), Fx.Inv->GetItems().Num(), 1);
		TestEqual(TEXT("filler kept"), TotalOfRow(Fx.Inv, Filler), 1);
	}
	return true;
}

// ---- Delegates ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvDelegatesTest, "MazeDemons.Inventory.Delegates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvDelegatesTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Row = Fx.FindRow([&Fx](FName R) { return Fx.SlotOf(R) < TestGoldSlot; });
	if (Row.IsNone())
	{
		AddWarning(TEXT("No equippable row; skipping delegate test"));
		return true;
	}
	UMDInventoryTestListener* L = NewObject<UMDInventoryTestListener>(GetTransientPackage());
	Fx.Inv->OnInventoryChanged.AddDynamic(L, &UMDInventoryTestListener::HandleInventoryChanged);
	Fx.Inv->OnEquipmentChanged.AddDynamic(L, &UMDInventoryTestListener::HandleEquipmentChanged);

	TestTrue(TEXT("add"), Fx.Inv->AddItem(Row, 2 > Fx.StackOf(Row) ? 1 : 2));
	TestEqual(TEXT("AddItem fires OnInventoryChanged"), L->InventoryCount, 1);

	TestFalse(TEXT("rejected add"), Fx.Inv->AddItem(NAME_None, 1));
	TestEqual(TEXT("rejected add is silent"), L->InventoryCount, 1);

	Fx.Inv->AddGold(10);
	TestEqual(TEXT("AddGold fires OnInventoryChanged"), L->InventoryCount, 2);
	Fx.Inv->AddGold(0);
	TestEqual(TEXT("AddGold(0) silent"), L->InventoryCount, 2);

	TestTrue(TEXT("remove"), Fx.Inv->RemoveItem(0, 1));
	TestEqual(TEXT("RemoveItem fires OnInventoryChanged"), L->InventoryCount, 3);
	TestFalse(TEXT("rejected remove"), Fx.Inv->RemoveItem(7, 1));
	TestEqual(TEXT("rejected remove is silent"), L->InventoryCount, 3);

	if (Fx.Inv->GetItems().Num() == 0)
	{
		TestTrue(TEXT("re-add"), Fx.Inv->AddItem(Row, 1));
	}
	const int32 InvBeforeEquip = L->InventoryCount;
	FMDInventoryTestAccess::ServerEquip(Fx.Inv, 0);
	TestEqual(TEXT("Equip fires OnEquipmentChanged"), L->EquipmentCount, 1);
	TestTrue(TEXT("Equip fires OnInventoryChanged"), L->InventoryCount > InvBeforeEquip);

	FMDInventoryTestAccess::ServerEquip(Fx.Inv, 50);
	TestEqual(TEXT("rejected equip is silent"), L->EquipmentCount, 1);

	const int32 InvBeforeUnequip = L->InventoryCount;
	FMDInventoryTestAccess::ServerUnequip(Fx.Inv, Fx.SlotOf(Row));
	TestEqual(TEXT("Unequip fires OnEquipmentChanged"), L->EquipmentCount, 2);
	TestTrue(TEXT("Unequip fires OnInventoryChanged"), L->InventoryCount > InvBeforeUnequip);

	Fx.Inv->OnInventoryChanged.RemoveAll(L);
	Fx.Inv->OnEquipmentChanged.RemoveAll(L);
	return true;
}

// ---- Pickup ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInvPickupTest, "MazeDemons.Inventory.Pickup.TryPickup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDInvPickupTest::RunTest(const FString& Parameters)
{
	FMDInvFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const FName Row = Fx.FindRow([](FName) { return true; });
	if (Row.IsNone())
	{
		AddError(TEXT("DT_Items has no rows"));
		return false;
	}
	const int32 Qty = FMath::Min(2, Fx.StackOf(Row));

	// Interactor without an inventory: pickup must stay in the world.
	AActor* Bystander = Fx.World->SpawnActor<AActor>();
	AMDPickupBase* Kept = Fx.World->SpawnActor<AMDPickupBase>();
	if (!Bystander || !Kept)
	{
		AddError(TEXT("Could not spawn test actors"));
		return false;
	}
	Kept->ItemRowName = Row;
	Kept->Quantity = Qty;
	TestFalse(TEXT("no inventory -> false"), Kept->TryPickup(Bystander));
	TestFalse(TEXT("not marked picked up"), Kept->bPickedUp);
	TestTrue(TEXT("still alive"), IsValid(Kept));
	TestFalse(TEXT("null interactor -> false"), Kept->TryPickup(nullptr));

	AMDPickupBase* Pickup = Fx.World->SpawnActor<AMDPickupBase>();
	if (!Pickup)
	{
		AddError(TEXT("Could not spawn pickup"));
		return false;
	}
	Pickup->ItemRowName = Row;
	Pickup->Quantity = Qty;
	TWeakObjectPtr<AMDPickupBase> WeakPickup(Pickup);

	TestTrue(TEXT("first pickup succeeds"), Pickup->TryPickup(Fx.Owner));
	TestEqual(TEXT("item added"), TotalOfRow(Fx.Inv, Row), Qty);
	TestTrue(TEXT("bPickedUp set"), Pickup->bPickedUp);
	TestFalse(TEXT("pickup destroyed"), WeakPickup.IsValid());

	// The destroyed actor's memory survives until GC, so the guard on bPickedUp can be exercised directly.
	TestFalse(TEXT("second pickup rejected"), Pickup->TryPickup(Fx.Owner));
	TestEqual(TEXT("no double add"), TotalOfRow(Fx.Inv, Row), Qty);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
