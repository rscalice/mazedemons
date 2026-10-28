#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "MDThreatIndicator.generated.h"

class AMDEnemyCharacter;

// Off-screen threat indicator (v1-design 1.3, V1 WP4): an enemy that holds an attack token on the local player and is not on
// the player's screen gets a screen-edge arrow, and a localized sting plays as it starts its swing (the wind-up, about 0.35 -
// 0.8 s before the hit). Option: md.UI.OffscreenThreat 0 turns both off.
namespace MDThreat
{
	// Where the arrow for an enemy at RelYawDeg (0 = straight ahead of the camera, +90 = to the right, 180 = behind) sits on a
	// ViewSize screen, Margin pixels inside the edge: ahead is the top edge, behind the bottom edge. OutRotationRad is the
	// direction the arrow points (0 = right, +pi/2 = down, in screen space).
	MAZEDEMONS_API void ArrowForYaw(float RelYawDeg, const FVector2D& ViewSize, float Margin, FVector2D& OutPosition, float& OutRotationRad);
}

// One arrow to draw.
struct FMDThreatArrow
{
	float RelYawDeg = 0.0f;
	float Alpha = 1.0f;
};

// Full-screen, hit-test-invisible widget that paints the arrows (no Blueprint: it has no children, so it needs no layout).
UCLASS()
class MAZEDEMONS_API UMDThreatIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetArrows(const TArray<FMDThreatArrow>& InArrows) { Arrows = InArrows; }
	const TArray<FMDThreatArrow>& GetArrows() const { return Arrows; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FMDThreatArrow> Arrows;
};

// Client side (any machine with a local player): finds the enemies that threaten the local player, decides which are off
// screen, feeds the widget and plays the sting. Reads only replicated state (AMDEnemyCharacter::bThreatening / ThreatTarget /
// ReplicatedIntent), so it works the same on a client and on the listen host.
UCLASS()
class MAZEDEMONS_API UMDThreatIndicatorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// md.UI.OffscreenThreat != 0 (default on)
	static bool IsEnabled();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// The arrows of the last tick (tests / debugging).
	const TArray<FMDThreatArrow>& GetArrows() const { return LastArrows; }

private:
	bool IsOnScreen(const APlayerController* PC, const AActor* Enemy, const FVector2D& ViewSize) const;

	UPROPERTY(Transient)
	TObjectPtr<UMDThreatIndicatorWidget> Widget;

	TArray<FMDThreatArrow> LastArrows;
	TMap<TWeakObjectPtr<const AMDEnemyCharacter>, uint8> LastIntent;
};
