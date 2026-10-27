#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AI/MDAIRules.h"
#include "AI/MDAITypes.h"
#include "MDAISubsystem.generated.h"

class UMDEnemyBrainComponent;

// Server-only world services the enemy brains share (V1 WP1):
//   - the round-robin brain scheduler (2.9): brains do not tick themselves, this runs BrainHz x N decisions per second
//     spread evenly over frames;
//   - the attack token pool (1.3): one pool, MaxTokensPerPlayer from the tier (1 at T0). This is the minimal interface
//     WP2's UMDPackDirectorSubsystem takes over (same calls, same FMDAttackTokenPool);
//   - the run seed every brain's noise is keyed on (md.AI.Seed; 0 = drawn once per world and logged).
// Console: md.AI.Debug, md.AI.ForceTier, md.AI.BrainHz, md.AI.Seed (see MDAISubsystem.cpp).
UCLASS()
class MAZEDEMONS_API UMDAISubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMDAISubsystem* Get(const UWorld* World);

	// ---- Brains ----
	// Registers a brain with the scheduler and returns its stable enemy id (1-based, never reused in this world).
	int32 RegisterBrain(UMDEnemyBrainComponent* Brain);
	void UnregisterBrain(UMDEnemyBrainComponent* Brain);
	int32 NumBrains() const { return Brains.Num(); }

	// ---- Tokens (WP2 replaces the implementation, not the calls) ----
	bool RequestAttackToken(AActor* Enemy, AActor* Target);
	bool HoldsAttackToken(const AActor* Enemy) const;
	void ReleaseAttackToken(AActor* Enemy);
	void ReleaseAttackTokenAfter(AActor* Enemy, double Delay = MDAI::TokenHoldAfterAttack);
	int32 CountAttackTokensOn(const AActor* Target) const;

	// Stable id of an actor for the token pool (assigned on first use).
	int32 GetActorId(const AActor* Actor);

	// ---- Run / tier ----
	int32 GetRunSeed() const { return RunSeed; }
	void SetRunSeed(int32 InSeed) { RunSeed = InSeed; }

	// The tier every brain runs at this WP: md.AI.ForceTier when >= 0, else T0 (tier computation is WP4).
	static EMDIntelligenceTier ResolveTier();
	static FMDTierParams ResolveTierParams() { return FMDTierParams::ForTier(ResolveTier()); }

	// md.AI.Debug != 0
	static bool IsDebugEnabled();

	// md.AI.BrainHz
	static float GetBrainHz();

	double Now() const;

	// UTickableWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// Exposed for tests: runs one scheduler step as if DeltaTime passed. Returns the number of brains run.
	int32 StepScheduler(float DeltaTime);

private:
	TArray<TWeakObjectPtr<UMDEnemyBrainComponent>> Brains;
	int32 Cursor = 0;
	float Accumulator = 0.0f;
	int32 NextEnemyId = 1;

	FMDAttackTokenPool Tokens;
	TMap<TWeakObjectPtr<const AActor>, int32> ActorIds;
	int32 NextActorId = 1;

	int32 RunSeed = 0;
};
