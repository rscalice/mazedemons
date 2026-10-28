// Ranged weapon UI + IK reach rules: gun -> equip slot 9 -> paper-doll Ranged box (not Boots), and the support-hand reach fade.
#include "Misc/AutomationTest.h"

#include "MDInventoryTypes.h"
#include "Ranged/MDRangedTypes.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedDollMappingTest, "MazeDemons.Ranged.UI.DollMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDRangedDollMappingTest::RunTest(const FString& Parameters)
{
	// C++ table: the Ranged item type resolves to equip slot 9 and to the Ranged doll box.
	TestEqual(TEXT("Ranged item type -> doll Ranged"), MDDoll::IndexForItemType(MDItemType::Ranged), MDDoll::Ranged);
	TestEqual(TEXT("Ranged equip slot 9 -> doll Ranged"), MDDoll::IndexForEquipSlot(MDItemType::RangedSlot), MDDoll::Ranged);
	TestEqual(TEXT("Ranged equip slot is 9"), (int32)MDItemType::RangedSlot, 9);
	TestNotEqual(TEXT("Ranged is not the Boots box"), MDDoll::IndexForEquipSlot(MDItemType::RangedSlot), MDDoll::Boots);
	TestEqual(TEXT("Boots stays Boots"), MDDoll::IndexForItemType(MDItemType::Boots), MDDoll::Boots);
	TestEqual(TEXT("Two-hander shows in main hand"), MDDoll::IndexForItemType(MDItemType::TwoHanded), MDDoll::MainHand);
	TestEqual(TEXT("Shield shows in off hand"), MDDoll::IndexForItemType(MDItemType::Shield), MDDoll::OffHand);
	TestEqual(TEXT("Gold has no box"), MDDoll::IndexForItemType(MDItemType::Gold), MDDoll::None);
	TestEqual(TEXT("Material has no box"), MDDoll::IndexForItemType(MDItemType::Material), MDDoll::None);
	TestTrue(TEXT("Ranged box is inside the doll"), MDDoll::Ranged < MDDoll::NumSlots);

	// Blueprint: AC_Inventory::DetermineEquipmentSlotIndex must agree with the table for every equip slot byte.
	UClass* Class = LoadClass<UObject>(nullptr, TEXT("/Game/Components/Inventory/AC_Inventory.AC_Inventory_C"));
	if (!TestNotNull(TEXT("AC_Inventory loads"), Class))
	{
		return false;
	}
	UFunction* Func = Class->FindFunctionByName(TEXT("DetermineEquipmentSlotIndex"));
	if (!TestNotNull(TEXT("DetermineEquipmentSlotIndex exists"), Func))
	{
		return false;
	}
	UObject* Object = NewObject<UObject>(GetTransientPackage(), Class);
	FProperty* In = nullptr;
	FProperty* Out = nullptr;
	for (TFieldIterator<FProperty> It(Func); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm) || (It->HasAnyPropertyFlags(CPF_OutParm) && !In && false))
		{
			Out = *It;
		}
		else if (It->HasAnyPropertyFlags(CPF_OutParm))
		{
			Out = Out ? Out : *It;
		}
		else if (It->HasAnyPropertyFlags(CPF_Parm) && !In)
		{
			In = *It;
		}
	}
	if (!TestTrue(TEXT("function has one input and one output"), In && Out))
	{
		return false;
	}
	for (uint8 Slot = 0; Slot < MDItemType::NumEquipSlots; ++Slot)
	{
		TArray<uint8> Params;
		Params.SetNumZeroed(Func->ParmsSize);
		Func->InitializeStruct(Params.GetData());
		if (const FByteProperty* Byte = CastField<FByteProperty>(In))
		{
			*Byte->ContainerPtrToValuePtr<uint8>(Params.GetData()) = Slot;
		}
		else if (const FEnumProperty* Enum = CastField<FEnumProperty>(In))
		{
			Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Params.GetData()), (int64)Slot);
		}
		Object->ProcessEvent(Func, Params.GetData());
		int64 Result = -1;
		if (const FIntProperty* Int = CastField<FIntProperty>(Out))
		{
			Result = Int->GetPropertyValue(Out->ContainerPtrToValuePtr<void>(Params.GetData()));
		}
		else if (const FByteProperty* OutByte = CastField<FByteProperty>(Out))
		{
			Result = OutByte->GetPropertyValue(Out->ContainerPtrToValuePtr<void>(Params.GetData()));
		}
		Func->DestroyStruct(Params.GetData());
		const uint8 Expected = MDDoll::IndexForEquipSlot(Slot);
		if (Expected != MDDoll::None)
		{
			TestEqual(*FString::Printf(TEXT("Blueprint doll index for equip slot %d"), Slot), Result, (int64)Expected);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedSupportReachTest, "MazeDemons.Ranged.Anim.SupportHandReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDRangedSupportReachTest::RunTest(const FString& Parameters)
{
	const float Arm = 50.0f;
	TestEqual(TEXT("close target: full"), FMDRangedRules::SupportHandReachAlpha(20.0f, Arm), 1.0f);
	TestEqual(TEXT("at 85% of the arm: still full"), FMDRangedRules::SupportHandReachAlpha(42.5f, Arm), 1.0f);
	TestEqual(TEXT("halfway through the fade"), FMDRangedRules::SupportHandReachAlpha(46.25f, Arm), 0.5f, 0.01f);
	TestEqual(TEXT("at full arm length: zero"), FMDRangedRules::SupportHandReachAlpha(50.0f, Arm), 0.0f);
	TestEqual(TEXT("beyond reach (the 1 m float): zero"), FMDRangedRules::SupportHandReachAlpha(100.0f, Arm), 0.0f);
	TestEqual(TEXT("invalid arm: zero"), FMDRangedRules::SupportHandReachAlpha(10.0f, 0.0f), 0.0f);
	TestEqual(TEXT("non-finite distance: zero"), FMDRangedRules::SupportHandReachAlpha(std::numeric_limits<float>::quiet_NaN(), Arm), 0.0f);
	float Previous = 1.0f;
	for (float Distance = 0.0f; Distance <= 80.0f; Distance += 1.0f)
	{
		const float Alpha = FMDRangedRules::SupportHandReachAlpha(Distance, Arm);
		TestTrue(TEXT("alpha never rises with distance"), Alpha <= Previous + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("alpha stays in 0..1"), Alpha >= 0.0f && Alpha <= 1.0f);
		Previous = Alpha;
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
