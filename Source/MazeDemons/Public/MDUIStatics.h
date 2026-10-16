#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDUITypes.h"
#include "MDUIStatics.generated.h"

class UMDUIStyle;

UCLASS()
class MAZEDEMONS_API UMDUIStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// The active style: the asset set in Project Settings, else the built-in defaults. Never null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static const UMDUIStyle* GetStyle();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static FLinearColor GetColor(EMDColor Token);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static FLinearColor RarityColor(EMDRarity Rarity);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static FMDMotion GetMotion();

	// Ease-out cubic, the only easing the style guide allows. T is clamped to 0..1.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static float EaseOutCubic(float T);
};
