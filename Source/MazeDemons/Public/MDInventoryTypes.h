#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MDInventoryTypes.generated.h"

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
