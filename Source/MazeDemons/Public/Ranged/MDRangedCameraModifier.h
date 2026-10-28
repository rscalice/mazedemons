// R4 slice 1: over-the-shoulder camera for ranged mode. Pure blend rules + a UCameraModifier added to the local
// player's PlayerCameraManager, so it works on top of whatever the GASP rig / camera component produced.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "MDRangedCameraModifier.generated.h"

// Pure rules (no world, unit tested: MazeDemons.Ranged.Camera.*).
struct MAZEDEMONS_API FMDRangedCameraRules
{
	// Move Alpha toward Target (0 or 1) at 1/BlendSeconds per second; BlendSeconds <= 0 snaps.
	static float StepAlpha(float Alpha, float Target, float DeltaSeconds, float BlendSeconds);
	// Smoothstep so the camera eases in and out of the shoulder.
	static float Ease(float Alpha);
	// World-space offset: Right * RightCm * Side + Up * UpCm, scaled by the eased alpha. Side is clamped to -1 / +1 (0 -> +1).
	static FVector ComputeOffset(float Alpha, float Side, float RightCm, float UpCm, const FVector& RightAxis, const FVector& UpAxis);
	// Pull a desired camera spot in along Pivot->Desired when a probe hit at HitFraction (0..1) with a margin; no hit = unchanged.
	static FVector ApplyCollision(const FVector& Pivot, const FVector& Desired, bool bHit, float HitFraction);
};

UCLASS()
class MAZEDEMONS_API UMDRangedCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	float ShoulderAlpha = 0.0f;
	float AimAlpha = 0.0f;
};
