#include "AI/MDEnemyAnimInstance.h"

#include "AI/MDAIRules.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "MDPoiseComponent.h"
#include "GameFramework/Pawn.h"

ERootMotionMode::Type UMDEnemyAnimInstance::ApplyRootMotionModeForRole()
{
	const AActor* Owner = GetOwningActor();
	// No owner (a preview or a bare test object): leave the Blueprint's mode alone.
	const ERootMotionMode::Type Mode = Owner ? MDEnemyAttackRules::RootMotionModeFor(Owner->GetLocalRole()) : RootMotionMode.GetValue();
	RootMotionMode = Mode;
	return Mode;
}

void UMDEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ApplyRootMotionModeForRole();
}

void UMDEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return;
	}
	// Cheap and idempotent: the role is final before BeginPlay, but a late-initialised instance must never keep a proxy
	// in RootMotionFromMontagesOnly (that would make the client simulate the attack root motion itself).
	if (RootMotionMode.GetValue() != MDEnemyAttackRules::RootMotionModeFor(Pawn->GetLocalRole()))
	{
		ApplyRootMotionModeForRole();
	}
	Speed = static_cast<float>(Pawn->GetVelocity().Size2D());
	bMoving = Speed > 3.0f;

	const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Pawn);
	if (!Enemy)
	{
		return;
	}
	const float MaxSpeed = Enemy->Archetype ? FMath::Max(Enemy->Archetype->MoveSpeed, 1.0f) : 450.0f;
	SpeedFraction = Speed / MaxSpeed;
	Intent = Enemy->GetIntent();
	bDead = Enemy->IsEnemyDead() || Intent == EMDIntent::Dead;
	bAttacking = Enemy->AttackComponent && Enemy->AttackComponent->IsAttacking();
	bStunned = Enemy->Poise && Enemy->Poise->IsStunned();
}
