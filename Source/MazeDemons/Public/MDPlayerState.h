#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "MDPlayerState.generated.h"

class UMDAbilitySystemComponent;
class UMDAttributeSet;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "1"))
	float StartingMaxHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "0"))
	float StartingAttackPower = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Stats", meta = (ClampMin = "0", ClampMax = "90"))
	float StartingBaseDefense = 0.0f;

protected:
	virtual void BeginPlay() override;

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
