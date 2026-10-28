// R1: a minimal native reticle for ranged mode (no widget Blueprint): four ticks whose gap follows the current spread, and a hit
// marker (white X, red on a kill) that flashes when the server confirms a hit. R7 replaces it with the ProHUD reticle; until then it
// makes the spread, the bloom and the hit confirms visible. Local only; created by UMDRangedComponent on the owning machine.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MDRangedReticleWidget.generated.h"

class UMDRangedComponent;

UCLASS()
class MAZEDEMONS_API UMDRangedReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	void SetRanged(UMDRangedComponent* InRanged) { Ranged = InRanged; }

	// A hit was confirmed: flash the marker.
	UFUNCTION()
	void OnHitConfirmed(bool bKill, bool bHeadshot, FGameplayTag DamageType);

	// Pixels between the centre and the inner end of a tick for a spread half-angle (degrees) at the given vertical FOV and viewport height.
	static float SpreadToPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap = 6.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reticle")
	float TickLength = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reticle")
	FLinearColor Color = FLinearColor(0.88f, 0.86f, 0.80f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reticle")
	float HitMarkerSeconds = 0.25f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMDRangedComponent> Ranged;
	double HitMarkerStart = -1000.0;
	bool bMarkerKill = false;
	bool bMarkerHead = false;
};
