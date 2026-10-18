#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDInventoryTypes.h"
#include "MDGameSettings.generated.h"

class UMDBalanceData;

// Project Settings > Game > Maze Demons Game. Points at DA_BalanceReference; empty or missing falls back to built-in defaults.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Maze Demons Game"))
class MAZEDEMONS_API UMDGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TSoftObjectPtr<UMDBalanceData> BalanceData;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// The balance asset, or nullptr when none is configured / it does not exist yet. A missing package is re-checked at most
	// every few seconds so per-hit callers never hit the disk.
	static const UMDBalanceData* GetBalanceData();

	// Economy tuning from the balance asset, else a default-constructed FMDEconomyTuning (BALANCE.md values).
	static const FMDEconomyTuning& GetEconomy();
};
