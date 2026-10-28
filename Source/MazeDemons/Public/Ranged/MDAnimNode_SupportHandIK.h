// R0 section 7.1: support-hand (left) two-bone IK onto the held gun's Grip_L. Self-contained: it finds its own target and alpha
// (UMDRangedComponent + FMDRangedRules::SupportHandIKAlpha) in PreUpdate on the game thread, so the anim BP needs no variables.
// Placed in ABP_GenericRetarget after the retargeted GASP pose. DisableLHandIK anim curve = 1 turns it off (reload / swap).
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "MDAnimNode_SupportHandIK.generated.h"

class USkeletalMeshComponent;

USTRUCT(BlueprintInternalUseOnly)
struct MAZEDEMONS_API FAnimNode_MDSupportHandIK : public FAnimNode_TwoBoneIK
{
	GENERATED_BODY()

	FAnimNode_MDSupportHandIK();

	// Name of the anim curve that forces the IK off (reload and swap montages key it to 1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName DisableCurveName = FName(TEXT("DisableLHandIK"));

	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

private:
	// Upper arm + forearm length of the IK chain (IKBone = the hand) on the posed mesh, in world units; also returns the shoulder
	// and elbow world positions. 0 when the chain is missing.
	float MeasureArm(const USkeletalMeshComponent* Mesh, FVector& OutShoulder, FVector& OutElbow) const;

	// Reach weight after smoothing (0..1), see FMDRangedRules::SupportHandReachAlpha.
	float SmoothedReach = 0.0f;
};
