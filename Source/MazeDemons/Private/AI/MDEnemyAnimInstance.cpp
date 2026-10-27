#include "AI/MDEnemyAnimInstance.h"

#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "MDPoiseComponent.h"
#include "GameFramework/Pawn.h"

void UMDEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return;
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
