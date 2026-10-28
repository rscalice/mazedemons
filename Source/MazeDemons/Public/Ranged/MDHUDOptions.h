// R7: the ranged / HUD options. There is no options menu widget in the project yet (W_OptionsMenu was deleted in WP3), so these are the
// setters an options screen calls: each clamps, stores into UMDLocalSettings, saves GameUserSettings.ini and applies the value.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDHUDOptions.generated.h"

UCLASS()
class MAZEDEMONS_API UMDHUDOptions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void SetAimAssistStrength(float Strength01);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void SetAimToggle(bool bToggle);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void SetReticleScale(float Scale);

	// Also pushed to the server by the ranged component on its next tick (the server decides auto-reload).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void SetAutoReload(bool bAuto);

	// Drives md.UI.OffscreenThreat.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void SetOffscreenThreat(bool bEnabled);

	// Pushes the saved values that live in console variables (the off-screen threat toggle). Cheap; the HUD calls it on construct.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Options")
	static void ApplySavedOptions();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Options")
	static float GetReticleScale();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Options")
	static bool GetAutoReload();
};
