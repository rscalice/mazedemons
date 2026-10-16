#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDUISettings.generated.h"

class UMDUIStyle;

// Project Settings > Game > Maze Demons UI. Points at DA_MDUIStyle; empty falls back to the built-in defaults.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Maze Demons UI"))
class MAZEDEMONS_API UMDUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (AllowedClasses = "/Script/MazeDemons.MDUIStyle"))
	TSoftObjectPtr<UMDUIStyle> Style;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
