#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDPack.h"
#include "AI/MDPackRules.h"
#include "AI/MDPerceptionRules.h"
#include "AI/MDTierRules.h"
#include "MDPackDirectorSubsystem.generated.h"

// The squad layer (v1-design 2.4), server only. It owns:
//   - packs (FMDPack): one per spawn group, loose enemies are packs of one; packs merge at T2+ (within 10 m, same target seen);
//   - the per-player attack token pools (FMDPackTokenPool): MaxTokens by tier, role priority, expiry rules (1.3);
//   - roles (Striker / Flanker / Bait / Leader ...) from the archetype's AllowedRoles and the tier, reassigned when the
//     membership changes;
//   - the focus target per pack (isolation / health / staggered / threat / crowding) with the tier's re-target interval;
//   - ring slots (6, 1.8 - 2.6 m) around the focus target and the 4 - 6 m threat ring for enemies without a token;
//   - pack state (Engage / Pressing / Regroup / Routed) and the retreat to a rally point (tag "MDRallyPoint").
// At WP2 roles and slots only position enemies; the advanced behaviours that use them are WP5. The brains report in
// (ReportMember / ReportDamage) and read their assignment back (GetAssignment); the rules are in MDPackRules.
// UMDAISubsystem's token calls (RequestAttackToken ...) delegate here. Console: md.AI.DebugSlots.
UCLASS()
class MAZEDEMONS_API UMDPackDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMDPackDirectorSubsystem* Get(const UWorld* World);

	// ---- Packs ----
	// Adds Enemy to pack PackId (0 = a new pack of one) and returns the pack id. Joining a pack raises its SpawnCount.
	int32 RegisterEnemy(AActor* Enemy, int32 PackId = 0);

	// Creates (or extends, when PackId already exists) a pack of Enemies with SpawnCount = their number. PackId 0 = allocate.
	int32 RegisterSpawnGroup(const TArray<AActor*>& Enemies, int32 PackId, FName RoomRef);

	// The pack of a level-placed spawn group, created on first use. Enemies with the same GroupKey share a pack.
	int32 GetOrCreateGroupPack(FName GroupKey, FName RoomRef = NAME_None);

	// Death or removal: returns the enemy's token and drops it from its pack (an empty pack is deleted).
	void UnregisterEnemy(AActor* Enemy);

	int32 NumPacks() const { return Packs.Num(); }
	const FMDPack* FindPack(int32 PackId) const;
	const FMDPack* FindPackOf(const AActor* Enemy) const;
	int32 GetPackIdOf(const AActor* Enemy) const;

	// ---- Intelligence tier (V1 WP4, v1-design 3.1) ----
	// Each pack gets its tier ONCE, when its first member joins (the enemy has just spawned): MDTier::Compute over the zone the
	// enemy stands in, the party's perk points and gear upgrades, the player count, the run seed and the pack id, clamped to the
	// archetype's MinTier / MaxTier. It never changes afterwards, so a player who joins late only affects packs spawned later.
	// Merged packs keep the older pack's tier.
	void ComputeTier(FMDPack& Pack, const AActor* Sample);

	// The inputs ComputeTier feeds to MDTier::Compute for an enemy spawning now (public for the tests).
	MDTier::FInputs GatherTierInputs(const AActor* Sample, int32 PackId) const;

	// The tier a pack runs at: md.AI.ForceTier when set, else the tier computed for it (T0 before it has one).
	EMDIntelligenceTier GetPackTier(const FMDPack& Pack) const;

	// GetPackTier of the enemy's pack; md.AI.ForceTier or T0 for an enemy in no pack.
	EMDIntelligenceTier GetTierOf(const AActor* Enemy) const;

	// ---- Reports (from the brains) ----
	void ReportMember(AActor* Enemy, const FMDMemberReport& Report);

	// The enemy took Amount damage from Instigator (feeds the focus score's threat term).
	void ReportDamage(AActor* Enemy, AActor* Instigator, float Amount);

	// Pack-shared awareness (V1 WP3, 2.3 / 2.5): Seer saw (or, for a loud noise, heard) Player at Location at SightTime. Every
	// other member of Seer's pack learns the LAST-KNOWN position (never the live one) at SightTime + max(ShareDelay, its
	// reaction time). ShareDelay < 0 (T0) shares nothing. Returns how many pack mates were told.
	int32 ShareSighting(AActor* Seer, AActor* Player, const FVector& Location, double SightTime, float ShareDelay);

	// ---- Assignment (to the brains) ----
	bool GetAssignment(const AActor* Enemy, FMDEnemyAssignment& Out) const;

	// ---- Tokens (UMDAISubsystem's calls delegate here) ----
	bool RequestToken(AActor* Enemy, AActor* Target);
	bool HoldsToken(const AActor* Enemy) const;
	void ReleaseToken(AActor* Enemy);
	void ReleaseTokenAfter(AActor* Enemy, double Delay);
	// The enemy started its attack: its token can no longer be taken by a higher priority requester.
	void CommitToken(AActor* Enemy);
	int32 CountTokensOn(const AActor* Target) const;

	// ---- Stepping ----
	// One pack update at world time Now (merge, roles, focus, pack state, slots). The tick calls it at 5 Hz; tests call it
	// with their own clock.
	void StepPacks(double Now);

	// Draws the ring slots, the focus link and pack ids (md.AI.DebugSlots 1). Server.
	void DrawSlotsDebug() const;

	// md.AI.DebugSlots != 0
	static bool IsDebugSlotsEnabled();

	double Now() const;

	// UTickableWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	FMDPack* FindPackMutable(int32 PackId);
	FMDPack* FindPackOfMutable(const AActor* Enemy);
	FMDPack& CreatePack(int32 PackId, FName GroupKey, FName RoomRef);
	void AddToPack(FMDPack& Pack, AActor* Enemy);
	void RemovePackIfEmpty(int32 PackId);

	void MergePacks();
	void UpdateRoles(FMDPack& Pack, EMDIntelligenceTier Tier);
	void UpdateFocus(FMDPack& Pack, EMDIntelligenceTier Tier, double Now);
	void UpdatePackState(FMDPack& Pack, EMDIntelligenceTier Tier, double Now);
	void UpdateSlots(FMDPack& Pack);
	void PruneMembers(FMDPack& Pack);

	// Rally points are actors tagged "MDRallyPoint" (AMDRallyPoint), else room entries tagged "MDRoomEntry"; both come from
	// UMDAISubsystem and only revealed rooms count (MDAIQueries::FindRallyPoint). True when the level has a usable one.
	bool HasAnyRallyPoint(double InNow);
	// The rally point nearest to Near; false when none.
	bool FindRallyPoint(const FVector& Near, FVector& OutPoint) const;

	// The distinct players the pack knows, alive.
	void CollectCandidates(const FMDPack& Pack, TArray<AActor*>& Out) const;

	TArray<FMDPack> Packs;
	int32 NextPackId = 1;
	TMap<FName, int32> GroupPacks;

	FMDPackTokenPool Tokens;
	float StepAccumulator = 0.0f;
	MDAIPerf::FFrameProfile Profile;

public:
	// Time spent in this subsystem's Tick (the pack step), for the 40-enemy budget test and tuning.
	const MDAIPerf::FFrameProfile& GetProfile() const { return Profile; }
	void ResetProfile() { Profile = MDAIPerf::FFrameProfile(); }
};
