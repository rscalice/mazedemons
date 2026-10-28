#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AI/MDAIRules.h"
#include "AI/MDAITypes.h"
#include "AI/MDPerceptionRules.h"
#include "MDQuestTypes.h"
#include "MDAISubsystem.generated.h"

class APawn;
class UMDEnemyBrainComponent;

// Server-only world services the enemy brains share (V1 WP1):
//   - the round-robin brain scheduler (2.9): brains do not tick themselves, this runs BrainHz x N decisions per second
//     spread evenly over frames;
//   - the attack token calls (1.3): thin wrappers over UMDPackDirectorSubsystem (V1 WP2), which holds the per-player pools,
//     MaxTokensPerPlayer from the tier (1 at T0);
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

	// ---- Tokens (V1 WP2: these calls delegate to UMDPackDirectorSubsystem, which owns the per-player pools) ----
	bool RequestAttackToken(AActor* Enemy, AActor* Target);
	bool HoldsAttackToken(const AActor* Enemy) const;
	void ReleaseAttackToken(AActor* Enemy);
	void ReleaseAttackTokenAfter(AActor* Enemy, double Delay = MDAI::TokenHoldAfterAttack);
	// The enemy started the attack its token was for: a higher priority requester can no longer take it.
	void CommitAttackToken(AActor* Enemy);
	int32 CountAttackTokensOn(const AActor* Target) const;

	// Stable id of an actor for the token pools (assigned on first use).
	int32 GetActorId(const AActor* Actor);
	// The id if the actor already has one, else 0 (never assigns).
	int32 FindActorId(const AActor* Actor) const;

	// ---- Run / tier ----
	int32 GetRunSeed() const { return RunSeed; }
	void SetRunSeed(int32 InSeed) { RunSeed = InSeed; }

	// md.AI.ForceTier: true (and the tier) when it is set to 0..4. A pack's own tier (UMDPackDirectorSubsystem::ComputeTier,
	// V1 WP4) applies when it is off.
	static bool TryGetForcedTier(EMDIntelligenceTier& OutTier);

	// The tier for an enemy that is in no pack: md.AI.ForceTier when set, else T0. Enemies in a pack ask the director
	// (UMDPackDirectorSubsystem::GetTierOf).
	static EMDIntelligenceTier ResolveTier();
	static FMDTierParams ResolveTierParams() { return FMDTierParams::ForTier(ResolveTier()); }

	// md.AI.Debug != 0
	static bool IsDebugEnabled();

	// md.AI.BrainHz
	static float GetBrainHz();

	// md.AI.DebugPerception != 0
	static bool IsDebugPerceptionEnabled();

	// md.AI.Dormancy != 0 (default on)
	static bool IsDormancyEnabled();

	double Now() const;

	// ---- Players (V1 WP3) ----
	// The living player pawns: every player controller's pawn plus the pawns added with TrackExtraPlayer.
	void CollectPlayerPawns(TArray<AActor*>& Out) const;
	void CollectPlayerLocations(TArray<FVector>& Out) const;
	// Locations of the players with an enemy within melee reach (kite points keep 3 m from them).
	void CollectMeleePlayerLocations(TArray<FVector>& Out) const;
	// A pawn that counts as a player without a player controller (tests, a possessed-by-AI stand-in).
	void TrackExtraPlayer(APawn* Pawn);

	// ---- Dormancy (2.9) ----
	// Evaluates every brain: further than 40 m from every player (and not hurt for 10 s) = dormant (no sensing, no thinking),
	// else awake; also sets the pawn's NetUpdateFrequency (10 Hz beyond 25 m). Runs twice a second from Tick; tests call it
	// directly. Returns the number of dormant brains afterwards.
	int32 UpdateDormancy(double Now);
	int32 NumDormant() const;

	// ---- Room reveal gate (2.6, U4 shared fog of war) ----
	// True when the room / zone at Location has been revealed by the players (the shared fog grid). True when there is no fog
	// (no game state, no level map, tests) and for points outside the fog's bounds: the gate only ever blocks known hidden rooms.
	bool IsRoomRevealed(const FVector& Location) const;
	// Tests: use Grid instead of the game state's fog (null = back to the real fog).
	void SetRevealGridOverride(const FMDFogGrid* Grid);

	// ---- Rally points (2.6) ----
	// Positions of the actors tagged MDRallyPoint and of the room entries (MDRoomEntry); rescanned every 2 s.
	void CollectRallyPoints(TArray<FVector>& OutRallyPoints, TArray<FVector>& OutRoomEntries) const;

	// ---- Navigation (2.9) ----
	// One path request out of this frame's budget of 10. The movement tasks ask before every MoveTo; a denied task retries on
	// the next frame.
	bool TryConsumePathRequest();
	const MDNav::FPathBudget& GetPathBudget() const { return PathBudget; }

	// ---- Profile ----
	// Time this subsystem's Tick took (scheduler, dormancy, noise), summed over frames: the brain budget (0.6 ms/frame).
	const MDAIPerf::FFrameProfile& GetProfile() const { return Profile; }
	void ResetProfile() { Profile = MDAIPerf::FFrameProfile(); }

	// UTickableWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// Exposed for tests: runs one scheduler step as if DeltaTime passed. Returns the number of brains run.
	int32 StepScheduler(float DeltaTime);

private:
	// DT_IntelligenceTiers (V1 WP4), loaded on initialize and handed to FMDTierParams; null when the asset is missing.
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> TierTable;

	TArray<TWeakObjectPtr<UMDEnemyBrainComponent>> Brains;
	int32 Cursor = 0;
	float Accumulator = 0.0f;
	int32 NextEnemyId = 1;

	TMap<TWeakObjectPtr<const AActor>, int32> ActorIds;
	int32 NextActorId = 1;

	int32 RunSeed = 0;

	// V1 WP3
	void ReportSprintNoise();
	void RefreshRallyCache(double InNow) const;
	void RefreshFogCache(double InNow) const;

	TArray<TWeakObjectPtr<APawn>> ExtraPlayers;
	float SlowAccumulator = 0.0f;
	float DebugAccumulator = 0.0f;
	MDAIPerf::FFrameProfile Profile;
	MDNav::FPathBudget PathBudget;

	TOptional<FMDFogGrid> FogOverride;
	mutable FMDFogGrid FogCache;
	mutable bool bFogCacheValid = false;
	mutable double NextFogRefresh = -1.0;

	mutable TArray<TWeakObjectPtr<AActor>> RallyActors;
	mutable TArray<TWeakObjectPtr<AActor>> EntryActors;
	mutable double NextRallyRefresh = -1.0;
	mutable int32 CachedActorCount = -1;
};
