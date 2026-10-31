// R4: the pure rules of aiming (no world, no UObject state; unit tested as MazeDemons.Ranged.Aim.* and .Camera.PresetBlendValues).
// Aim state gates, the camera presets and their blend, the aim offset layer, and the aim assist (friction + magnetism) maths.
#pragma once

#include "CoreMinimal.h"

// One camera look: where the shoulder camera sits and how it is framed. Hip = ranged mode, not aiming; Aimed = holding aim.
struct FMDCameraPreset
{
	float RightCm = 0.0f;   // shift to the shoulder side (before the shoulder side sign)
	float UpCm = 0.0f;
	float FovDelta = 0.0f;  // degrees added to the rig's FOV (negative zooms in)
	float MaxArmCm = 0.0f;  // longest allowed pivot -> camera distance; 0 = leave the rig's arm alone
	float PitchLimit = 89.9f; // |pitch| clamp in degrees

	bool operator==(const FMDCameraPreset& Other) const
	{
		return FMath::IsNearlyEqual(RightCm, Other.RightCm) && FMath::IsNearlyEqual(UpCm, Other.UpCm) && FMath::IsNearlyEqual(FovDelta, Other.FovDelta)
			&& FMath::IsNearlyEqual(MaxArmCm, Other.MaxArmCm) && FMath::IsNearlyEqual(PitchLimit, Other.PitchLimit);
	}
};

// What an input device gets from the aim assist (ranged-design 7: gamepad magnetism only, mouse friction only; nothing ever snaps).
struct FMDAimAssistKind
{
	bool bFriction = false;
	bool bMagnetism = false;
};

struct MAZEDEMONS_API FMDAimRules
{
	// ---- Aim state ----
	// Aim blend length for the camera preset (ranged-design 7: 0.18 s).
	static constexpr float AimBlendSeconds = 0.18f;

	// May the player be aiming right now? Server and owner use the same rule. Sprinting leaves aim (ranged-design 7).
	static bool CanAim(bool bRangedMode, bool bGunEquipped, bool bDead, bool bStaggered, bool bSprinting);

	// What the held-aim wish becomes after the rules ran. Hold mode keeps the wish (aim resumes when the sprint ends while the button is
	// still down); toggle mode drops it, so a sprint really cancels the toggle.
	static bool WantedAfterRules(bool bWanted, bool bToggleMode, bool bCanAim, bool bRangedMode);

	// ---- Camera presets ----
	static FMDCameraPreset HipPreset(float RightCm, float UpCm);
	static FMDCameraPreset AimedPreset(float RightCm, float UpCm, float FovDelta, float MaxArmCm, float PitchLimit);
	// Blend by an already eased alpha (0 = Hip, 1 = Aimed). Every field interpolates; MaxArmCm interpolates from "no limit" in a way
	// that never lengthens the arm (see .cpp).
	static FMDCameraPreset Blend(const FMDCameraPreset& Hip, const FMDCameraPreset& Aimed, float EasedAlpha);
	// A weapon's AimCameraPreset name to a preset: None / unknown names give the default aimed preset.
	static FMDCameraPreset ResolveAimedPreset(FName PresetName, const FMDCameraPreset& Default);
	// Pull Desired in along Pivot -> Desired so it is at most MaxArmCm from Pivot (0 = no limit).
	static FVector ClampArm(const FVector& Pivot, const FVector& Desired, float MaxArmCm);

	// ---- Aim offset layer (anim BP: upper body follows the camera) ----
	static constexpr float AimOffsetYawLimit = 90.0f;
	static constexpr float AimOffsetPitchLimit = 60.0f;
	static constexpr float AimOffsetLingerSeconds = 1.5f; // stays on this long after a shot
	// (Yaw, Pitch) of Aim relative to Base, each normalized to -180..180 and clamped to the limits.
	static FVector2D ComputeAimOffset(const FRotator& Aim, const FRotator& Base);
	static float AimOffsetAlpha(bool bAimed, bool bRangedMode, float SecondsSinceShot);

	// ---- Aim assist ----
	static constexpr float FrictionSensitivity = 0.55f;  // look sensitivity multiplier at assist strength 1 while the reticle is on the target
	static constexpr float MagnetismHip = 0.35f;         // magnetism weight at assist strength 1
	static constexpr float MagnetismAimed = 0.6f;
	static constexpr float MagnetismConeDeg = 10.0f;     // no pull beyond this angle from the target
	static constexpr float MagnetismMaxDegPerSec = 30.0f; // hard speed cap: the camera glides, it never jumps
	static constexpr float MagnetismSpring = 4.0f;       // pull speed = Weight * error * Spring (deg/s), then capped
	static constexpr float MagnetismDeadZoneDeg = 0.05f;
	static constexpr float MaxAssistDelta = 0.1f;        // longest frame the assist integrates
	static constexpr float DefaultTargetRadiusCm = 60.0f; // chest sphere the reticle must overlap for friction
	static constexpr float MinOverlapDeg = 1.5f;

	static FMDAimAssistKind KindFor(bool bGamepad);
	// Strength option 0..1 to the friction look multiplier: 1 (none) at 0, FrictionSensitivity at 1; only while the reticle overlaps.
	static float FrictionMultiplier(float Strength01, bool bOverlapsTarget);
	static float MagnetismWeight(bool bAimed, float Strength01);
	// Does the reticle (view centre) overlap a sphere of RadiusCm at DistanceCm? AngleDeg = angle from the view axis to the sphere centre.
	static bool ReticleOverlaps(float AngleDeg, float DistanceCm, float RadiusCm);
	// A hard lock already steers the camera (lock-on camera assist), so magnetism stays out of its way.
	static bool MagnetismAllowed(bool bHardLock);
	// Degrees to turn on one axis this frame. ErrorDeg = target angle minus view angle on that axis (signed). InputDeg = the player's raw
	// look input on that axis this frame (signed, any scale). Zero when out of the cone, inside the dead zone, or when the player is
	// turning away from the target. Never exceeds |ErrorDeg| (no overshoot) nor MagnetismMaxDegPerSec x Delta (no snap).
	static float MagnetismStep(float ErrorDeg, float Weight, float DeltaSeconds, float InputDeg);
};
