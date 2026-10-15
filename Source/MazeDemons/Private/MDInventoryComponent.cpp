#include "MDInventoryComponent.h"

#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogMDInventory);

namespace
{
	constexpr uint8 InvalidSlot = 255;
	constexpr uint8 GoldSlot = 9; // E_ItemTypes::Gold, never equippable
	constexpr int32 MaxRequestQuantity = 100000;

	// BP struct properties are named "<name>_<index>_<GUID>", so match by prefix.
	const FProperty* FindRowProperty(const UScriptStruct* Struct, const TCHAR* Prefix)
	{
		if (!Struct)
		{
			return nullptr;
		}
		const FString Needle = FString(Prefix) + TEXT("_");
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetName().StartsWith(Needle, ESearchCase::IgnoreCase) || It->GetName().Equals(Prefix, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

UMDInventoryComponent::UMDInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMDInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UMDInventoryComponent, Items, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDInventoryComponent, GoldAmount, COND_OwnerOnly);
	DOREPLIFETIME(UMDInventoryComponent, Equipped);
}

// ---- Row data ----

bool UMDInventoryComponent::IsValidRow(FName Row) const
{
	if (Row.IsNone())
	{
		return false;
	}
	// If no table is assigned we cannot validate; accept and let BP rules decide.
	return !ItemTable || ItemTable->FindRowUnchecked(Row) != nullptr;
}

uint8 UMDInventoryComponent::GetSlotForItem_Implementation(FName ItemRowName) const
{
	if (!ItemTable)
	{
		return InvalidSlot;
	}
	const uint8* RowData = ItemTable->FindRowUnchecked(ItemRowName);
	const FProperty* Prop = FindRowProperty(ItemTable->GetRowStruct(), TEXT("itemType"));
	if (!RowData || !Prop)
	{
		return InvalidSlot;
	}
	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		return ByteProp->GetPropertyValue(ValuePtr);
	}
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		return static_cast<uint8>(EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr));
	}
	return InvalidSlot;
}

int32 UMDInventoryComponent::GetMaxStackForItem_Implementation(FName ItemRowName) const
{
	if (!ItemTable)
	{
		return 1;
	}
	const uint8* RowData = ItemTable->FindRowUnchecked(ItemRowName);
	const FIntProperty* Prop = CastField<FIntProperty>(FindRowProperty(ItemTable->GetRowStruct(), TEXT("maxStack")));
	if (!RowData || !Prop)
	{
		return 1;
	}
	return FMath::Max(1, Prop->GetPropertyValue_InContainer(RowData));
}

// ---- Authority API ----

bool UMDInventoryComponent::HasRoomForItem(FName Row, int32 Qty) const
{
	const int32 MaxStack = GetMaxStackForItem(Row);
	int32 Remaining = Qty;
	for (const FMDItemStack& Stack : Items)
	{
		if (Stack.ItemRowName == Row)
		{
			Remaining -= FMath::Max(0, MaxStack - Stack.Quantity);
		}
	}
	if (Remaining <= 0)
	{
		return true;
	}
	const int32 FreeSlots = MaxSlots - Items.Num();
	return FreeSlots > 0 && static_cast<int64>(FreeSlots) * static_cast<int64>(MaxStack) >= static_cast<int64>(Remaining);
}

bool UMDInventoryComponent::AddItem(FName ItemRowName, int32 Quantity)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Quantity <= 0 || !IsValidRow(ItemRowName))
	{
		return false;
	}
	// All-or-nothing: refuse if the whole quantity does not fit.
	if (!HasRoomForItem(ItemRowName, Quantity))
	{
		return false;
	}

	const int32 MaxStack = GetMaxStackForItem(ItemRowName);
	int32 Remaining = Quantity;

	for (FMDItemStack& Stack : Items)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Stack.ItemRowName == ItemRowName && Stack.Quantity < MaxStack)
		{
			const int32 Moved = FMath::Min(MaxStack - Stack.Quantity, Remaining);
			Stack.Quantity += Moved;
			Remaining -= Moved;
		}
	}
	while (Remaining > 0)
	{
		FMDItemStack& NewStack = Items.AddDefaulted_GetRef();
		NewStack.ItemRowName = ItemRowName;
		NewStack.Quantity = FMath::Min(MaxStack, Remaining);
		Remaining -= NewStack.Quantity;
	}

	OnInventoryChanged.Broadcast();
	return true;
}

bool UMDInventoryComponent::RemoveItem(int32 Index, int32 Quantity)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Items.IsValidIndex(Index) || Quantity <= 0 || Items[Index].Quantity < Quantity)
	{
		return false;
	}
	Items[Index].Quantity -= Quantity;
	if (Items[Index].Quantity <= 0)
	{
		Items.RemoveAt(Index);
	}
	OnInventoryChanged.Broadcast();
	return true;
}

void UMDInventoryComponent::AddGold(int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount == 0)
	{
		return;
	}
	GoldAmount = FMath::Max(0, GoldAmount + Amount);
	OnInventoryChanged.Broadcast();
}

// ---- Requests ----

void UMDInventoryComponent::RequestEquip(int32 Index)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EquipItem_Impl(Index);
	}
	else
	{
		Server_EquipItem(Index);
	}
}

void UMDInventoryComponent::RequestUnequip(uint8 Slot)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		UnequipSlot_Impl(Slot);
	}
	else
	{
		Server_UnequipSlot(Slot);
	}
}

void UMDInventoryComponent::RequestDrop(int32 Index, int32 Quantity)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		DropItem_Impl(Index, Quantity);
	}
	else
	{
		Server_DropItem(Index, Quantity);
	}
}

bool UMDInventoryComponent::Server_EquipItem_Validate(int32 Index)
{
	return Index >= 0 && Index < 1024;
}

void UMDInventoryComponent::Server_EquipItem_Implementation(int32 Index)
{
	EquipItem_Impl(Index);
}

bool UMDInventoryComponent::Server_UnequipSlot_Validate(uint8 Slot)
{
	return Slot < GoldSlot;
}

void UMDInventoryComponent::Server_UnequipSlot_Implementation(uint8 Slot)
{
	UnequipSlot_Impl(Slot);
}

bool UMDInventoryComponent::Server_DropItem_Validate(int32 Index, int32 Quantity)
{
	return Index >= 0 && Index < 1024 && Quantity > 0 && Quantity <= MaxRequestQuantity;
}

void UMDInventoryComponent::Server_DropItem_Implementation(int32 Index, int32 Quantity)
{
	DropItem_Impl(Index, Quantity);
}

// ---- Implementations (server only) ----

void UMDInventoryComponent::EquipItem_Impl(int32 Index)
{
	if (!Items.IsValidIndex(Index))
	{
		return;
	}
	const FName NewRow = Items[Index].ItemRowName;
	if (!IsValidRow(NewRow))
	{
		return;
	}
	const uint8 Slot = GetSlotForItem(NewRow);
	if (Slot >= GoldSlot)
	{
		return;
	}

	FMDEquippedSlot* Existing = Equipped.FindByPredicate([Slot](const FMDEquippedSlot& E) { return E.Slot == Slot; });
	const FName PreviousRow = Existing ? Existing->ItemRowName : NAME_None;

	// Take one unit out of the stack.
	const bool bStackEmptied = Items[Index].Quantity <= 1;
	if (!PreviousRow.IsNone() && !bStackEmptied && !HasRoomForItem(PreviousRow, 1))
	{
		return; // no room to swap the old item back in
	}
	if (bStackEmptied)
	{
		Items.RemoveAt(Index);
	}
	else
	{
		Items[Index].Quantity -= 1;
	}

	if (Existing)
	{
		Existing->ItemRowName = NewRow;
	}
	else
	{
		FMDEquippedSlot& NewEntry = Equipped.AddDefaulted_GetRef();
		NewEntry.Slot = Slot;
		NewEntry.ItemRowName = NewRow;
	}

	if (!PreviousRow.IsNone())
	{
		AddItem(PreviousRow, 1); // room guaranteed above
	}

	OnInventoryChanged.Broadcast();
	OnEquipmentChanged.Broadcast();
	K2_ApplyEquipment(Slot, NewRow);
}

void UMDInventoryComponent::UnequipSlot_Impl(uint8 Slot)
{
	const int32 EqIdx = Equipped.IndexOfByPredicate([Slot](const FMDEquippedSlot& E) { return E.Slot == Slot; });
	if (EqIdx == INDEX_NONE)
	{
		return;
	}
	const FName Row = Equipped[EqIdx].ItemRowName;
	if (!AddItem(Row, 1))
	{
		return; // inventory full, keep it equipped
	}
	Equipped.RemoveAt(EqIdx);
	OnEquipmentChanged.Broadcast();
	K2_ClearEquipment(Slot);
}

void UMDInventoryComponent::DropItem_Impl(int32 Index, int32 Quantity)
{
	if (!Items.IsValidIndex(Index) || Quantity <= 0 || Items[Index].Quantity < Quantity)
	{
		return;
	}
	const FName Row = Items[Index].ItemRowName;
	if (!IsValidRow(Row))
	{
		return;
	}

	FVector Location = FVector::ZeroVector;
	if (const AActor* Owner = GetOwner())
	{
		// Owner may be a controller/PlayerState; prefer the possessed pawn's location when available.
		Location = Owner->GetActorLocation();
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			Location += Pawn->GetActorForwardVector() * DropDistance;
		}
		else if (const AController* Controller = Cast<AController>(Owner))
		{
			if (const APawn* P = Controller->GetPawn())
			{
				Location = P->GetActorLocation() + P->GetActorForwardVector() * DropDistance;
			}
		}
	}

	if (RemoveItem(Index, Quantity))
	{
		K2_SpawnDrop(Row, Quantity, Location);
	}
}

// ---- OnReps ----

void UMDInventoryComponent::OnRep_Items()
{
	OnInventoryChanged.Broadcast();
}

void UMDInventoryComponent::OnRep_Gold()
{
	OnInventoryChanged.Broadcast();
}

void UMDInventoryComponent::OnRep_Equipped()
{
	OnEquipmentChanged.Broadcast();
}
