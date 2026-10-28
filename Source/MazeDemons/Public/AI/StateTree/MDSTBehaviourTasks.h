#pragma once

#include "CoreMinimal.h"
#include "AI/StateTree/MDSTEnemyTasks.h"
#include "MDSTBehaviourTasks.generated.h"

// V1 WP5 executor tasks (v1-design 1.2 / 3.2). Thin on purpose: the scorer and MDBehaviourRules decide, these walk to a point or
// call UMDEnemyBehaviourComponent::Begin*. Each runs until the brain's StateTree event leaves the state.

// Swarm (T1+): close in on the evenly spaced ring slot (token holder) or hold the threat ring point (no token), facing the target.
USTRUCT(meta = (DisplayName = "MD Enemy Swarm", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemySwarm : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemySwarm();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Swarm", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.3f;
};

// Bait (T2+): face the player, half-step back when approached, taunt, fake a flinch at T3+, stay within 8 m of the pack.
USTRUCT(meta = (DisplayName = "MD Enemy Bait", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyBait : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyBait();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Bait", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.25f;
};

// Kite (T1+, ranged archetypes): keep the preferred range, reposition at most once a second, fire with a token.
USTRUCT(meta = (DisplayName = "MD Enemy Kite", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyKite : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyKite();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Kite", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;
};

// Flank (T2+): walk to the flank point behind the target. The flanker's attack still needs a token.
USTRUCT(meta = (DisplayName = "MD Enemy Flank", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyFlank : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyFlank();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Flank", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.35f;
};

// Guard (T2+): stand on the guard line between the caster and the player, facing the player.
USTRUCT(meta = (DisplayName = "MD Enemy Guard", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyGuard : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyGuard();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.3f;
};

// Feint (T3+): start the wind-up of the chosen attack, cancel it with a visible jerk; the brain then attacks for real.
USTRUCT(meta = (DisplayName = "MD Enemy Feint", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyFeint : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyFeint();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Blink (T3+): 0.4 s pre-blink cue, then a validated teleport (UMDBlinkComponent, server).
USTRUCT(meta = (DisplayName = "MD Enemy Blink", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyBlink : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyBlink();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Charge (T1+, Brute): line up and run in a straight line; a sideways dodge-slide cancels it.
USTRUCT(meta = (DisplayName = "MD Enemy Charge", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyCharge : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyCharge();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

// Ambush (T2+): the ambusher stands still and watches during its arrival grace.
USTRUCT(meta = (DisplayName = "MD Enemy Ambush", Category = "MazeDemons"))
struct MAZEDEMONS_API FMDST_EnemyAmbush : public FMDSTEnemyTaskBase
{
	GENERATED_BODY()

	FMDST_EnemyAmbush();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
