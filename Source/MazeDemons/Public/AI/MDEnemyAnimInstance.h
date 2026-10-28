#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "AI/MDAITypes.h"
#include "MDEnemyAnimInstance.generated.h"

// Base class of the enemy anim Blueprints (ABP_Ghoul). It computes everything the graph reads, on every machine, so the
// Blueprint holds only nodes: a locomotion blend space driven by Speed and a montage slot. No logic in the graph.
//
// Root motion is networked the server-authoritative way (MDEnemyAttackRules::RootMotionModeFor): on the authority the
// attack montages drive the capsule (RootMotionFromMontagesOnly, plus the motion warp); on a simulated proxy the same
// montage plays in place (IgnoreRootMotion: extracted and discarded, the root stays locked) on a capsule that only follows
// the replicated movement. The Blueprint's own RootMotionMode is overridden at initialisation and kept in step on every update.
UCLASS()
class MAZEDEMONS_API UMDEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Sets RootMotionMode for the owning pawn's local role (see above). Returns the mode applied.
	ERootMotionMode::Type ApplyRootMotionModeForRole();

	// Horizontal speed of the owning pawn (cm/s).
	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	float Speed = 0.0f;

	// Speed as a fraction of the archetype's MoveSpeed (0..1+), for blend spaces authored in normalised units.
	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	float SpeedFraction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	bool bMoving = false;

	// Mirrors AMDEnemyCharacter::ReplicatedIntent (correct on clients and late joiners).
	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	EMDIntent Intent = EMDIntent::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	bool bAttacking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	bool bStunned = false;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	bool bDead = false;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
