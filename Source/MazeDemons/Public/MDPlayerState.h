#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "MDRunStats.h"
#include "MDPlayerState.generated.h"

class UMDAbilitySystemComponent;
class UMDAttributeSet;
class UMDPerkComponent;
class UMDStaminaComponent;
class USkeletalMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDRunStatsChanged);

// Owns the player's ASC and attributes so stats/abilities persist across pawn respawns.
UCLASS()
class MAZEDEMONS_API AMDPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMDPlayerState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	UMDAbilitySystemComponent* GetMDAbilitySystemComponent() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|GAS")
	TObjectPtr<UMDAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|GAS")
	TObjectPtr<UMDAttributeSet> Attributes;

	// Server-side stamina rules (drain/regen/exhaustion). Pool size and regen come from its tunables.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|Stamina")
	TObjectPtr<UMDStaminaComponent> StaminaComponent;

	// Perk ranks and everything they drive (U3). Ranks persist across respawns.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|Perks")
	TObjectPtr<UMDPerkComponent> PerkComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "1"))
	float StartingMaxHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "0"))
	float StartingAttackPower = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "0", ClampMax = "90"))
	float StartingBaseDefense = 0.0f;

	// Authority only: copies the pawn's current Torso/Hands/Legs/Feet meshes into the replicated armor
	// properties and applies them. Call from Blueprint after equip/unequip.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Armor")
	void SyncArmorFromPawn();

	// ---- Run stats (Run tab). Replicated to the owning client only. ----
	UPROPERTY(ReplicatedUsing = OnRep_RunStats, BlueprintReadOnly, Category = "Maze Demons|Run")
	FMDPlayerRunStats RunStats;

	// Fires on the server after a change and on the owning client after replication.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Run")
	FMDRunStatsChanged OnRunStatsChanged;

	// Authority only; each ignores bad input (see FMDPlayerRunStats::AddAmount / AddCount).
	void AddDamageDealt(float Amount);
	void AddDamageTaken(float Amount);
	void AddKill();
	void AddGoldEarned(int32 Amount);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RunStats();

	virtual void BeginPlay() override;

	UPROPERTY(ReplicatedUsing = OnRep_Armor)
	TObjectPtr<USkeletalMesh> TorsoMesh;

	UPROPERTY(ReplicatedUsing = OnRep_Armor)
	TObjectPtr<USkeletalMesh> HandsMesh;

	UPROPERTY(ReplicatedUsing = OnRep_Armor)
	TObjectPtr<USkeletalMesh> LegsMesh;

	UPROPERTY(ReplicatedUsing = OnRep_Armor)
	TObjectPtr<USkeletalMesh> FeetMesh;

	UFUNCTION()
	void OnRep_Armor();

	// The pawn that died. Only this exact pawn is ever ragdolled, so a respawn pawn is never mistaken for a corpse
	// while the replicated death flag lags behind (late join, respawn). Server sets it on death and clears it when a
	// different pawn is possessed.
	UPROPERTY(ReplicatedUsing = OnRep_DeadPawn)
	TObjectPtr<APawn> DeadPawn;

	UFUNCTION()
	void OnRep_DeadPawn();

	void ApplyArmorVisuals();

	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);

	// Bound to the ASC's OnDeath on every machine; ragdolls the current pawn locally.
	UFUNCTION()
	void HandleDeath();

private:
	void RagdollPawn(APawn* Pawn);

	bool bStatsInitialized = false;
	bool bDeathBound = false;
};
