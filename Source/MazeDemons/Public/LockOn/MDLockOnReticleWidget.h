#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDLockOnReticleWidget.generated.h"

// The lock-on reticle (v1-design 5.1): a sharp diamond over the locked target, a thin ring for its poise, a red flash while it winds up
// (bThreatening), small ticks over the two targets a cycle would pick, and a short "snap-off" animation when the lock breaks.
// One full-screen, hit-test-invisible widget that the lock-on component creates once and then only updates in place (never removed and
// re-added; the same rule as the ProHUD world markers). No Blueprint: it has no children, so it needs no layout.
UCLASS()
class MAZEDEMONS_API UMDLockOnReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Locked: the diamond sits at TargetPixel. PoiseFraction < 0 hides the ring. Neighbour pixels with bHas* false are not drawn.
	void SetLocked(const FVector2D& TargetPixel, float PoiseFraction, bool bThreatening, float Scale);
	void SetNeighbours(bool bHasLeft, const FVector2D& LeftPixel, bool bHasRight, const FVector2D& RightPixel);
	// The lock broke: play the snap-off animation at the last position, then hide.
	void BeginSnapOff();
	// No lock and no animation running.
	void SetHidden();

	bool IsShowing() const { return bLocked || SnapOffAlpha > 0.0f; }
	bool IsLockedState() const { return bLocked; }
	FVector2D GetPixel() const { return Pixel; }

	// Seconds the snap-off takes.
	static constexpr float SnapOffSeconds = 0.18f;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bLocked = false;
	FVector2D Pixel = FVector2D::ZeroVector;
	float Poise = -1.0f;
	bool bThreat = false;
	float ReticleScale = 1.0f;
	bool bHasLeft = false;
	bool bHasRight = false;
	FVector2D LeftPixel = FVector2D::ZeroVector;
	FVector2D RightPixel = FVector2D::ZeroVector;
	float SnapOffAlpha = 0.0f; // 1 -> 0 over SnapOffSeconds
};
