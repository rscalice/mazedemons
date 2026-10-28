#include "Ranged/MDRangedCameraModifier.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Ranged/MDRangedComponent.h"

namespace MDRangedCameraLocal
{
	static TAutoConsoleVariable<float> CVarRangedOffset(TEXT("md.Cam.RangedOffset"), 55.0f,
		TEXT("Ranged mode: camera shift to the shoulder side, cm (0 = off)."));
	static TAutoConsoleVariable<float> CVarRangedUp(TEXT("md.Cam.RangedUp"), 12.0f,
		TEXT("Ranged mode: camera shift up, cm."));
	static TAutoConsoleVariable<float> CVarRangedBlend(TEXT("md.Cam.RangedBlend"), 0.2f,
		TEXT("Ranged mode: seconds to blend the shoulder camera in / out."));
	static TAutoConsoleVariable<int32> CVarShoulderSide(TEXT("md.Cam.ShoulderSide"), 1,
		TEXT("Shoulder side: 1 = right, -1 = left."));
	static TAutoConsoleVariable<float> CVarAimExtra(TEXT("md.Cam.AimExtraOffset"), 10.0f,
		TEXT("Ranged + aimed: extra shoulder shift, cm."));
	static TAutoConsoleVariable<float> CVarAimFov(TEXT("md.Cam.AimFovDelta"), -6.0f,
		TEXT("Ranged + aimed: FOV change in degrees (negative narrows)."));
	static TAutoConsoleVariable<int32> CVarCamDebug(TEXT("md.Cam.Debug"), 0,
		TEXT("Log the ranged shoulder camera once a second: alpha, offset, probe hit."));
	constexpr float ProbeRadius = 12.0f;
	constexpr float ProbeMargin = 0.97f; // stop slightly short of the hit
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

bool UMDRangedCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	using namespace MDRangedCameraLocal;
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UMDRangedComponent* Ranged = Pawn ? UMDRangedComponent::Find(Pawn) : nullptr; // lives on the PlayerState; Find resolves it from the pawn

	const bool bRanged = Ranged && Ranged->IsRangedMode();
	const float Blend = CVarRangedBlend.GetValueOnGameThread();
	ShoulderAlpha = FMDRangedCameraRules::StepAlpha(ShoulderAlpha, bRanged ? 1.0f : 0.0f, DeltaTime, Blend);
	AimAlpha = FMDRangedCameraRules::StepAlpha(AimAlpha, bRanged && Ranged->IsAimed() ? 1.0f : 0.0f, DeltaTime, Blend);

	if (CVarCamDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("MDCam: tick pawn %s ranged %d alpha %.2f"), Pawn ? *Pawn->GetName() : TEXT("none"), bRanged ? 1 : 0, ShoulderAlpha);
	}
	const float Eased = FMDRangedCameraRules::Ease(ShoulderAlpha);
	if (Eased <= KINDA_SMALL_NUMBER || !Pawn)
	{
		return false;
	}

	const float AimEased = FMDRangedCameraRules::Ease(AimAlpha);
	const float RightCm = CVarRangedOffset.GetValueOnGameThread() + CVarAimExtra.GetValueOnGameThread() * AimEased;
	const FRotationMatrix Axes(InOutPOV.Rotation);
	const FVector Offset = FMDRangedCameraRules::ComputeOffset(ShoulderAlpha, static_cast<float>(CVarShoulderSide.GetValueOnGameThread()),
		RightCm, CVarRangedUp.GetValueOnGameThread(), Axes.GetScaledAxis(EAxis::Y), Axes.GetScaledAxis(EAxis::Z));

	const FVector Desired = InOutPOV.Location + Offset;
	// Keep the shifted camera out of walls: sphere probe from the character's chest to the shifted spot, pull in on a hit.
	FVector Final = Desired;
	if (const UWorld* World = Pawn->GetWorld())
	{
		const FVector Pivot = Pawn->GetActorLocation() + FVector(0.0, 0.0, PivotHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRangedCamProbe), false, Pawn);
		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, Pivot, Desired, FQuat::Identity, ECC_Camera,
			FCollisionShape::MakeSphere(ProbeRadius), Params);
		Final = FMDRangedCameraRules::ApplyCollision(Pivot, Desired, bHit, bHit ? Hit.Time : 1.0f);
	}
	if (CVarCamDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("MDCam: alpha %.2f offset %s from %s to %s"), ShoulderAlpha, *Offset.ToString(), *InOutPOV.Location.ToString(), *Final.ToString());
	}
	InOutPOV.Location = Final;
	InOutPOV.FOV += CVarAimFov.GetValueOnGameThread() * AimEased;
	return false;
}
