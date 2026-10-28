#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/MDAIRules.h"
#include "AI/MDAITypes.h"
#include "AI/MDPack.h"
#include "MDCombatTypes.h"
#include "MDEnemyBrainComponent.generated.h"

class AMDEnemyCharacter;
class UMDAISubsystem;
class UMDEnemyAttackComponent;
class UMDPackDirectorSubsystem;
class UMDPoiseComponent;

// Server: the brain (re)selected an intent. bRestart = the executor must enter the state again even if it is the same
// intent (a fresh attack after the previous one finished). The controller turns this into a StateTree event.
DECLARE_MULTICAST_DELEGATE_TwoParams(FMDIntentSelectedNative, EMDIntent /*Intent*/, bool /*bRestart*/);

// The per-enemy brain (v1-design 2.3), on AMDEnemyController so it exists on the server only. Decision logic lives in
// MDAIRules (FMDUtilityScorer, FMDDelayedStimulusQueue, FMDAttackTokenPool); this component wires those rules to the pawn:
//   sense   -> ReportStimulus (AIPerception sight / damage) through the reaction-time delay
//   context -> FMDBrainContext from the pawn, target, tier and token state
//   score   -> FMDUtilityScorer::Score, seeded on (run seed, enemy id, decision index)
//   select  -> hysteresis (SwitchMargin), commitment while an attack runs
//   execute -> OnIntentSelected (StateTree event) + the pawn's ReplicatedIntent
// Interrupts bypass scoring: a Stagger reaction (token returned, attack cancelled) and death.
// It never ticks: UMDAISubsystem's round-robin scheduler calls RunDecision at BrainHz.
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDEnemyBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDEnemyBrainComponent();

	// ---- Lifecycle (called by AMDEnemyController) ----
	void OnPossessed(APawn* InPawn);
	void OnUnpossessed();

	// ---- Input ----
	// A perception event: Source seen / heard / felt (bSensed) at Location, or lost (bSensed false). Timestamped now and
	// delivered to the brain ReactionTime later.
	void ReportStimulus(AActor* Source, bool bSensed, const FVector& Location);

	// One decision (the scheduler's entry point; tests call it directly with their own clock).
	void RunDecision(double Now);

	// The executor finished the current intent's action (attack montage ended, stun ended): decide again at once.
	void NotifyIntentFinished();

	// The pawn died (the character calls this before it detaches its controller): Dead intent, token returned, unscheduled.
	void NotifyDeath() { HandleDeath(); }

	// ---- Queries ----
	UFUNCTION(BlueprintPure, Category = "Enemy|Brain")
	EMDIntent GetCurrentIntent() const { return CurrentIntent; }

	int32 GetDecisionIndex() const { return DecisionIndex; }
	bool IsIntentFinished() const { return bIntentFinished; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Brain")
	AActor* GetTarget() const { return Target.Get(); }

	// Where the target is (sensed) or was last known to be.
	FVector GetTargetLocation() const;
	bool IsTargetSensed() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Brain")
	bool HoldsAttackToken() const;

	const MDAI::FScores& GetLastScores() const { return LastScores; }
	int32 GetChosenAttackIndex() const { return ChosenAttackIndex; }
	const FMDTierParams& GetTier() const { return Tier; }
	const FMDArchetypeAIParams& GetAIParams() const;
	float GetAttackRange() const;
	float GetThreatRange() const { return GetAIParams().ThreatRange; }

	// Tier-scaled tell of the chosen attack (0 when none).
	float GetChosenTellTime() const;

	// ---- Pack (V1 WP2) ----
	// What the pack director assigned at the last decision (role, pack state, focus target).
	const FMDEnemyAssignment& GetAssignment() const { return Assignment; }
	EMDRole GetRole() const { return Assignment.Role; }
	EMDPackState GetPackState() const { return Assignment.PackState; }

	// Live queries to the director for the executor. True when the point exists:
	//   slot point    the melee ring slot around the focus target (only while the focus target is this brain's target and sensed)
	//   threat point  the 4-6 m ring point on the same angle, where an enemy without a token holds
	//   rally point   where a retreating pack runs to (only while the pack is in Regroup)
	bool GetSlotPoint(FVector& OutPoint) const;
	bool GetThreatPoint(FVector& OutPoint) const;
	bool GetRallyPoint(FVector& OutPoint) const;

	// Within reach of the rally point.
	bool IsAtRally() const;

	int32 GetEnemyId() const { return EnemyId; }
	AMDEnemyCharacter* GetEnemy() const { return Enemy.Get(); }
	UMDAISubsystem* GetAISubsystem() const;

	// Hysteresis margin (2.3 step 4).
	UPROPERTY(EditAnywhere, Category = "Brain", meta = (ClampMin = "0"))
	float SwitchMargin = MDAI::SwitchMargin;

	// Seconds without a stimulus before a target is forgotten.
	UPROPERTY(EditAnywhere, Category = "Brain", meta = (ClampMin = "0"))
	float LoseTargetTime = static_cast<float>(MDAI::LoseTargetTime);

	FMDIntentSelectedNative OnIntentSelected;

	// Debug overlay (md.AI.Debug 1): intent, top 3 scores, token. Server side.
	void DrawDebug() const;

	static UMDEnemyBrainComponent* FindFor(const AActor* Actor);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FKnownActor
	{
		FVector LastLocation = FVector::ZeroVector;
		double LastSensedTime = -1000.0;
		bool bSensedNow = false;
	};

	UFUNCTION()
	void HandleReaction(const FMDHitResultRep& Hit);

	UFUNCTION()
	void HandleStunChanged(bool bStunned);

	UFUNCTION()
	void HandleDeath();

	void HandleAttackEnded(bool bInterrupted);
	void HandleDamageApplied(AActor* Instigator, float Applied);

	void SetIntent(EMDIntent Intent, bool bRestart);
	void UpdateKnownActors(double Now);
	void UpdateTarget(double Now);
	bool IsValidTargetActor(const AActor* Actor) const;
	FMDBrainContext BuildContext(double Now) const;
	void ReleaseToken(bool bImmediate);
	UMDPackDirectorSubsystem* GetDirector() const;
	void ReportToDirector();
	bool ResolveAssignment(FMDEnemyAssignment& Out) const;

	struct FDamageRecord
	{
		TWeakObjectPtr<AActor> Instigator;
		float Amount = 0.0f;
		double Time = 0.0;
	};

	FMDEnemyAssignment Assignment;
	TArray<FDamageRecord> RecentDamage;
	TWeakObjectPtr<AActor> OverrideTarget;
	double OverrideUntil = 0.0;

	TWeakObjectPtr<AMDEnemyCharacter> Enemy;
	TWeakObjectPtr<UMDEnemyAttackComponent> Attack;
	TWeakObjectPtr<UMDPoiseComponent> Poise;
	TWeakObjectPtr<AActor> Target;
	TMap<TWeakObjectPtr<AActor>, FKnownActor> Known;
	FMDDelayedStimulusQueue Stimuli;

	FMDTierParams Tier;
	EMDIntent CurrentIntent = EMDIntent::Idle;
	bool bIntentFinished = false;
	int32 DecisionIndex = 0;
	MDAI::FScores LastScores;
	int32 ChosenAttackIndex = INDEX_NONE;
	double NextAttackReadyTime = 0.0;
	int32 EnemyId = 0;
	bool bDead = false;
	bool bBound = false;
};
