#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDStaminaComponent.generated.h"

class UMDAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDExhaustedChanged, bool, bNowExhausted);

// Inputs/outputs of one stamina simulation step. Plain data so the maths can be unit-tested without a world.
struct FMDStaminaStepInput
{
	float Stamina = 0.0f;
	float MaxStamina = 100.0f;
	float RegenPerSec = 20.0f;
	float RegenDelay = 1.0f;
	float SprintDrainPerSec = 15.0f;
	float ExhaustedRecoverFraction = 0.3f;
	float DeltaTime = 0.1f;
	float TimeSinceSpend = 0.0f; // seconds since stamina was last spent
	bool bSprinting = false;
	bool bExhausted = false;
};

struct FMDStaminaStepOutput
{
	float Stamina = 0.0f;
	bool bExhausted = false;
	bool bSpent = false; // true when this step drained stamina (restarts the regen delay)
};

// Server-authoritative stamina rules for a player. Lives on AMDPlayerState so it survives respawns. Stamina is an
// attribute (UMDAttributeSet) on the PlayerState ASC; this component drains/regenerates it and replicates the
// exhausted flag to everyone. Stamina only gates sprint, jump and vault - attacks never cost or check stamina.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDStaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDStaminaComponent();

	// ---- Tunables (see assets/MazeDemons/BALANCE.md) ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "1"))
	float StartingMaxStamina = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float RegenPerSec = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float RegenDelay = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float SprintDrainPerSec = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float JumpCost = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float VaultCost = 15.0f;

	// Fraction of max stamina that must be regained before an exhausted player can sprint/jump/vault again.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0", ClampMax = "1"))
	float ExhaustedRecoverFraction = 0.3f;

	// 2D speed (cm/s) at or above which movement on the ground counts as sprinting and drains stamina.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float SprintSpeedThreshold = 550.0f;

	// Grace (seconds) during which the server still lets a REMOTE client's pawn sprint after exhaustion starts, so a
	// client that has not yet received the exhausted flag is not corrected (rubber-banded).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0"))
	float SprintGateGrace = 0.25f;

	// Server simulation interval in seconds.
	UPROPERTY(EditDefaultsOnly, Category = "Maze Demons|Stamina", meta = (ClampMin = "0.02"))
	float SimulationInterval = 0.1f;

	// ---- Queries (all machines) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool IsExhausted() const { return bExhausted; }

	// True when sprinting is currently allowed (not exhausted).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool IsSprintAllowed() const { return !bExhausted; }

	// Movement-gate version of IsSprintAllowed. The owning client is strict; the server gives a remote client's pawn
	// SprintGateGrace seconds after exhaustion starts (see above).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool IsSprintAllowedFor(bool bPawnLocallyControlled) const;

	// Cost is the unscaled cost; the perk StaminaCostScale attribute (Light Step) is applied here.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool HasStaminaFor(float Cost) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool CanJump() const { return !bExhausted && HasStaminaFor(JumpCost); }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	bool CanVault() const { return !bExhausted && HasStaminaFor(VaultCost); }

	// ---- Authority only ----
	// Seconds removed from RegenDelay by perks (Fleet). Server only: the delay lives in the server simulation. Never negative overall.
	void SetRegenDelayModifier(float ReductionSeconds);

	// RegenDelay minus the perk reduction, never below 0.
	float GetEffectiveRegenDelay() const;

	// Spends stamina, clamped at 0 (the server never refuses a jump/vault the owning client already started).
	// Restarts the regen delay and flags exhaustion at 0. Cost is unscaled; StaminaCostScale is applied here.
	void Consume(float Cost);

	// Charged by the movement layer when a jump / traversal actually starts.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Stamina")
	void NotifyJumped();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Stamina")
	void NotifyTraversalStarted();

	// Clears exhaustion and the regen delay (respawn). Stamina itself is refilled via the ASC.
	void ResetState();

	// Authority only: full respawn reset. Stamina = Max, exhausted cleared, State.Sprinting / State.Exhausted loose
	// tags forced to 0 and abilities carrying the sprint tag cancelled, so a new life never inherits a stuck sprint.
	void ResetForRespawn();

	// One simulation step; pure, used by TickComponent and by the automation tests.
	static FMDStaminaStepOutput Step(const FMDStaminaStepInput& In);

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Stamina")
	FMDExhaustedChanged OnExhaustedChanged;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Exhausted)
	bool bExhausted = false;

	UFUNCTION()
	void OnRep_Exhausted();

	UMDAbilitySystemComponent* GetASC() const;
	bool IsPawnSprinting() const;
	void SetExhausted(bool bNewExhausted);

	// Stamina cost multiplier from the ASC attribute (1 without perks), clamped to the BALANCE floor.
	float ScaleCost(float Cost) const;

	float RegenDelayReduction = 0.0f; // server only
	double LastSpendTime = -1000.0;
	double ExhaustedStartTime = -1000.0; // server only
	bool bSprintTagged = false;
};
