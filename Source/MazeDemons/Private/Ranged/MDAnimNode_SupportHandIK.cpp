
#include "Ranged/MDAnimNode_SupportHandIK.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Ranged/MDRangedComponent.h"

FAnimNode_MDSupportHandIK::FAnimNode_MDSupportHandIK()
{
	IKBone.BoneName = FName(TEXT("hand_l"));
	EffectorLocationSpace = BCS_WorldSpace;
	JointTargetLocationSpace = BCS_WorldSpace;
	bAllowStretching = false;
	Alpha = 0.0f;
}

float FAnimNode_MDSupportHandIK::MeasureArm(const USkeletalMeshComponent* Mesh, FVector& OutShoulder, FVector& OutElbow) const
{
	if (!Mesh)
	{
		return 0.0f;
	}
	const FName Hand = IKBone.BoneName;
	const FName Lower = Mesh->GetParentBone(Hand);
	const FName Upper = Lower.IsNone() ? NAME_None : Mesh->GetParentBone(Lower);
	if (Lower.IsNone() || Upper.IsNone())
	{
		return 0.0f;
	}
	// World-space bone positions of the current pose: the length follows the component scale and never needs a hard-coded 55 cm.
	OutShoulder = Mesh->GetBoneLocation(Upper, EBoneSpaces::WorldSpace);
	OutElbow = Mesh->GetBoneLocation(Lower, EBoneSpaces::WorldSpace);
	const FVector Wrist = Mesh->GetBoneLocation(Hand, EBoneSpaces::WorldSpace);
	return FVector::Dist(OutShoulder, OutElbow) + FVector::Dist(OutElbow, Wrist);
}

void FAnimNode_MDSupportHandIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	FAnimNode_TwoBoneIK::PreUpdate(InAnimInstance);

	Alpha = 0.0f;
	const USkeletalMeshComponent* Mesh = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	const APawn* Pawn = Mesh ? Cast<APawn>(Mesh->GetOwner()) : nullptr;
	UMDRangedComponent* Ranged = UMDRangedComponent::Find(Pawn);
	if (!Ranged)
	{
		SmoothedReach = 0.0f;
		return;
	}
	Ranged->RefreshWeaponMesh();

	const float DisableCurve = InAnimInstance->GetCurveValue(DisableCurveName);
	const float TargetAlpha = Ranged->GetSupportHandAlpha(DisableCurve);
	FTransform Grip;
	if (TargetAlpha <= 0.0f || !Ranged->GetSupportGripWorldTransform(Grip))
	{
		SmoothedReach = 0.0f;
		return;
	}

	// Reach rule (FMDRangedRules::SupportHandReachAlpha): a grip beyond the arm's length fades the IK out instead of stretching the arm.
	FVector Shoulder = FVector::ZeroVector;
	FVector Elbow = FVector::ZeroVector;
	const float ArmLength = MeasureArm(Mesh, Shoulder, Elbow);
	const float Reach = FMDRangedRules::SupportHandReachAlpha(FVector::Dist(Shoulder, Grip.GetLocation()), ArmLength);
	const UWorld* World = Mesh->GetWorld();
	const float DeltaTime = World ? World->GetDeltaSeconds() : 0.0f;
	// Fades out fast (a stretched arm is the bug), back in slower so a briefly out-of-reach grip does not pop.
	SmoothedReach = FMath::FInterpConstantTo(SmoothedReach, Reach, DeltaTime, Reach < SmoothedReach ? 8.0f : 4.0f);

	EffectorLocation = Grip.GetLocation();
	// Keep the elbow where the animation put it (last frame's lower arm) so the pole vector never flips.
	JointTargetLocation = Elbow;
	Alpha = TargetAlpha * SmoothedReach;
}
