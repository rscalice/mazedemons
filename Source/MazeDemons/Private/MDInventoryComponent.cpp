#include "MDInventoryComponent.h"

#include "AbilitySystemInterface.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDPlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogMDInventory);

namespace
{
	constexpr uint8 InvalidSlot = 255;
	constexpr uint8 GoldSlot = 9; // E_ItemTypes::Gold, never equippable
	constexpr int32 MaxRequestQuantity = 100000;

	// Armor slot -> name used by the ASC (matches the names BP_Player already uses).
	FName ArmorSlotName(uint8 Slot)
	{
		switch (Slot)
		{
		case 4: return FName("Helmet");
		case 5: return FName("Chest");
		case 6: return FName("Gloves");
		case 7: return FName("Legs");
		case 8: return FName("Boots");
		default: return NAME_None;
		}
	}

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

	// Reads a numeric / byte / enum column of a DT_Items row (matched by prefix). False if the table, row or column is missing.
	bool ReadRowNumber(const UDataTable* Table, FName Row, const TCHAR* Prefix, double& OutValue)
	{
		if (!Table || Row.IsNone())
		{
			return false;
		}
		const uint8* RowData = Table->FindRowUnchecked(Row);
		const FProperty* Prop = FindRowProperty(Table->GetRowStruct(), Prefix);
		if (!RowData || !Prop)
		{
			return false;
		}
		const void* Ptr = Prop->ContainerPtrToValuePtr<void>(RowData);
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			OutValue = static_cast<double>(EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr));
			return true;
		}
		if (const FNumericProperty* Num = CastField<FNumericProperty>(Prop)) // includes FByteProperty
		{
			OutValue = Num->IsFloatingPoint() ? Num->GetFloatingPointPropertyValue(Ptr) : static_cast<double>(Num->GetSignedIntPropertyValue(Ptr));
			return true;
		}
		return false;
	}

	// Weapon slot (E_ItemTypes value) -> name used by the ASC's attack bonus map. NAME_None = no attack bonus.
	FName WeaponSlotName(uint8 Slot)
	{
		switch (Slot)
		{
		case 0: return FName("MainHand");
		case 1: return FName("OffHand");
		case 3: return FName("TwoHanded");
		default: return NAME_None;
		}
	}
}

UMDInventoryComponent::UMDInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	QuickSlots.Init(NAME_None, NumQuickSlots);
}

void UMDInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UMDInventoryComponent, Items, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDInventoryComponent, GoldAmount, COND_OwnerOnly);
	DOREPLIFETIME(UMDInventoryComponent, Equipped);
	DOREPLIFETIME_CONDITION(UMDInventoryComponent, QuickSlots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDInventoryComponent, ConsumableReadyAtServer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDInventoryComponent, ConsumableCooldownDuration, COND_OwnerOnly);
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
	if (!ItemTable || IsConsumable(ItemRowName))
	{
		return InvalidSlot; // consumables are used, never equipped
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

// ---- Owner resolution / armor reduction (server) ----

APawn* UMDInventoryComponent::GetOwnerPawn() const
{
	AActor* Owner = GetOwner();
	if (APawn* Pawn = Cast<APawn>(Owner))
	{
		return Pawn;
	}
	if (const APlayerState* PS = Cast<APlayerState>(Owner))
	{
		return PS->GetPawn();
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller->GetPawn();
	}
	return nullptr;
}

void UMDInventoryComponent::ReapplyAllEquipment()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// Copy: BP handlers may touch the inventory while we iterate.
	const TArray<FMDEquippedSlot> Snapshot = Equipped;
	for (const FMDEquippedSlot& Entry : Snapshot)
	{
		ApplyArmorReduction(Entry.Slot, Entry.ItemRowName);
		ApplyWeaponBonus(Entry.Slot, Entry.ItemRowName);
		K2_ApplyEquipment(Entry.Slot, Entry.ItemRowName);
	}
	OnEquipmentChanged.Broadcast();
}

UMDAbilitySystemComponent* UMDInventoryComponent::ResolveASC() const
{
	// The ASC lives on the PlayerState for players; an owner that is itself a PlayerState (or has its own ASC)
	// is checked directly, otherwise go through the owner pawn's PlayerState.
	UAbilitySystemComponent* BaseASC = nullptr;
	if (const IAbilitySystemInterface* Iface = Cast<IAbilitySystemInterface>(GetOwner()))
	{
		BaseASC = Iface->GetAbilitySystemComponent();
	}
	if (!BaseASC)
	{
		if (const APawn* Pawn = GetOwnerPawn())
		{
			if (const IAbilitySystemInterface* PS = Cast<IAbilitySystemInterface>(Pawn->GetPlayerState()))
			{
				BaseASC = PS->GetAbilitySystemComponent();
			}
		}
	}
	return Cast<UMDAbilitySystemComponent>(BaseASC);
}

// ---- Consumables ----

const FMDConsumableRow* UMDInventoryComponent::FindConsumable(FName Row) const
{
	if (Row.IsNone())
	{
		return nullptr;
	}
	const UDataTable* Table = ConsumableTable.LoadSynchronous();
	return Table ? Table->FindRow<FMDConsumableRow>(Row, TEXT("FindConsumable"), false) : nullptr;
}

bool UMDInventoryComponent::IsConsumable(FName ItemRowName) const
{
	return FindConsumable(ItemRowName) != nullptr;
}

float UMDInventoryComponent::GetConsumableHeal(FName ItemRowName) const
{
	const FMDConsumableRow* Row = FindConsumable(ItemRowName);
	return Row ? Row->HealAmount : 0.0f;
}

bool UMDInventoryComponent::CanUseHeal(float Health, float MaxHealth, bool bDead, double Now, double ReadyAt)
{
	return !bDead && MaxHealth > 0.0f && Health < MaxHealth && Now >= ReadyAt;
}

EMDUseRejectReason UMDInventoryComponent::GetUseRejectReason(float Health, float MaxHealth, bool bDead, double Now, double ReadyAt)
{
	if (bDead)
	{
		return EMDUseRejectReason::Dead;
	}
	if (Now < ReadyAt)
	{
		return EMDUseRejectReason::OnCooldown;
	}
	if (!(MaxHealth > 0.0f) || Health >= MaxHealth)
	{
		return EMDUseRejectReason::HealthFull;
	}
	return EMDUseRejectReason::None;
}

FText UMDInventoryComponent::GetUseRejectText(EMDUseRejectReason Reason, float CooldownRemaining)
{
	switch (Reason)
	{
	case EMDUseRejectReason::HealthFull:
		return NSLOCTEXT("MDInventory", "UseRejectHealthFull", "Health is full");
	case EMDUseRejectReason::OnCooldown:
		return FText::Format(NSLOCTEXT("MDInventory", "UseRejectCooldown", "Potion on cooldown ({0}s)"),
			FText::AsNumber(FMath::Max(1, FMath::CeilToInt(CooldownRemaining))));
	case EMDUseRejectReason::Dead:
		return NSLOCTEXT("MDInventory", "UseRejectDead", "Cannot use while dead");
	case EMDUseRejectReason::EmptySlot:
		return NSLOCTEXT("MDInventory", "UseRejectEmptySlot", "Nothing in slot");
	default:
		return FText::GetEmpty();
	}
}

void UMDInventoryComponent::NotifyUseRejected(EMDUseRejectReason Reason, float CooldownRemaining) const
{
	if (const APawn* Pawn = GetOwnerPawn())
	{
		if (AMDPlayerController* PC = Cast<AMDPlayerController>(Pawn->GetController()))
		{
			PC->NotifyUseRejected(Reason, CooldownRemaining);
		}
	}
}

void UMDInventoryComponent::RequestUseItem(int32 Index)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		UseItem_Impl(Index);
	}
	else
	{
		Server_UseItem(Index);
	}
}

bool UMDInventoryComponent::Server_UseItem_Validate(int32 Index)
{
	return Index >= 0 && Index < 1024;
}

void UMDInventoryComponent::Server_UseItem_Implementation(int32 Index)
{
	UseItem_Impl(Index);
}

bool UMDInventoryComponent::UseItem_Impl(int32 Index)
{
	if (!Items.IsValidIndex(Index))
	{
		return false;
	}
	const FMDConsumableRow* Row = FindConsumable(Items[Index].ItemRowName);
	UMDAbilitySystemComponent* ASC = ResolveASC();
	const UWorld* World = GetWorld();
	if (!Row || !ASC || !World)
	{
		return false;
	}
	const double Now = GetServerNow(); // same clock the owning client reads the cooldown against
	if (!CanUseHeal(ASC->GetHealth(), ASC->GetMaxHealth(), ASC->IsDead(), Now, ConsumableReadyAt))
	{
		NotifyUseRejected(GetUseRejectReason(ASC->GetHealth(), ASC->GetMaxHealth(), ASC->IsDead(), Now, ConsumableReadyAt),
			static_cast<float>(FMath::Max(0.0, ConsumableReadyAt - Now)));
		return false;
	}
	ConsumableReadyAt = Now + Row->CooldownSeconds;
	ConsumableReadyAtServer = static_cast<float>(ConsumableReadyAt);
	ConsumableCooldownDuration = Row->CooldownSeconds;
	OnQuickSlotsChanged.Broadcast(); // the server-side owner (listen server) has no OnRep
	ASC->ApplyHealing(Row->HealAmount);
	RemoveItem(Index, 1);
	return true;
}

// ---- Quick slots ----

bool UMDInventoryComponent::IsValidQuickAssignment(FName Row, const UDataTable* InConsumableTable)
{
	return !Row.IsNone() && InConsumableTable && InConsumableTable->FindRowUnchecked(Row) != nullptr;
}

FName UMDInventoryComponent::GetQuickSlotItem(int32 QuickIndex) const
{
	return QuickSlots.IsValidIndex(QuickIndex) ? QuickSlots[QuickIndex] : NAME_None;
}

int32 UMDInventoryComponent::GetQuickSlotCount(int32 QuickIndex) const
{
	const FName Row = GetQuickSlotItem(QuickIndex);
	int32 Total = 0;
	if (!Row.IsNone())
	{
		for (const FMDItemStack& Stack : Items)
		{
			if (Stack.ItemRowName == Row)
			{
				Total += Stack.Quantity;
			}
		}
	}
	return Total;
}

int32 UMDInventoryComponent::ResolveQuickSlotIndex(int32 QuickIndex) const
{
	const FName Row = GetQuickSlotItem(QuickIndex);
	if (Row.IsNone())
	{
		return INDEX_NONE;
	}
	return Items.IndexOfByPredicate([Row](const FMDItemStack& S) { return S.ItemRowName == Row && S.Quantity > 0; });
}

void UMDInventoryComponent::RequestAssignQuickSlot(int32 QuickIndex, int32 InventoryIndex)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		AssignQuickSlot_Impl(QuickIndex, InventoryIndex);
	}
	else
	{
		Server_AssignQuickSlot(QuickIndex, InventoryIndex);
	}
}

void UMDInventoryComponent::RequestUseQuickSlot(int32 QuickIndex)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		UseQuickSlot_Impl(QuickIndex);
	}
	else
	{
		Server_UseQuickSlot(QuickIndex);
	}
}

bool UMDInventoryComponent::Server_AssignQuickSlot_Validate(int32 QuickIndex, int32 InventoryIndex)
{
	return QuickIndex >= 0 && QuickIndex < NumQuickSlots && InventoryIndex >= INDEX_NONE && InventoryIndex < 1024;
}

void UMDInventoryComponent::Server_AssignQuickSlot_Implementation(int32 QuickIndex, int32 InventoryIndex)
{
	AssignQuickSlot_Impl(QuickIndex, InventoryIndex);
}

bool UMDInventoryComponent::Server_UseQuickSlot_Validate(int32 QuickIndex)
{
	return QuickIndex >= 0 && QuickIndex < NumQuickSlots;
}

void UMDInventoryComponent::Server_UseQuickSlot_Implementation(int32 QuickIndex)
{
	UseQuickSlot_Impl(QuickIndex);
}

bool UMDInventoryComponent::AssignQuickSlot_Impl(int32 QuickIndex, int32 InventoryIndex)
{
	if (QuickIndex < 0 || QuickIndex >= NumQuickSlots)
	{
		return false;
	}
	if (QuickSlots.Num() != NumQuickSlots)
	{
		QuickSlots.SetNum(NumQuickSlots);
	}
	if (InventoryIndex == INDEX_NONE)
	{
		QuickSlots[QuickIndex] = NAME_None;
		OnQuickSlotsChanged.Broadcast();
		return true;
	}
	if (!Items.IsValidIndex(InventoryIndex))
	{
		return false;
	}
	const FName Row = Items[InventoryIndex].ItemRowName;
	if (!IsValidQuickAssignment(Row, ConsumableTable.LoadSynchronous()))
	{
		UE_LOG(LogMDInventory, Warning, TEXT("AssignQuickSlot rejected: '%s' is not a consumable."), *Row.ToString());
		return false;
	}
	for (int32 i = 0; i < QuickSlots.Num(); ++i)
	{
		if (i != QuickIndex && QuickSlots[i] == Row)
		{
			QuickSlots[i] = NAME_None; // an item lives in one quick slot at a time
		}
	}
	QuickSlots[QuickIndex] = Row;
	OnQuickSlotsChanged.Broadcast();
	return true;
}

bool UMDInventoryComponent::UseQuickSlot_Impl(int32 QuickIndex)
{
	const int32 Index = ResolveQuickSlotIndex(QuickIndex);
	if (Index == INDEX_NONE)
	{
		NotifyUseRejected(EMDUseRejectReason::EmptySlot, 0.0f);
		return false;
	}
	return UseItem_Impl(Index); // keeps CanUseHeal + cooldown
}

void UMDInventoryComponent::ValidateQuickSlots()
{
	bool bChanged = false;
	const UDataTable* Table = ConsumableTable.LoadSynchronous();
	for (FName& Row : QuickSlots)
	{
		if (!Row.IsNone() && !IsValidQuickAssignment(Row, Table))
		{
			Row = NAME_None;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		OnQuickSlotsChanged.Broadcast();
	}
}

void UMDInventoryComponent::OnRep_QuickSlots()
{
	OnQuickSlotsChanged.Broadcast();
}

void UMDInventoryComponent::ApplyArmorReduction(uint8 Slot, FName Row)
{
	const FName SlotName = ArmorSlotName(Slot);
	if (SlotName.IsNone() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAbilitySystemComponent* ASC = ResolveASC();
	if (!ASC)
	{
		return;
	}

	const float Percent = GetItemArmorReduction(Row);
	if (Percent > 0.0f)
	{
		ASC->SetArmorPieceDamageReduction(SlotName, Percent);
	}
	else
	{
		ASC->RemoveArmorPieceDamageReduction(SlotName);
	}
}

void UMDInventoryComponent::ApplyWeaponBonus(uint8 Slot, FName Row)
{
	const FName SlotName = WeaponSlotName(Slot);
	if (SlotName.IsNone() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAbilitySystemComponent* ASC = ResolveASC();
	if (!ASC)
	{
		return;
	}

	const float Bonus = GetItemAttackBonus(Row);
	if (Bonus != 0.0f)
	{
		ASC->SetWeaponAttackBonus(SlotName, Bonus);
	}
	else
	{
		ASC->RemoveWeaponAttackBonus(SlotName);
	}
}

// ---- Item reads ----

EMDRarity UMDInventoryComponent::RarityFromByte(int32 Value)
{
	return (Value >= 0 && Value <= static_cast<int32>(EMDRarity::Legendary)) ? static_cast<EMDRarity>(Value) : EMDRarity::Common;
}

EMDRarity UMDInventoryComponent::GetItemRarity(FName ItemRowName) const
{
	double Value = 0.0;
	if (!ReadRowNumber(ItemTable, ItemRowName, TEXT("rarity"), Value))
	{
		return EMDRarity::Common;
	}
	return RarityFromByte(static_cast<int32>(Value));
}

float UMDInventoryComponent::GetItemArmorReduction(FName ItemRowName) const
{
	double Value = 0.0;
	if (!ReadRowNumber(ItemTable, ItemRowName, TEXT("armorDamageReductionPercent"), Value))
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, static_cast<float>(Value));
}

const FMDItemStatsRow* UMDInventoryComponent::FindItemStats(FName Row) const
{
	if (Row.IsNone() || bItemStatsMissing)
	{
		return nullptr;
	}
	// DT_ItemStats is created later in the editor; do not trigger a failed load every call when it does not exist yet.
	if (!ItemStatsTable.IsValid() && !FPackageName::DoesPackageExist(ItemStatsTable.ToSoftObjectPath().GetLongPackageName()))
	{
		bItemStatsMissing = true;
		return nullptr;
	}
	const UDataTable* Table = ItemStatsTable.LoadSynchronous();
	return Table ? Table->FindRow<FMDItemStatsRow>(Row, TEXT("FindItemStats"), false) : nullptr;
}

float UMDInventoryComponent::GetItemAttackBonus(FName ItemRowName) const
{
	const FMDItemStatsRow* Stats = FindItemStats(ItemRowName);
	return Stats ? Stats->AttackPowerBonus : 0.0f;
}

// ---- Consumable cooldown ----

double UMDInventoryComponent::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

float UMDInventoryComponent::CooldownFraction(double Now, double ReadyAt, float Duration)
{
	if (Duration <= 0.0f)
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Clamp((ReadyAt - Now) / static_cast<double>(Duration), 0.0, 1.0));
}

float UMDInventoryComponent::GetConsumableCooldownRemaining() const
{
	return static_cast<float>(FMath::Max(0.0, static_cast<double>(ConsumableReadyAtServer) - GetServerNow()));
}

float UMDInventoryComponent::GetConsumableCooldownFraction() const
{
	return CooldownFraction(GetServerNow(), ConsumableReadyAtServer, ConsumableCooldownDuration);
}

void UMDInventoryComponent::OnRep_ConsumableCooldown()
{
	OnQuickSlotsChanged.Broadcast();
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
	OnQuickSlotsChanged.Broadcast(); // counts changed
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
	ValidateQuickSlots();
	OnInventoryChanged.Broadcast();
	OnQuickSlotsChanged.Broadcast(); // counts changed
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

void UMDInventoryComponent::RequestDestroy(int32 Index, int32 Quantity)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RemoveItem(Index, Quantity);
	}
	else
	{
		Server_DestroyItem(Index, Quantity);
	}
}

bool UMDInventoryComponent::Server_DestroyItem_Validate(int32 Index, int32 Quantity)
{
	return Index >= 0 && Index < 1024 && Quantity > 0 && Quantity <= MaxRequestQuantity;
}

void UMDInventoryComponent::Server_DestroyItem_Implementation(int32 Index, int32 Quantity)
{
	RemoveItem(Index, Quantity);
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
	ApplyArmorReduction(Slot, NewRow);
	ApplyWeaponBonus(Slot, NewRow);
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
	ApplyArmorReduction(Slot, NAME_None);
	ApplyWeaponBonus(Slot, NAME_None);
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
	if (const APawn* Pawn = GetOwnerPawn())
	{
		Location = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * DropDistance;
	}
	else if (const AActor* Owner = GetOwner())
	{
		if (Owner->IsA<APlayerState>() || Owner->IsA<AController>())
		{
			// Player-owned inventory with no pawn (dead / between lives): nothing to drop next to.
			UE_LOG(LogMDInventory, Warning, TEXT("Drop rejected: %s has no pawn."), *GetNameSafe(Owner));
			return;
		}
		Location = Owner->GetActorLocation();
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
	OnQuickSlotsChanged.Broadcast();
}

void UMDInventoryComponent::OnRep_Gold()
{
	OnInventoryChanged.Broadcast();
}

void UMDInventoryComponent::OnRep_Equipped()
{
	// Clients: weapon/helmet visuals (slots 0..4) are spawned locally from the replicated list, so every machine
	// sees them attached to the wielder. Armor meshes arrive via the PlayerState (ApplyArmorVisuals).
	for (const FMDEquippedSlot& Old : PrevEquipped)
	{
		if (Old.Slot > 4) { continue; }
		const FMDEquippedSlot* Now = Equipped.FindByPredicate([&Old](const FMDEquippedSlot& E) { return E.Slot == Old.Slot; });
		if (!Now || Now->ItemRowName != Old.ItemRowName)
		{
			K2_ClearEquipment(Old.Slot);
		}
	}
	for (const FMDEquippedSlot& Now : Equipped)
	{
		if (Now.Slot > 4) { continue; }
		const FMDEquippedSlot* Old = PrevEquipped.FindByPredicate([&Now](const FMDEquippedSlot& E) { return E.Slot == Now.Slot; });
		if (!Old || Old->ItemRowName != Now.ItemRowName)
		{
			K2_ApplyEquipment(Now.Slot, Now.ItemRowName);
		}
	}
	PrevEquipped = Equipped;
	OnEquipmentChanged.Broadcast();
}
