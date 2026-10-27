#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeExecutionTypes.h"
#include "MDSTEnemyTasks.generated.h"

class AAIController;
class AMDEnemyController;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

// ST_Enemy executor tasks (v1-design 2.1: "StateTree executes, C++ decides"). One task per intent state; each one reads
// the brain on AMDEnemyController for the target / attack it decided on and drives the pawn through the controller.
// No task decides anything: the brain's StateTree event (AI.Intent.<X>) moves the tree between these states.
// All tasks run forever (Running) and are left through those events; the brain learns about the action's end from the
// attack component / poise component, not from the tree.

USTRUCT()
struct MAZEDEMONS_API FMDSTEnemyTaskInstanceData
{
	GENERATED_BODY()

	// Bound to the schema's AIController context data at compile time (same convention as the engine's Move To task).
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAIController> AIController = nullptr;

	// Next time (world seconds) the task re-evaluates its move.
	UPROPERTY()
	double NextUpdateTime = 0.0;

	UPROPERTY()
	FVector LastGoal = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<AActor> LastTarget = nullptr;
};

// Shared base: resolves the enemy controller from the instance data (or the tree owner as a fallback).
USTRUCT(meta = (Hidden))
struct MAZEDEMONS_API FMDSTEnemyTaskBase : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMDSTEnemyTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

protected:
	static AMDEnemyController* ResolveController(FStateTreeExecutionContext& Context, const FInstanceDataType& Data);
};

// Idle: stop, face nothing.
USTRUCT(meta = (DisplayName = "MD Enemy Idle", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyIdle : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyIdle();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Chase: move on the navmesh to the brain's target (its last known position when it is out of sight).
USTRUCT(meta = (DisplayName = "MD Enemy Chase", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyChase : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyChase();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	// Seconds between move re-evaluations.
	UPROPERTY(EditAnywhere, Category = "Chase", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.25f;

private:
	void IssueMove(AMDEnemyController& Controller, FInstanceDataType& Data, double Now) const;
};

// HoldThreat: no token -> keep the archetype's ThreatRange to the target, facing it (approach and threaten, never swing).
USTRUCT(meta = (DisplayName = "MD Enemy Hold Threat", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyHoldThreat : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyHoldThreat();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Hold Threat", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.3f;

	// Dead band around the threat range before the enemy steps in or back (cm).
	UPROPERTY(EditAnywhere, Category = "Hold Threat", meta = (ClampMin = "0"))
	float Tolerance = 120.0f;

	// Pure rule: where to stand given the target, the enemy and the range (on the target->enemy line at Range), or no move
	// when within Tolerance. Returns false when no move is needed.
	static bool ComputeHoldPoint(const FVector& TargetLocation, const FVector& EnemyLocation, float Range, float Tolerance, FVector& OutPoint);
};

// Attack: play the brain's chosen attack at its target through the attack component; the montage end tells the brain.
USTRUCT(meta = (DisplayName = "MD Enemy Attack", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyAttack : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyAttack();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Stagger: stop everything; the poise component plays the reaction and ends the stun (the brain then decides again).
USTRUCT(meta = (DisplayName = "MD Enemy Stagger", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyStagger : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyStagger();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Dead: stop everything; the character's death path (ragdoll, controller detach) does the rest.
USTRUCT(meta = (DisplayName = "MD Enemy Dead", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyDead : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyDead();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
