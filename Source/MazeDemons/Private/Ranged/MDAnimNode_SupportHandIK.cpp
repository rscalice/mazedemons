#include "Ranged/MDAnimNode_SupportHandIK.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
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

void FAnimNode_MDSupportHandIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	FAnimNode_TwoBoneIK::PreUpdate(InAnimInstance);

	Alpha = 0.0f;
	const USkeletalMeshComponent* Mesh = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	const APawn* Pawn = Mesh ? Cast<APawn>(Mesh->GetOwner()) : nullptr;
	UMDRangedComponent* Ranged = UMDRangedComponent::Find(Pawn);
	if (!Ranged)
	{
		return;
	}
	Ranged->RefreshWeaponMesh();

	const float DisableCurve = InAnimInstance->GetCurveValue(DisableCurveName);
	const float TargetAlpha = Ranged->GetSupportHandAlpha(DisableCurve);
	FTransform Grip;
	if (TargetAlpha <= 0.0f || !Ranged->GetSupportGripWorldTransform(Grip))
	{
		return;
	}
	EffectorLocation = Grip.GetLocation();
	// Keep the elbow where the animation put it (last frame's lower arm) so the pole vector never flips.
	JointTargetLocation = Mesh->GetSocketLocation(FName(TEXT("lowerarm_l")));
	Alpha = TargetAlpha;
}
