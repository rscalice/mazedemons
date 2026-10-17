#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDUISettings.generated.h"

class UMDUIStyle;
class UUserWidget;
class UDataTable;
class UInputMappingContext;

// Project Settings > Game > Maze Demons UI. Points at DA_MDUIStyle; empty falls back to the built-in defaults.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Maze Demons UI"))
class MAZEDEMONS_API UMDUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (AllowedClasses = "/Script/MazeDemons.MDUIStyle"))
	TSoftObjectPtr<UMDUIStyle> Style;

	// Widget the local PlayerController adds to the viewport at BeginPlay (survives pawn death).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (MetaClass = "/Script/MazeDemons.MDHUDWidget"))
	TSoftClassPtr<UUserWidget> HUDWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/UI/HUD/W_MDHUD.W_MDHUD_C")));

	// DT_ItemDisplay (FMDItemDisplayRow keyed like DT_Items). Empty/unloadable falls back to a split-CamelCase row name.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Items", meta = (RequiredAssetDataTags = "RowStructure=/Script/MazeDemons.MDItemDisplayRow"))
	TSoftObjectPtr<UDataTable> ItemDisplayTable;

	// Pause menu widget created by AMDPlayerController::TogglePauseMenu (never pauses the game).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSoftClassPtr<UUserWidget> PauseWidgetClass;

	// Optional extra mapping context for UI input.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> UIMappingContext;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
