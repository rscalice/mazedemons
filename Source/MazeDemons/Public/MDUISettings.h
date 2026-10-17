#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDUISettings.generated.h"

class UMDUIStyle;
class UUserWidget;

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

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
