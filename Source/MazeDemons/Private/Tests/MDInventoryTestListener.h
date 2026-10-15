
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MDInventoryTestListener.generated.h"

// Test helper: counts inventory/equipment delegate broadcasts. Only used by Private/Tests/MDInventoryTests.cpp.
UCLASS(Transient)
class UMDInventoryTestListener : public UObject
{
	GENERATED_BODY()

public:
	int32 InventoryCount = 0;
	int32 EquipmentCount = 0;

	UFUNCTION()
	void HandleInventoryChanged() { ++InventoryCount; }

	UFUNCTION()
	void HandleEquipmentChanged() { ++EquipmentCount; }
};
