#include "AI/StateTree/MDSTEnemyTasks.h"

#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "StateTreeExecutionContext.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"

#define LOCTEXT_NAMESPACE "MDSTEnemyTasks"

namespace MDSTLocal
{
	double Now(const FStateTreeExecutionContext& Context)
	{
		const UWorld* World = Context.GetWorld();
		return World ? World->GetTimeSeconds() : 0.0;
	}

	// Stop moving and face a target (focus) or, with none, the movement direction. The facing rule lives in AMDEnemyCharacter.
	void Stop(AMDEnemyController& Controller, AActor* FaceTarget)
	{
		Controller.StopMovement();
		if (AMDEnemyCharacter* Enemy = Controller.GetEnemy())
		{
			Enemy->FaceTarget(FaceTarget);
		}
		else if (!FaceTarget)
		{
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
}

AMDEnemyController* FMDSTEnemyTaskBase::ResolveController(FStateTreeExecutionContext& Context, const FInstanceDataType& Data)
{
	if (AMDEnemyController* FromContext = Cast<AMDEnemyController>(Data.AIController))
	{
		return FromContext;
	}
	// Not bound (an asset authored without the context binding): the tree runs on the controller itself.
	return Cast<AMDEnemyController>(Context.GetOwner());
}

// ---- Idle ----

FMDST_EnemyIdle::FMDST_EnemyIdle()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyIdle::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this)))
	{
		MDSTLocal::Stop(*Controller, nullptr);
	}
	return EStateTreeRunStatus::Running;
}

// ---- Chase ----

FMDST_EnemyChase::FMDST_EnemyChase()
{
	bShouldCallTick = true;
}

void FMDST_EnemyChase::IssueMove(AMDEnemyController& Controller, FInstanceDataType& Data, double Now) const
{
	UMDEnemyBrainComponent* Brain = Controller.GetBrain();
	AActor* Target = Brain ? Brain->GetTarget() : nullptr;
	Data.NextUpdateTime = Now + UpdateInterval;
	if (!Target)
	{
		Controller.StopMovement();
		Data.LastTarget = nullptr;
		return;
	}
	// Stop a little inside the attack reach so the swing can start without another step.
	const float Acceptance = FMath::Max(Brain->GetAttackRange() * 0.75f, 60.0f);
	if (Brain->IsTargetSensed())
	{
		if (Data.LastTarget != Target || Controller.GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			Controller.MoveToActor(Target, Acceptance, /*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bCanStrafe*/ true);
			Data.LastTarget = Target;
			Data.LastGoal = Target->GetActorLocation();
		}
	}
	else
	{
		// Out of sight: go to where it was last seen (the brain forgets it after LoseTargetTime).
		const FVector Goal = Brain->GetTargetLocation();
		if (Data.LastTarget != nullptr || FVector::Dist2D(Goal, Data.LastGoal) > 100.0f || Controller.GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			Controller.MoveToLocation(Goal, Acceptance, true, true, false, true);
			Data.LastTarget = nullptr;
			Data.LastGoal = Goal;
		}
	}
}

EStateTreeRunStatus FMDST_EnemyChase::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AMDEnemyController* Controller = ResolveController(Context, Data);
	if (!Controller)
	{
		return EStateTreeRunStatus::Running;
	}
	if (AMDEnemyCharacter* Enemy = Controller->GetEnemy())
	{
		Enemy->FaceMovement();
	}
	Data.LastTarget = nullptr;
	Data.LastGoal = FVector::ZeroVector;
	IssueMove(*Controller, Data, MDSTLocal::Now(Context));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMDST_EnemyChase::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTLocal::Now(Context);
	if (Now >= Data.NextUpdateTime)
	{
		if (AMDEnemyController* Controller = ResolveController(Context, Data))
		{
			IssueMove(*Controller, Data, Now);
		}
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyChase::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this)))
	{
		Controller->StopMovement();
	}
}

// ---- HoldThreat ----

FMDST_EnemyHoldThreat::FMDST_EnemyHoldThreat()
{
	bShouldCallTick = true;
}

bool FMDST_EnemyHoldThreat::ComputeHoldPoint(const FVector& TargetLocation, const FVector& EnemyLocation, float Range, float Tolerance, FVector& OutPoint)
{
	FVector Away = EnemyLocation - TargetLocation;
	Away.Z = 0.0;
	const double Dist = Away.Size();
	if (FMath::Abs(Dist - Range) <= Tolerance)
	{
		OutPoint = EnemyLocation;
		return false;
	}
	const FVector Dir = Dist > 1.0 ? Away / Dist : FVector::ForwardVector;
	OutPoint = TargetLocation + Dir * Range;
	OutPoint.Z = EnemyLocation.Z;
	return true;
}

EStateTreeRunStatus FMDST_EnemyHoldThreat::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AMDEnemyController* Controller = ResolveController(Context, Data);
	if (!Controller)
	{
		return EStateTreeRunStatus::Running;
	}
	// Face the player while stepping in or back.
	MDSTLocal::Stop(*Controller, Controller->GetBrain() ? Controller->GetBrain()->GetTarget() : nullptr);
	Data.NextUpdateTime = 0.0;
	Data.LastGoal = FVector::ZeroVector;
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemyHoldThreat::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	AMDEnemyController* Controller = ResolveController(Context, Data);
	UMDEnemyBrainComponent* Brain = Controller ? Controller->GetBrain() : nullptr;
	AMDEnemyCharacter* Enemy = Controller ? Controller->GetEnemy() : nullptr;
	if (!Brain || !Enemy)
	{
		return EStateTreeRunStatus::Running;
	}
	AActor* Target = Brain->GetTarget();
	if (!Target)
	{
		Controller->StopMovement();
		return EStateTreeRunStatus::Running;
	}
	if (Controller->GetFocusActor() != Target || !Enemy->IsFacingTarget())
	{
		Enemy->FaceTarget(Target);
	}
	FVector Point;
	if (ComputeHoldPoint(Brain->GetTargetLocation(), Enemy->GetActorLocation(), Brain->GetThreatRange(), Tolerance, Point))
	{
		if (FVector::Dist2D(Point, Data.LastGoal) > 50.0f || Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			Controller->MoveToLocation(Point, 40.0f, true, true, true, true);
			Data.LastGoal = Point;
		}
	}
	else if (Controller->GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		Controller->StopMovement();
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyHoldThreat::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this)))
	{
		MDSTLocal::Stop(*Controller, nullptr);
	}
}

// ---- Attack ----

FMDST_EnemyAttack::FMDST_EnemyAttack()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyAttack::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this));
	UMDEnemyBrainComponent* Brain = Controller ? Controller->GetBrain() : nullptr;
	AMDEnemyCharacter* Enemy = Controller ? Controller->GetEnemy() : nullptr;
	if (!Brain || !Enemy)
	{
		return EStateTreeRunStatus::Running;
	}
	// Face the target through the wind-up (the warp aims at the same point); the trace notify then freezes the facing.
	MDSTLocal::Stop(*Controller, Brain->GetTarget());
	UMDEnemyAttackComponent* Attack = Enemy->AttackComponent;
	if (!Attack || !Attack->StartAttack(Brain->GetChosenAttackIndex(), Brain->GetTarget(), Brain->GetChosenTellTime()))
	{
		Brain->NotifyIntentFinished(); // nothing to play: let the brain pick something else
	}
	return EStateTreeRunStatus::Running;
}

// ---- Stagger ----

FMDST_EnemyStagger::FMDST_EnemyStagger()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyStagger::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this)))
	{
		MDSTLocal::Stop(*Controller, nullptr);
	}
	return EStateTreeRunStatus::Running;
}

// ---- Dead ----

FMDST_EnemyDead::FMDST_EnemyDead()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyDead::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMDEnemyController* Controller = ResolveController(Context, Context.GetInstanceData(*this)))
	{
		MDSTLocal::Stop(*Controller, nullptr);
	}
	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FMDST_EnemyIdle::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("IdleDesc", "Stand still");
}
FText FMDST_EnemyChase::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("ChaseDesc", "Move to the brain's target");
}
FText FMDST_EnemyHoldThreat::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("HoldDesc", "Hold the threat range, facing the target");
}
FText FMDST_EnemyAttack::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("AttackDesc", "Play the chosen attack at the target");
}
FText FMDST_EnemyStagger::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("StaggerDesc", "Staggered: stop until the stun ends");
}
FText FMDST_EnemyDead::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("DeadDesc", "Dead");
}
#endif

#undef LOCTEXT_NAMESPACE
