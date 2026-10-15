#pragma once

#include "CoreMinimal.h"
#include "MDInventoryTypes.generated.h"

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
