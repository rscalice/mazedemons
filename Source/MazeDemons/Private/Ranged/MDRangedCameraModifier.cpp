#include "Ranged/MDRangedCameraModifier.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedWeaponData.h"

namespace MDRangedCameraLocal
{
	static TAutoConsoleVariable<float> CVarRangedOffset(TEXT("md.Cam.RangedOffset"), 55.0f,
		TEXT("Ranged mode: camera shift to the shoulder side, cm (0 = off)."));
	static TAutoConsoleVariable<float> CVarRangedUp(TEXT("md.Cam.RangedUp"), 12.0f,
		TEXT("Ranged mode: camera shift up, cm."));
	static TAutoConsoleVariable<float> CVarRangedBlend(TEXT("md.Cam.RangedBlend"), 0.2f,
		TEXT("Ranged mode: seconds to blend the shoulder camera in / out."));
	static TAutoConsoleVariable<float> CVarAimBlend(TEXT("md.Cam.AimBlend"), FMDAimRules::AimBlendSeconds,
		TEXT("Aimed: seconds to blend the aim camera preset in / out (design: 0.18)."));
	static TAutoConsoleVariable<int32> CVarShoulderSide(TEXT("md.Cam.ShoulderSide"), 1,
		TEXT("Shoulder side: 1 = right, -1 = left."));
	static TAutoConsoleVariable<float> CVarAimExtra(TEXT("md.Cam.AimExtraOffset"), 10.0f,
		TEXT("Ranged + aimed: extra shoulder shift on top of md.Cam.RangedOffset, cm."));
	static TAutoConsoleVariable<float> CVarAimUp(TEXT("md.Cam.AimUpDelta"), 0.0f,
		TEXT("Ranged + aimed: extra camera height on top of md.Cam.RangedUp, cm (negative lowers it)."));
	static TAutoConsoleVariable<float> CVarAimFov(TEXT("md.Cam.AimFovDelta"), -18.0f,
		TEXT("Ranged + aimed: FOV change in degrees (negative narrows; design 90 -> 72)."));
	static TAutoConsoleVariable<float> CVarAimArm(TEXT("md.Cam.AimArm"), 180.0f,
		TEXT("Ranged + aimed: longest pivot -> camera distance in cm (0 = leave the rig's arm alone). A shorter arm keeps corridors clear."));
	static TAutoConsoleVariable<float> CVarAimPitch(TEXT("md.Cam.AimPitch"), 60.0f,
		TEXT("Ranged + aimed: pitch clamp in degrees either way (89.9 = no clamp)."));
	static TAutoConsoleVariable<int32> CVarCamDebug(TEXT("md.Cam.Debug"), 0,
		TEXT("Log the ranged shoulder camera once a second: alpha, offset, probe hit."));
	constexpr float ProbeRadius = 12.0f;
	constexpr float RigProbeRadius = 8.0f; // smaller than the rig's own probe: only a rig that really clips gets pulled in
	constexpr float ProbeMargin = 0.97f;   // stop slightly short of the hit
	constexpr float PivotHeight = 50.0f;
}

float FMDRangedCameraRules::StepAlpha(float Alpha, float Target, float DeltaSeconds, float BlendSeconds)
{
	Target = FMath::Clamp(Target, 0.0f, 1.0f);
	if (BlendSeconds <= KINDA_SMALL_NUMBER)
	{
		return Target;
	}
	const float Step = FMath::Max(DeltaSeconds, 0.0f) / BlendSeconds;
	return Alpha < Target ? FMath::Min(Alpha + Step, Target) : FMath::Max(Alpha - Step, Target);
}

float FMDRangedCameraRules::Ease(float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return A * A * (3.0f - 2.0f * A);
}

FVector FMDRangedCameraRules::ComputeOffset(float Alpha, float Side, float RightCm, float UpCm, const FVector& RightAxis, const FVector& UpAxis)
{
	const float S = Side < 0.0f ? -1.0f : 1.0f;
	return (RightAxis * (RightCm * S) + UpAxis * UpCm) * Ease(Alpha);
}

FVector FMDRangedCameraRules::ApplyCollision(const FVector& Pivot, const FVector& Desired, bool bHit, float HitFraction)
{
	if (!bHit)
	{
		return Desired;
	}
	const float F = FMath::Clamp(HitFraction, 0.0f, 1.0f) * MDRangedCameraLocal::ProbeMargin;
	return Pivot + (Desired - Pivot) * F;
}

FMDCameraPreset FMDRangedCameraRules::ConfiguredHipPreset()
{
	using namespace MDRangedCameraLocal;
	return FMDAimRules::HipPreset(CVarRangedOffset.GetValueOnGameThread(), CVarRangedUp.GetValueOnGameThread());
}

FMDCameraPreset FMDRangedCameraRules::ConfiguredAimedPreset(FName WeaponPresetName)
{
	using namespace MDRangedCameraLocal;
	const FMDCameraPreset Default = FMDAimRules::AimedPreset(
		CVarRangedOffset.GetValueOnGameThread() + CVarAimExtra.GetValueOnGameThread(),
		CVarRangedUp.GetValueOnGameThread() + CVarAimUp.GetValueOnGameThread(),
		CVarAimFov.GetValueOnGameThread(), CVarAimArm.GetValueOnGameThread(), CVarAimPitch.GetValueOnGameThread());
	return FMDAimRules::ResolveAimedPreset(WeaponPresetName, Default);
}

float FMDRangedCameraRules::ConfiguredAimBlendSeconds()
{
	return MDRangedCameraLocal::CVarAimBlend.GetValueOnGameThread();
}

FVector FMDRangedCameraRules::ResolveCollision(const UWorld* World, const AActor* Ignore, const FVector& Pivot, const FVector& RigLocation,
	const FVector& Offset, bool* OutPulledIn)
{
	using namespace MDRangedCameraLocal;
	if (OutPulledIn)
	{
		*OutPulledIn = false;
	}
	if (!World)
	{
		return RigLocation + Offset;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRangedCamProbe), false, Ignore);
	const auto Probe = [&](const FVector& From, const FVector& To, float Radius) -> FVector
	{
		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Radius), Params);
		if (bHit && OutPulledIn)
		{
			*OutPulledIn = true;
		}
		return ApplyCollision(From, To, bHit, bHit ? Hit.Time : 1.0f);
	};
	const FVector SafeRig = Probe(Pivot, RigLocation, RigProbeRadius);
	const FVector Shifted = Probe(SafeRig, SafeRig + Offset, ProbeRadius);
	return Probe(Pivot, Shifted, ProbeRadius);
}

void UMDRangedCameraModifier::ApplyPitchLimit(float Limit)
{
	if (!CameraOwner)
	{
		return;
	}
	if (!bPitchDefaultsSaved)
	{
		DefaultPitchMin = CameraOwner->ViewPitchMin;
		DefaultPitchMax = CameraOwner->ViewPitchMax;
		bPitchDefaultsSaved = true;
	}
	CameraOwner->ViewPitchMin = FMath::Max(DefaultPitchMin, -Limit);
	CameraOwner->ViewPitchMax = FMath::Min(DefaultPitchMax, Limit);
}

bool UMDRangedCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	using namespace MDRangedCameraLocal;
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UMDRangedComponent* Ranged = Pawn ? UMDRangedComponent::Find(Pawn) : nullptr; // lives on the PlayerState; Find resolves it from the pawn

	const bool bRanged = Ranged && Ranged->IsRangedMode();
	const bool bAiming = bRanged && Ranged->IsAimingLocal();
	ShoulderAlpha = FMDRangedCameraRules::StepAlpha(ShoulderAlpha, bRanged ? 1.0f : 0.0f, DeltaTime, CVarRangedBlend.GetValueOnGameThread());
	AimAlpha = FMDRangedCameraRules::StepAlpha(AimAlpha, bAiming ? 1.0f : 0.0f, DeltaTime, FMDRangedCameraRules::ConfiguredAimBlendSeconds());

	if (CVarCamDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("MDCam: tick pawn %s ranged %d aiming %d alpha %.2f aim %.2f"), Pawn ? *Pawn->GetName() : TEXT("none"),
			bRanged ? 1 : 0, bAiming ? 1 : 0, ShoulderAlpha, AimAlpha);
	}
	const float Eased = FMDRangedCameraRules::Ease(ShoulderAlpha);
	const float AimEased = FMDRangedCameraRules::Ease(AimAlpha);
	const FMDCameraPreset Hip = FMDRangedCameraRules::ConfiguredHipPreset();
	const FMDCameraPreset Aimed = FMDRangedCameraRules::ConfiguredAimedPreset(
		Ranged && Ranged->GetEquippedWeaponData() ? Ranged->GetEquippedWeaponData()->AimCameraPreset : NAME_None);
	const FMDCameraPreset Current = FMDAimRules::Blend(Hip, Aimed, AimEased);

	ApplyPitchLimit(Current.PitchLimit);
	if (Eased <= KINDA_SMALL_NUMBER || !Pawn)
	{
		return false;
	}

	const FRotationMatrix Axes(InOutPOV.Rotation);
	const FVector Offset = FMDRangedCameraRules::ComputeOffset(ShoulderAlpha, static_cast<float>(CVarShoulderSide.GetValueOnGameThread()),
		Current.RightCm, Current.UpCm, Axes.GetScaledAxis(EAxis::Y), Axes.GetScaledAxis(EAxis::Z));

	// Aimed: a shorter arm keeps the camera out of corridor walls. Then the collision pass (rig spot, shoulder shift, chest line of sight).
	const FVector Pivot = Pawn->GetActorLocation() + FVector(0.0, 0.0, PivotHeight);
	const FVector Rig = FMDAimRules::ClampArm(Pivot, InOutPOV.Location, Current.MaxArmCm);
	bool bPulledIn = false;
	const FVector Final = FMDRangedCameraRules::ResolveCollision(Pawn->GetWorld(), Pawn, Pivot, Rig, Offset, &bPulledIn);
	if (CVarCamDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("MDCam: alpha %.2f aim %.2f offset %s arm %.0f from %s to %s pulledIn %d"), ShoulderAlpha, AimAlpha,
			*Offset.ToString(), Current.MaxArmCm, *InOutPOV.Location.ToString(), *Final.ToString(), bPulledIn ? 1 : 0);
	}
	InOutPOV.Location = Final;
	InOutPOV.FOV += Current.FovDelta; // already blended by the eased aim alpha
	return false;
}
