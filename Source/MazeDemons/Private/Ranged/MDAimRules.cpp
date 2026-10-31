#include "Ranged/MDAimRules.h"

namespace MDAimRulesLocal
{
	constexpr float UnlimitedArmCm = 1000.0f; // what "no arm limit" counts as while blending toward a limited arm
}

bool FMDAimRules::CanAim(bool bRangedMode, bool bGunEquipped, bool bDead, bool bStaggered, bool bSprinting)
{
	return bRangedMode && bGunEquipped && !bDead && !bStaggered && !bSprinting;
}

bool FMDAimRules::WantedAfterRules(bool bWanted, bool bToggleMode, bool bCanAim, bool bRangedMode)
{
	if (!bRangedMode)
	{
		return false; // holstering the gun ends the wish
	}
	if (!bCanAim && bToggleMode)
	{
		return false; // a sprint / stagger / death cancels a toggled aim for good
	}
	return bWanted;
}

FMDCameraPreset FMDAimRules::HipPreset(float RightCm, float UpCm)
{
	FMDCameraPreset P;
	P.RightCm = RightCm;
	P.UpCm = UpCm;
	return P;
}

FMDCameraPreset FMDAimRules::AimedPreset(float RightCm, float UpCm, float FovDelta, float MaxArmCm, float PitchLimit)
{
	FMDCameraPreset P;
	P.RightCm = RightCm;
	P.UpCm = UpCm;
	P.FovDelta = FovDelta;
	P.MaxArmCm = FMath::Max(MaxArmCm, 0.0f);
	P.PitchLimit = FMath::Clamp(PitchLimit, 1.0f, 89.9f);
	return P;
}

FMDCameraPreset FMDAimRules::Blend(const FMDCameraPreset& Hip, const FMDCameraPreset& Aimed, float EasedAlpha)
{
	const float A = FMath::Clamp(EasedAlpha, 0.0f, 1.0f);
	FMDCameraPreset Out;
	Out.RightCm = FMath::Lerp(Hip.RightCm, Aimed.RightCm, A);
	Out.UpCm = FMath::Lerp(Hip.UpCm, Aimed.UpCm, A);
	Out.FovDelta = FMath::Lerp(Hip.FovDelta, Aimed.FovDelta, A);
	Out.PitchLimit = FMath::Lerp(Hip.PitchLimit, Aimed.PitchLimit, A);
	if (A <= 0.0f)
	{
		Out.MaxArmCm = Hip.MaxArmCm;
	}
	else if (Aimed.MaxArmCm <= 0.0f)
	{
		Out.MaxArmCm = Hip.MaxArmCm;
	}
	else
	{
		const float From = Hip.MaxArmCm > 0.0f ? Hip.MaxArmCm : MDAimRulesLocal::UnlimitedArmCm;
		Out.MaxArmCm = FMath::Lerp(From, Aimed.MaxArmCm, A);
	}
	return Out;
}

FMDCameraPreset FMDAimRules::ResolveAimedPreset(FName PresetName, const FMDCameraPreset& Default)
{
	// Only the default preset exists today; a weapon's AimCameraPreset name is the hook for per-weapon looks (a sniper zoom later).
	(void)PresetName;
	return Default;
}

FVector FMDAimRules::ClampArm(const FVector& Pivot, const FVector& Desired, float MaxArmCm)
{
	if (MaxArmCm <= 0.0f)
	{
		return Desired;
	}
	const FVector Arm = Desired - Pivot;
	const double Length = Arm.Size();
	if (Length <= static_cast<double>(MaxArmCm) || Length < UE_KINDA_SMALL_NUMBER)
	{
		return Desired;
	}
	return Pivot + Arm * (static_cast<double>(MaxArmCm) / Length);
}

FVector2D FMDAimRules::ComputeAimOffset(const FRotator& Aim, const FRotator& Base)
{
	const float Yaw = FRotator::NormalizeAxis(Aim.Yaw - Base.Yaw);
	const float Pitch = FRotator::NormalizeAxis(Aim.Pitch - Base.Pitch);
	return FVector2D(FMath::Clamp(Yaw, -AimOffsetYawLimit, AimOffsetYawLimit), FMath::Clamp(Pitch, -AimOffsetPitchLimit, AimOffsetPitchLimit));
}

float FMDAimRules::AimOffsetAlpha(bool bAimed, bool bRangedMode, float SecondsSinceShot)
{
	if (!bRangedMode)
	{
		return 0.0f;
	}
	return (bAimed || (SecondsSinceShot >= 0.0f && SecondsSinceShot <= AimOffsetLingerSeconds)) ? 1.0f : 0.0f;
}

FMDAimAssistKind FMDAimRules::KindFor(bool bGamepad)
{
	FMDAimAssistKind Kind;
	Kind.bFriction = !bGamepad;
	Kind.bMagnetism = bGamepad;
	return Kind;
}

float FMDAimRules::FrictionMultiplier(float Strength01, bool bOverlapsTarget)
{
	if (!bOverlapsTarget)
	{
		return 1.0f;
	}
	return FMath::Lerp(1.0f, FrictionSensitivity, FMath::Clamp(Strength01, 0.0f, 1.0f));
}

float FMDAimRules::MagnetismWeight(bool bAimed, float Strength01)
{
	return (bAimed ? MagnetismAimed : MagnetismHip) * FMath::Clamp(Strength01, 0.0f, 1.0f);
}

bool FMDAimRules::ReticleOverlaps(float AngleDeg, float DistanceCm, float RadiusCm)
{
	const float Dist = FMath::Max(DistanceCm, 1.0f);
	const float HalfAngle = FMath::RadiansToDegrees(FMath::Atan(FMath::Max(RadiusCm, 0.0f) / Dist));
	return FMath::Abs(AngleDeg) <= FMath::Max(HalfAngle, MinOverlapDeg);
}

bool FMDAimRules::MagnetismAllowed(bool bHardLock)
{
	return !bHardLock;
}

float FMDAimRules::MagnetismStep(float ErrorDeg, float Weight, float DeltaSeconds, float InputDeg)
{
	const float Abs = FMath::Abs(ErrorDeg);
	if (Weight <= 0.0f || DeltaSeconds <= 0.0f || Abs < MagnetismDeadZoneDeg || Abs > MagnetismConeDeg)
	{
		return 0.0f;
	}
	// The player is turning away from the target on this axis: never fight them.
	if (FMath::Abs(InputDeg) > KINDA_SMALL_NUMBER && FMath::Sign(InputDeg) != FMath::Sign(ErrorDeg))
	{
		return 0.0f;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxAssistDelta);
	const float Speed = FMath::Min(Weight * Abs * MagnetismSpring, MagnetismMaxDegPerSec);
	const float Step = FMath::Min(Speed * Dt, Abs); // never past the target
	return ErrorDeg < 0.0f ? -Step : Step;
}
