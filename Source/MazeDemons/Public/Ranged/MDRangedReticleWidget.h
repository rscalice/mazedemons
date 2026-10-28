// R1 + R7: the centre reticle of ranged mode (local, created by UMDRangedComponent on the owning machine while a gun is held in ranged
// mode). Four ticks whose gap follows the current spread (hip wide, aimed tight; the shown gap animates so bloom is visible), a muted
// off-white colour, sharp corners. Hit marker: white X, red on a kill, element-coloured on a typed hit, plus an extra tick above it on a
// headshot. LockOnSlot is a centred overlay the WP7 lock-on diamond nests into. Size comes from UMDLocalSettings::ReticleScale.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MDRangedReticleWidget.generated.h"

class UMDRangedComponent;
class UPanelWidget;

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
	// (Forwards to FMDRangedUIRules::ReticleGapPixels.)
	static float SpreadToPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap = 6.0f);

	// A centred overlay for children that belong at the reticle: the WP7 lock-on diamond goes here.
	UFUNCTION(BlueprintPure, Category = "Reticle")
	UPanelWidget* GetLockOnSlot() const { return LockOnSlot; }

	// The gap the next paint uses (pixels) and the colour / flags of the running hit marker (tests).
	float GetDisplayGap() const { return DisplayGap; }
	FLinearColor GetMarkerColour() const { return MarkerColour; }
	bool IsMarkerHeadshot() const { return bMarkerHead; }
	bool IsMarkerActive() const;

	// Advances the animated gap toward the real spread (also what NativeTick does). Callable directly (tests).
	void UpdateGap(float DeltaSeconds, float TargetGapPixels);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reticle")
	float TickLength = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reticle")
	float HitMarkerSeconds = 0.25f;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float ComputeTargetGap(const FVector2D& ViewportSize) const;

	TWeakObjectPtr<UMDRangedComponent> Ranged;

	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> LockOnSlot;

	double HitMarkerStart = -1000.0;
	FLinearColor MarkerColour = FLinearColor::White;
	bool bMarkerHead = false;
	float DisplayGap = -1.0f; // < 0 = not placed yet (the first frame jumps to the target)
};
