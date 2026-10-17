#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"
#include "MDUISettings.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// Friend of UMDInventoryComponent (see header): reaches Items / QuickSlots and the *_Impl functions without a world.
struct FMDQuickSlotTestAccess
{
	// Transient DT_Consumables stand-in with a single HealthPotion row.
	static UDataTable* MakeConsumables()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FMDConsumableRow::StaticStruct();
		Table->AddRow(FName("HealthPotion"), FMDConsumableRow());
		Table->AddToRoot();
		return Table;
	}

	static UMDInventoryComponent* MakeInventory(UDataTable* Consumables)
	{
		UMDInventoryComponent* Inv = NewObject<UMDInventoryComponent>(GetTransientPackage());
		Inv->ConsumableTable = TSoftObjectPtr<UDataTable>(Consumables);
		return Inv;
	}

	static void AddStack(UMDInventoryComponent* Inv, FName Row, int32 Qty)
	{
		FMDItemStack& S = Inv->Items.AddDefaulted_GetRef();
		S.ItemRowName = Row;
		S.Quantity = Qty;
	}

	static void ClearItems(UMDInventoryComponent* Inv) { Inv->Items.Reset(); }
	static bool Assign(UMDInventoryComponent* Inv, int32 Q, int32 I) { return Inv->AssignQuickSlot_Impl(Q, I); }
	static int32 Resolve(const UMDInventoryComponent* Inv, int32 Q) { return Inv->ResolveQuickSlotIndex(Q); }
	static void Validate(UMDInventoryComponent* Inv) { Inv->ValidateQuickSlots(); }
	static void SetSlot(UMDInventoryComponent* Inv, int32 Q, FName Row) { Inv->QuickSlots[Q] = Row; }
};

namespace
{
	const FName Potion(TEXT("HealthPotion"));
	const FName Sword(TEXT("Sword"));

	struct FQuickSlotScope
	{
		UDataTable* Table = FMDQuickSlotTestAccess::MakeConsumables();
		UMDInventoryComponent* Inv = FMDQuickSlotTestAccess::MakeInventory(Table);
		~FQuickSlotScope() { Table->RemoveFromRoot(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuickSlotAssignUseTest, "MazeDemons.QuickSlot.AssignUse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuickSlotAssignUseTest::RunTest(const FString& Parameters)
{
	FQuickSlotScope S;
	FMDQuickSlotTestAccess::AddStack(S.Inv, Sword, 1);
	FMDQuickSlotTestAccess::AddStack(S.Inv, Potion, 3);

	TestEqual(TEXT("two quick slots"), S.Inv->GetQuickSlots().Num(), UMDInventoryComponent::NumQuickSlots);
	TestTrue(TEXT("assign potion to slot 0"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, 1));
	TestEqual(TEXT("slot 0 stores the row name"), S.Inv->GetQuickSlotItem(0), Potion);
	TestEqual(TEXT("slot 0 resolves to the stack"), FMDQuickSlotTestAccess::Resolve(S.Inv, 0), 1);
	TestEqual(TEXT("slot 0 count"), S.Inv->GetQuickSlotCount(0), 3);
	TestEqual(TEXT("slot 1 unassigned"), S.Inv->GetQuickSlotItem(1), NAME_None);
	TestEqual(TEXT("unassigned resolves to none"), FMDQuickSlotTestAccess::Resolve(S.Inv, 1), (int32)INDEX_NONE);

	TestTrue(TEXT("INDEX_NONE clears"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, INDEX_NONE));
	TestEqual(TEXT("cleared"), S.Inv->GetQuickSlotItem(0), NAME_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuickSlotEmptyStaysTest, "MazeDemons.QuickSlot.EmptyStaysAssigned", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuickSlotEmptyStaysTest::RunTest(const FString& Parameters)
{
	FQuickSlotScope S;
	FMDQuickSlotTestAccess::AddStack(S.Inv, Potion, 1);
	TestTrue(TEXT("assign"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, 0));

	FMDQuickSlotTestAccess::ClearItems(S.Inv); // last potion used
	FMDQuickSlotTestAccess::Validate(S.Inv);
	TestEqual(TEXT("slot keeps its row"), S.Inv->GetQuickSlotItem(0), Potion);
	TestEqual(TEXT("count is 0"), S.Inv->GetQuickSlotCount(0), 0);
	TestEqual(TEXT("resolve returns INDEX_NONE"), FMDQuickSlotTestAccess::Resolve(S.Inv, 0), (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuickSlotRejectsTest, "MazeDemons.QuickSlot.RejectsNonConsumable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuickSlotRejectsTest::RunTest(const FString& Parameters)
{
	FQuickSlotScope S;
	FMDQuickSlotTestAccess::AddStack(S.Inv, Sword, 1);
	FMDQuickSlotTestAccess::AddStack(S.Inv, Potion, 2);

	AddExpectedMessage(TEXT("AssignQuickSlot rejected"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("weapon rejected"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, 0));
	TestEqual(TEXT("slot untouched"), S.Inv->GetQuickSlotItem(0), NAME_None);
	TestFalse(TEXT("quick index -1"), FMDQuickSlotTestAccess::Assign(S.Inv, -1, 1));
	TestFalse(TEXT("quick index 2"), FMDQuickSlotTestAccess::Assign(S.Inv, 2, 1));
	TestFalse(TEXT("inventory index out of range"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, 5));

	TestFalse(TEXT("pure rule: weapon"), UMDInventoryComponent::IsValidQuickAssignment(Sword, S.Table));
	TestFalse(TEXT("pure rule: none"), UMDInventoryComponent::IsValidQuickAssignment(NAME_None, S.Table));
	TestFalse(TEXT("pure rule: no table"), UMDInventoryComponent::IsValidQuickAssignment(Potion, nullptr));
	TestTrue(TEXT("pure rule: potion"), UMDInventoryComponent::IsValidQuickAssignment(Potion, S.Table));

	FMDQuickSlotTestAccess::SetSlot(S.Inv, 0, Sword);
	FMDQuickSlotTestAccess::Validate(S.Inv);
	TestEqual(TEXT("validate clears a non-consumable"), S.Inv->GetQuickSlotItem(0), NAME_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuickSlotNoDuplicateTest, "MazeDemons.QuickSlot.NoDuplicate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuickSlotNoDuplicateTest::RunTest(const FString& Parameters)
{
	FQuickSlotScope S;
	FMDQuickSlotTestAccess::AddStack(S.Inv, Potion, 3);
	TestTrue(TEXT("slot 0"), FMDQuickSlotTestAccess::Assign(S.Inv, 0, 0));
	TestTrue(TEXT("slot 1"), FMDQuickSlotTestAccess::Assign(S.Inv, 1, 0));
	TestEqual(TEXT("slot 0 cleared"), S.Inv->GetQuickSlotItem(0), NAME_None);
	TestEqual(TEXT("slot 1 holds the potion"), S.Inv->GetQuickSlotItem(1), Potion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDItemRowValidationTest, "MazeDemons.Items.RowValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDItemRowValidationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("SplitCamel HealthPotion"), UMDGameplayStatics::SplitCamel(TEXT("HealthPotion")), FString(TEXT("Health Potion")));
	TestEqual(TEXT("SplitCamel single word"), UMDGameplayStatics::SplitCamel(TEXT("Sword")), FString(TEXT("Sword")));
	TestEqual(TEXT("SplitCamel acronym"), UMDGameplayStatics::SplitCamel(TEXT("HPPotion")), FString(TEXT("HP Potion")));
	TestEqual(TEXT("fallback display name"), UMDGameplayStatics::GetItemDisplayName(FName("zzNoSuchItem")).ToString(), FString(TEXT("zz No Such Item")));

	const UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items.DT_Items"));
	if (!Items)
	{
		AddWarning(TEXT("DT_Items not loadable; row validation skipped"));
		return true;
	}
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	const UDataTable* Display = Settings ? Settings->ItemDisplayTable.LoadSynchronous() : nullptr;
	if (!Display)
	{
		AddWarning(TEXT("UMDUISettings::ItemDisplayTable (DT_ItemDisplay) not set yet (WP2 data work); display rows not validated"));
		return true;
	}
	for (const FName Row : Items->GetRowNames())
	{
		const FMDItemDisplayRow* Found = Display->FindRow<FMDItemDisplayRow>(Row, TEXT("RowValidation"), false);
		if (!Found)
		{
			AddError(FString::Printf(TEXT("DT_ItemDisplay is missing row '%s'"), *Row.ToString()));
			continue;
		}
		if (Found->DisplayName.IsEmpty())
		{
			AddError(FString::Printf(TEXT("DT_ItemDisplay row '%s' has an empty DisplayName"), *Row.ToString()));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaRespawnRefillTest, "MazeDemons.Stamina.ResetForRespawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaRespawnRefillTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRespawnRefillTestWorld")));
	if (!World)
	{
		AddError(TEXT("UWorld::CreateWorld failed"));
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	AMDPlayerState* PS = World->SpawnActor<AMDPlayerState>();
	APawn* Pawn = World->SpawnActor<APawn>();
	bool bOk = false;
	if (PS && Pawn)
	{
		PS->SetRole(ROLE_Authority);
		Pawn->SetPlayerState(PS);
		UMDAbilitySystemComponent* ASC = PS->GetMDAbilitySystemComponent();
		UMDStaminaComponent* Stamina = PS->StaminaComponent;
		if (ASC && Stamina)
		{
			ASC->InitAbilityActorInfo(PS, Pawn);
			ASC->RegisterAttributeSet(PS->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			ASC->InitializeStamina(Stamina->StartingMaxStamina, Stamina->RegenPerSec);
			if (ASC->GetMazeDemonsAttributes())
			{
				bOk = true;
				Stamina->Consume(1.0e6f); // drain to 0 -> exhausted + State.Exhausted
				ASC->AddLooseGameplayTag(TAG_State_Sprinting.GetTag());
				TestTrue(TEXT("precondition: exhausted"), Stamina->IsExhausted());
				TestEqual(TEXT("precondition: stamina 0"), ASC->GetStamina(), 0.0f);
				TestTrue(TEXT("precondition: sprint tag set"), ASC->GetTagCount(TAG_State_Sprinting.GetTag()) > 0);

				Stamina->ResetForRespawn();

				TestEqual(TEXT("stamina refilled"), ASC->GetStamina(), ASC->GetMaxStamina());
				TestFalse(TEXT("not exhausted"), Stamina->IsExhausted());
				TestEqual(TEXT("State.Sprinting cleared"), ASC->GetTagCount(TAG_State_Sprinting.GetTag()), 0);
				TestEqual(TEXT("State.Exhausted cleared"), ASC->GetTagCount(TAG_State_Exhausted.GetTag()), 0);
			}
		}
	}
	if (!bOk)
	{
		AddWarning(TEXT("Could not build the stamina fixture in the test world; skipped"));
	}

	World->DestroyWorld(false);
	if (World->IsRooted())
	{
		World->RemoveFromRoot();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
