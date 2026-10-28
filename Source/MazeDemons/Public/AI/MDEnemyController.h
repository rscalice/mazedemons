#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "AI/MDAITypes.h"
#include "MDEnemyController.generated.h"

class AMDEnemyCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Damage;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UMDEnemyBrainComponent;
class UStateTree;
class UStateTreeAIComponent;

// Server-only AI controller for AMDEnemyCharacter (v1-design 2.2 / 2.8). It owns:
//   - UMDEnemyBrainComponent: the decision layer (utility scoring, reaction delay, hysteresis, tokens);
//   - UStateTreeAIComponent running ST_Enemy: the executor (one state per intent, thin C++ tasks);
//   - UAIPerceptionComponent with Sight (radius / FOV from the tier, lose-sight 3 s), Hearing (any noise tag, range from the
//     tier) and Damage (always on), all with MaxAge 3 s (V1 WP3);
//   - a UCrowdFollowingComponent as the path follower (RVO avoidance and separation between enemies).
// The brain's OnIntentSelected becomes a StateTree event (AI.Intent.<X>) that moves the tree to that intent's state.
// AI controllers do not replicate, so clients never see this actor; the pawn's ReplicatedIntent carries the intent.
UCLASS()
class MAZEDEMONS_API AMDEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	AMDEnemyController(const FObjectInitializer& ObjectInitializer);

	// Dormancy (2.9): sight and hearing off while no player is within 40 m; the damage sense stays on. Server.
	void SetSensesActive(bool bActive);

	UMDEnemyBrainComponent* GetBrain() const { return Brain; }
	UStateTreeAIComponent* GetStateTreeComponent() const { return StateTree; }
	AMDEnemyCharacter* GetEnemy() const;

	// The executor asset (ST_Enemy; built by UMD_AILibrary::BuildEnemyStateTree in the editor module). Loaded when the
	// pawn is possessed. Empty = the brain runs without an executor (tests).
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TSoftObjectPtr<UStateTree> EnemyStateTree;

	// Pushes the pawn's current ReplicatedIntent again as a StateTree event (used after a late (re)start of the tree).
	void ResendIntentEvent();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	void HandleIntentSelected(EMDIntent Intent, bool bRestart);
	void ConfigureSenses();
	void TryStartExecutor();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UMDEnemyBrainComponent> Brain;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStateTreeAIComponent> StateTree;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	bool bExecutorStarted = false;
};
