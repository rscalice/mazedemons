#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "MDStatusEffects.h"
#include "MDStatusTypes.h"
#include "MDStatusEffectComponent.generated.h"

class UCharacterMovementComponent;
class UMDAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDStatusesChanged);

// V1 WP6: the status effects (Bleed, Frost, Poison, Burn) of one creature. Lives next to the creature's ASC: on AMDPlayerState for players,
// on the enemy / dummy itself. Server authoritative: only the server applies, stacks, ticks and ends statuses (FMDStatusRules); the active
// set is replicated to everyone, so a late joiner sees it and the HUD / enemy bars just read it.
//
//   - DoT ticks go through UMDCombatStatics::ApplyTypedDamage -> UMDAbilitySystemComponent::ApplyDoTDamage -> UMDDoTEffect / UMDDoTExecCalc:
//     Defense applies, no armor / durability wear, never a crit, never a poise reaction.
//   - Frost: GetMoveSpeedScale() is applied to the pawn's CharacterMovement on every machine (see ApplyMovementScale); the stamina component
//     reads GetStaminaDrainScale() (Frost +20% drain) and BlocksStaminaRegen() (Poison).
//   - Each status also holds a GE (UMDStatusEffect_<Name>) on the owner's ASC that grants the Status.<Name> tag while it lasts.
//
// R6 hook (ranged-design): infusions will add a per-element build-up meter and call ApplyStatus when it reaches 100. ApplyStatus is the one
// entry point everything goes through; nothing else mutates the active set.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDStatusEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDStatusEffectComponent();

	// The status component that acts for Actor: the actor's own, else its pawn's / controller's PlayerState's. Null for actors without one.
	static UMDStatusEffectComponent* FindFor(const AActor* Actor);

	// Server only. Finds the target's status component and applies Type (see ApplyStatus). False for a null / dead target or no component.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Status")
	static bool ApplyStatusToActor(AActor* Target, EMDStatusType Type, AActor* Instigator = nullptr);

	// ---- Server only ----

	// Applies Type: a new status starts, an active one is refreshed and (Bleed) gains a stack up to the cap. Team rules are not checked here
	// (the sources do), but a dead owner never gains a status. Returns true when the active set changed.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Status")
	bool ApplyStatus(EMDStatusType Type, AActor* Instigator = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Status")
	void ClearStatus(EMDStatusType Type);

	// Death and respawn: every status ends.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Status")
	void ClearAll();

	// Moves the status clock: due DoT ticks hit the owner, finished statuses end. TickComponent calls it every frame while anything is active;
	// the automation tests call it directly.
	void AdvanceStatuses(float DeltaSeconds);

	// ---- Queries (all machines; read the replicated set) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	const TArray<FMDStatusRep>& GetActiveStatuses() const { return Statuses; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	bool HasStatus(EMDStatusType Type) const { return FindRep(Type) != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	int32 GetStacks(EMDStatusType Type) const;

	// Seconds until Type ends (0 when not active). Uses the server clock, so it is the same on every machine.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	float GetRemainingSeconds(EMDStatusType Type) const;

	// 1 without Frost, 0.75 with it. Applied to CharacterMovement by ApplyMovementScale.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	float GetMoveSpeedScale() const { return FMDStatusRules::MoveSpeedScale(Statuses); }

	// Multiplier on stamina costs and drain (1.2 with Frost).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	float GetStaminaDrainScale() const { return FMDStatusRules::StaminaDrainScale(Statuses); }

	// True while Poison is active.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Status")
	bool BlocksStaminaRegen() const { return FMDStatusRules::BlocksStaminaRegen(Statuses); }

	// Fires on every machine whenever the active set changes (apply, stack, refresh, end, replication). The player HUD and the future
	// enemy health bars (WP8) bind to it and read GetActiveStatuses().
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Status")
	FMDStatusesChanged OnStatusesChanged;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FMDStatusTestAccess;

	UPROPERTY(ReplicatedUsing = OnRep_Statuses)
	TArray<FMDStatusRep> Statuses;

	UFUNCTION()
	void OnRep_Statuses();

	UFUNCTION()
	void HandleOwnerDeath();

	const FMDStatusRep* FindRep(EMDStatusType Type) const;
	UMDAbilitySystemComponent* GetASC() const;
	// The actor a tick hits and whose movement Frost slows: the pawn for a PlayerState owner, else the owner.
	AActor* GetAvatar() const;
	double GetServerNow() const;

	// Rebuilds the replicated set from the server state; broadcasts when it changed.
	void SyncReplicated();
	void UpdateTickEnabled();
	void ApplyMovementScale();
	void AddTagEffect(EMDStatusType Type);
	void RemoveTagEffect(EMDStatusType Type);

	TArray<FMDStatusRuntime> Active; // server only
	TMap<EMDStatusType, FActiveGameplayEffectHandle> TagHandles; // server only
	bool bDeathBound = false;

	// Frost slow: the speed we wrote to CharacterMovement last frame and the speed it was derived from.
	TWeakObjectPtr<UCharacterMovementComponent> SlowedMovement;
	float LastBaseSpeed = 0.0f;
	float LastWrittenSpeed = 0.0f;
	bool bSpeedScaled = false;
};
