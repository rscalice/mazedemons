#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "MDUITypes.h"
#include "MDUIStyle.generated.h"

class UFont;

// The single source of truth for UI look and feel. Defaults are built in (so the UI is correct even before a
// DA_MDUIStyle asset exists); a data asset can override any token. All widgets read from here.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDUIStyle : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMDUIStyle();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TMap<EMDColor, FLinearColor> Colors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TMap<EMDText, FMDTextDef> Texts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FMDMotion Motion;

	// Optional font assets (Content/UI/Style/Fonts). Empty = engine default (Roboto).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fonts")
	TSoftObjectPtr<UFont> BodyFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fonts")
	TSoftObjectPtr<UFont> DisplayFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float PanelPadding = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float SlotGap = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float HudSafeMargin = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D ItemSlotSize = FVector2D(64.0, 64.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D QuickSlotSize = FVector2D(56.0, 56.0);

	// Restores every token to the values in UI_STYLE_GUIDE.md.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Style")
	void ResetToDefaults();

	UFUNCTION(BlueprintPure, Category = "Style")
	FLinearColor GetColor(EMDColor Token) const;

	UFUNCTION(BlueprintPure, Category = "Style")
	FLinearColor RarityColor(EMDRarity Rarity) const;

	UFUNCTION(BlueprintPure, Category = "Style")
	FMDTextDef GetTextDef(EMDText Token) const;

	FSlateFontInfo MakeFont(EMDText Token) const;

	// Sharp-cornered box brush: zero radius, hard stroke, no shadow.
	static FSlateBrush MakeBox(const FLinearColor& Fill, const FLinearColor& Stroke, float StrokeWidth);

	FButtonStyle MakeButtonStyle(EMDButton Kind) const;

	// WCAG relative-luminance contrast ratio (inputs are linear colours, as stored in Colors).
	UFUNCTION(BlueprintPure, Category = "Style")
	static float ContrastRatio(const FLinearColor& A, const FLinearColor& B);
};
