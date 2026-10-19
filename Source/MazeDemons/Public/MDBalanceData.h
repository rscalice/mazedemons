#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MDInventoryTypes.h"
#include "MDBalanceData.generated.h"

// Central balance numbers (asset /Game/Data/DA_BalanceReference, referenced from Project Settings > Game > Maze Demons Game).
// The values mirror BALANCE.md; nothing here is read by Blueprint logic, only by C++.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDBalanceData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player Start", meta = (ClampMin = "1"))
	float StartingMaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player Start", meta = (ClampMin = "0"))
	float StartingAttackPower = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player Start", meta = (ClampMin = "0", ClampMax = "90"))
	float StartingBaseDefense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	FMDEconomyTuning Economy;
};
