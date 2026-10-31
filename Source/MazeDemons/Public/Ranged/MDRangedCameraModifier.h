// R4: over-the-shoulder camera for ranged mode. Pure blend rules + a UCameraModifier added to the local player's PlayerCameraManager,
// so it works on top of whatever the GASP rig / camera component produced. Slice 1: shoulder shift + wall pull-in. Slice 2: aim preset
// (shift, FOV, shorter arm, pitch clamp) blended in 0.18 s, and the collision pass that keeps the shifted camera out of walls.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Ranged/MDAimRules.h"
#include "MDRangedCameraModifier.generated.h"

class UWorld;
class AActor;

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

	// The presets the cvars (md.Cam.*) currently describe. Hip = ranged mode, not aiming (the shoulder shift). Aimed = holding aim.
	static FMDCameraPreset ConfiguredHipPreset();
	static FMDCameraPreset ConfiguredAimedPreset(FName WeaponPresetName = NAME_None);
	// Seconds the aim preset takes to blend in / out (md.Cam.AimBlend, default FMDAimRules::AimBlendSeconds).
	static float ConfiguredAimBlendSeconds();

	// World pass (hip and aimed): 1) the rig's own camera spot is probed from Pivot with a small sphere, so a rig that clips a wall is
	// pulled in; 2) the shoulder Offset is swept from that spot, so it shrinks when a wall is beside the character; 3) the final spot
	// is probed from Pivot again so a pillar between the chest and the shifted camera still pulls it in. Ignore = the player's pawn.
	// Returns the camera location; OutPulledIn says whether any stage moved it.
	static FVector ResolveCollision(const UWorld* World, const AActor* Ignore, const FVector& Pivot, const FVector& RigLocation,
		const FVector& Offset, bool* OutPulledIn = nullptr);
};

UCLASS()
class MAZEDEMONS_API UMDRangedCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	float GetShoulderAlpha() const { return ShoulderAlpha; }
	float GetAimAlpha() const { return AimAlpha; }

private:
	void ApplyPitchLimit(float Limit);

	float ShoulderAlpha = 0.0f;
	float AimAlpha = 0.0f;
	float DefaultPitchMin = -89.9f;
	float DefaultPitchMax = 89.9f;
	bool bPitchDefaultsSaved = false;
};
