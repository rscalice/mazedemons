#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "GameplayEffectTypes.h"
#include "MDPerkTypes.h"
#include "MDUITypes.h"
#include "MDPerkComponent.generated.h"

class AMDPlayerState;
class UMDAbilitySystemComponent;
class UMDStaminaComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDPerksChanged);

// A player's perk ranks and everything that turns them into gameplay. Lives on AMDPlayerState so ranks survive respawns.
// Server-authoritative: clients only request (RequestCommit / RequestRespec). Points are computed from replicated progress
// (AMDGameState::ZonesCompleted / SoulsSaved) plus debug points, never granted one by one.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDPerkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDPerkComponent();

	// The perk component for a PlayerState, pawn or controller (or an actor that hosts one itself).
	static UMDPerkComponent* FindFor(const AActor* Actor);

	// Owner-only. Length MD_NumPerkNodes, index (Path - 1) * 4 + (Tier - 1).
	UPROPERTY(ReplicatedUsing = OnRep_Perks, BlueprintReadOnly, Category = "Maze Demons|Perks")
	TArray<uint8> Ranks;

	// Owner-only, exec-only extra points (GivePerkPoints).
	UPROPERTY(ReplicatedUsing = OnRep_Perks, BlueprintReadOnly, Category = "Maze Demons|Perks")
	int32 DebugBonusPoints = 0;

	// Owner-only: Second Wind was used this life (the panel shows "used").
	UPROPERTY(ReplicatedUsing = OnRep_Perks, BlueprintReadOnly, Category = "Maze Demons|Perks")
	bool bSecondWindUsed = false;

	// Fires on the server after any change and on the owning client after replication (ranks, debug points, Second Wind flag).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Perks")
	FMDPerksChanged OnPerksChanged;

	// ---- Queries (all machines) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPointsEarned() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPointsSpent() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPointsAvailable() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetRank(int32 Node) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPathPoints(EMDPerkPath Path) const;

	// 1 + Potent Draughts bonus; multiplies potion healing.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	float GetPotionHealScale() const;

	// Seconds since the owner last lost health (server value on the server, owner mirror on the owning client).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	float GetTimeSinceDamage() const;

	// The UI greys Confirm with this; the server runs the same rule on commit.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	EMDPerkReject GetCommitReject(const TArray<uint8>& Desired) const;

	// Modifiers for the current ranks, computed on whichever machine asks (used by stat lines and DumpPerks).
	FMDPerkModifiers ComputeCurrentModifiers() const;

	// Reject for a respec at Station (None = allowed): wrong / missing station, out of range, or dead.
	EMDPerkReject GetRespecReject(const AActor* Station) const;

	// ---- Requests (owning client -> server; run directly on the server) ----
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	void RequestCommit(const TArray<uint8>& Desired);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	void RequestRespec(AActor* Station);

	// ---- Server ----
	// First init of a run (called by AMDPlayerState after InitializeStats / InitializeStamina; V3's restart calls it again).
	// Clears ranks, debug points and flags, seeds the crit stream, binds the ASC and applies the (empty) perk set.
	void InitializeForRun();

	// Respawn: a new life gets Second Wind back and cancels a running Second Wind heal. Ranks and points persist.
	void ResetLifeFlags();

	// Recomputes the modifiers from the ranks and applies them: one infinite UMDPerkEffect (previous handle removed), the
	// ASC setters for DR / attack power / combat flags and the stamina regen delay. Never incremental.
	void ApplyToASC();

	// Crit roll from the per-player seeded stream. Consumes one random number only when Chance > 0.
	bool RollCrit(float Chance);

	// Seed = PlayerId XOR md.CritSeed (pure, for tests).
	static int32 ComputeCritSeed(int32 PlayerId, int32 CvarSeed);

	// Debug (exec) helpers; server only. They bypass the commit rules.
	void DebugAddPoints(int32 Delta);
	void DebugSetRank(int32 Node, int32 Rank);
	void DebugReset();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_CommitPerks(const TArray<uint8>& Desired);

	UFUNCTION(Server, Reliable)
	void Server_Respec(AActor* Station);

	UFUNCTION()
	void OnRep_Perks();

	// Bound to the ASC's OnHealthChanged: Stalwart flips on / off as health crosses the threshold.
	UFUNCTION()
	void HandleHealthChanged(float OldValue, float NewValue);

private:
	bool IsServer() const;
	AMDPlayerState* GetPlayerState() const;
	UMDAbilitySystemComponent* GetASC() const;
	UMDStaminaComponent* GetStamina() const;
	bool IsOwnerDead() const;
	void EnsureRanks();
	void SendToast(const FText& Message, EMDColor Tone) const;
	bool Commit_Impl(const TArray<uint8>& Desired);
	bool Respec_Impl(AActor* Station);
	void PushDefense();
	void PushCombatState();
	bool RecomputeStalwartFlag();
	void HandleSecondWindTriggered();
	void TickSecondWind();
	void TickRegen();
	void BindToASC();

	// Last modifier set pushed by ApplyToASC (server).
	FMDPerkModifiers AppliedMods;
	FActiveGameplayEffectHandle PerkEffectHandle;
	bool bStalwartActive = false;
	bool bBoundToASC = false;

	FRandomStream CritStream;
	bool bCritSeeded = false;

	FTimerHandle RegenTimer;
	FTimerHandle SecondWindTimer;
	float SecondWindHealRemaining = 0.0f;
	float SecondWindHealPerSecond = 0.0f;
};
