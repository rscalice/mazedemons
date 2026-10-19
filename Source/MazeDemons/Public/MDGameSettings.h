#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDInventoryTypes.h"
#include "MDPerkTypes.h"
#include "MDGameSettings.generated.h"

class UMDBalanceData;
class UMDPerkTree;

// Project Settings > Game > Maze Demons Game. Points at DA_BalanceReference; empty or missing falls back to built-in defaults.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Maze Demons Game"))
class MAZEDEMONS_API UMDGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TSoftObjectPtr<UMDBalanceData> BalanceData;

	// DA_PerkTree (optional): the 16 perk nodes and caps. Empty or missing falls back to UMDPerkTree's built-in defaults.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TSoftObjectPtr<UMDPerkTree> PerkTree;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// The balance asset, or nullptr when none is configured / it does not exist yet. A missing package is re-checked at most
	// every few seconds so per-hit callers never hit the disk.
	static const UMDBalanceData* GetBalanceData();

	// Economy tuning from the balance asset, else a default-constructed FMDEconomyTuning (BALANCE.md values).
	static const FMDEconomyTuning& GetEconomy();

	// Perk tuning from the balance asset, else FMDPerkTuning defaults.
	static const FMDPerkTuning& GetPerkTuning();

	// Reference enemy numbers from the balance asset, else defaults.
	static const FMDBalanceReference& GetBalanceReference();

	// The configured perk tree, else the class default object (which holds the BALANCE defaults). Never null.
	static const UMDPerkTree* GetPerkTree();

	// Blueprint access to the same tuning, returned by value (a copy of the asset values, or the built-in defaults).
	UFUNCTION(BlueprintPure, Category = "MazeDemons|Economy")
	static FMDEconomyTuning GetEconomyTuning();
};
