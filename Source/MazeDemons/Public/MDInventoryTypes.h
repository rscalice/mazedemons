#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPtr.h"
#include "MDInventoryTypes.generated.h"

class UMDComboSet;
class UMDRangedWeaponData;

// Why a consumable use was refused (server decides, owning client shows a toast).
UENUM(BlueprintType)
enum class EMDUseRejectReason : uint8
{
	None,
	HealthFull,
	OnCooldown,
	Dead,
	EmptySlot
};

// Row of DT_Consumables (same row names as DT_Items). An item with a row here is usable (RequestUseItem) and never equippable.
// Kept in C++ so consumables need no change to the Blueprint enum E_ItemTypes / struct S_ItemInfo.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDConsumableRow : public FTableRowBase
{
	GENERATED_BODY()

	// Health restored on use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable", meta = (ClampMin = "0"))
	float HealAmount = 35.0f;

	// Seconds before the owner can use any consumable again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable", meta = (ClampMin = "0"))
	float CooldownSeconds = 1.0f;
};

// Row of DT_ItemStats (same row names as DT_Items): C++-side combat stats for an item. Kept in C++ because S_ItemInfo cannot be
// edited via tools. A missing table or row means "no bonus".
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDItemStatsRow : public FTableRowBase
{
	GENERATED_BODY()

	// Added to the wielder's Attack Power while this weapon is equipped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float AttackPowerBonus = 0.0f;

	// Gold value of the item; repair cost scales with it. 0 = free to repair.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 ItemValue = 0;

	// Maximum durability of an equipped instance. 0 = the item has no durability (never wears, may stack).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 MaxDurability = 0;

	// True if a station can upgrade the slot while this item is equipped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool bUpgradeable = false;

	// C1: the weapon's combo data (DA_Combo_*). Empty = the weapon uses the old DoLightAttack montage array. Set on equip
	// (UMDInventoryComponent::GetItemComboSet -> UMDComboComponent::SetComboSet).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSoftObjectPtr<UMDComboSet> ComboSet;

	// C1 dual wield: used instead of ComboSet when the same weapon type is equipped in both hands (main + off-hand slot).
	// Empty = no dual set: the main-hand ComboSet is used as normal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSoftObjectPtr<UMDComboSet> DualComboSet;

	// R0: the gun's data (DA_Ranged_*). Set only on E_ItemTypes::Ranged rows; empty on every other item.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ranged")
	TSoftObjectPtr<UMDRangedWeaponData> RangedData;
};

// Row of DT_ItemDisplay (same row names as DT_Items): player-facing text. Kept in C++ because S_ItemInfo cannot be edited via tools.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDItemDisplayRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText Description;
};

// E_ItemTypes byte values (the Blueprint enum is append-only). Consumable / Material are never equipped.
namespace MDItemType
{
	constexpr uint8 MainHand = 0;
	constexpr uint8 OffHand = 1;
	constexpr uint8 Shield = 2;
	constexpr uint8 TwoHanded = 3;
	constexpr uint8 Helmet = 4;
	constexpr uint8 Chest = 5;
	constexpr uint8 Gloves = 6;
	constexpr uint8 Legs = 7;
	constexpr uint8 Boots = 8;
	constexpr uint8 Gold = 9;
	constexpr uint8 Consumable = 10;
	constexpr uint8 Material = 11;
	// R0: E_ItemTypes::Ranged (appended, ordinal 12). Its equip slot is RangedSlot, NOT 12, because the equip slot byte indexes
	// the Equipped array and slot 9 is free there (Gold 9 is a currency row type, never an equip slot).
	constexpr uint8 Ranged = 12;
	constexpr uint8 RangedSlot = 9;
	constexpr uint8 NumEquipSlots = 10;
}

// Durability value for an item without durability.
constexpr int16 MD_NoDurability = -1;

// Why a station service (repair / upgrade) was refused. Server decides; the UI greys buttons with the same query.
UENUM(BlueprintType)
enum class EMDStationReject : uint8
{
	None,
	NoStation,
	OutOfRange,
	Dead,
	EmptySlot,
	NotUpgradeable,
	MaxLevel,
	FullDurability,
	NotEnoughGold,
	NotEnoughShards
};

// Gold, upgrade and durability tuning. Defaults are the BALANCE.md values so tests and a missing DA_BalanceReference need no asset.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDEconomyTuning
{
	GENERATED_BODY()

	// Gold and Shards to go from level N to N+1 (index N). MaxUpgradeLevel decides how many entries are used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TArray<int32> UpgradeGold = { 50, 100, 200 };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TArray<int32> UpgradeShards = { 1, 2, 3 };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (ClampMin = "0"))
	int32 MaxUpgradeLevel = 3;

	// Percentage points of damage reduction added per upgrade level of an armor slot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	float ArmorDRPerLevel = 3.0f;

	// Attack power multiplier added per upgrade level of the main weapon (0.06 = +6%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	float WeaponAPPctPerLevel = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability", meta = (ClampMin = "1"))
	int32 DefaultMaxDurability = 100;

	// At or above this durability fraction an item is at full strength.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability", meta = (ClampMin = "0.01", ClampMax = "1"))
	float FullStrengthAbove = 0.25f;

	// Effectiveness multiplier at 0 durability (lerps up to 1 at FullStrengthAbove).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability", meta = (ClampMin = "0", ClampMax = "1"))
	float ConditionFloor = 0.5f;

	// Fraction of max durability lost by every equipped item on death.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability", meta = (ClampMin = "0", ClampMax = "1"))
	float DeathDurabilityLossPct = 0.10f;

	// Gold per missing durability point per item value point (0.01 = 1%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "0"))
	float RepairRate = 0.01f;

	// Repair cost multiplier added per upgrade level (0.25 = +25%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair", meta = (ClampMin = "0"))
	float RepairUpgradeScale = 0.25f;

	// Players within this distance (cm) of a dying enemy share its gold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gold", meta = (ClampMin = "0"))
	float KillGoldRadius = 3000.0f;

	// Max distance (cm) between a player and a station for a service request.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station", meta = (ClampMin = "0"))
	float StationRange = 400.0f;

	// Damage reduction cap for players (enemies and dummies keep the ASC default of 90).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense", meta = (ClampMin = "0", ClampMax = "90"))
	float PlayerDRCap = 65.0f;

	// DT_Items row of the upgrade currency.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FName ShardRow = FName(TEXT("UpgradeShard"));
};

// One stack in a player's inventory. ItemRowName is a row in /Game/Blueprints/Items/DT_Items.
// DT_Items rows carry no per-instance data (no durability / rolled stats), so row name + quantity is enough.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Quantity = 1;

	// Remaining durability of this instance, or MD_NoDurability when the row has none. Rows with MaxDurability > 0 never stack.
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int16 Durability = MD_NoDurability;
};

// Durability of the item equipped in Slot. Owner-only (separate from FMDEquippedSlot, which replicates to everyone for visuals).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDSlotCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	uint8 Slot = 0;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int16 Durability = MD_NoDurability;
};

// Upgrade level of an equipment slot (per slot, not per item). Owner-only.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDSlotUpgrade
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	uint8 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	uint8 Level = 0;
};

// One equipped item. Slot is the numeric value of the Blueprint enum E_ItemTypes:
// 0 MainHand, 1 OffHand, 2 Shield, 3 TwoHanded, 4 Helmet, 5 Chest, 6 Gloves, 7 Legs, 8 Boots, 9 Gold.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDEquippedSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	uint8 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemRowName = NAME_None;
};
