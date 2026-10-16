#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "MDAbilitySystemComponent.generated.h"

class UMDAttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDAttributeValueChanged, float, OldValue, float, NewValue);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDDeathSignature);

UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UMDAbilitySystemComponent();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	UMDAttributeSet* GetMazeDemonsAttributes() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetAttackPower() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetDefense() const;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void SetArmorPieceDamageReduction(FName ArmorSlot, float DamageReductionPercent);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void RemoveArmorPieceDamageReduction(FName ArmorSlot);

	// Applies raw damage through GAS; Defense mitigation is applied by the attribute set.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	// Server-only. Instigator/causer feed team checks (friendly fire) and the effect context.
	void ApplyDamage(float Damage, AActor* DamageInstigator = nullptr, AActor* DamageCauser = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void ApplyHealing(float Amount);

	// Sets base stats (e.g. for enemies). Health is filled to the new max.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void InitializeStats(float NewMaxHealth, float NewAttackPower, float NewBaseDefense);

	// Grants an ability and returns its handle. Activate with TryActivateAbilityByClass.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	FGameplayAbilitySpecHandle GrantAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	bool IsDead() const;

	// Server-only: refills Health to MaxHealth (used on respawn).
	void ResetHealthToMax();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetStaminaRegenRate() const;

	// Server-only: sets the stamina pool and regen rate; stamina is filled to the new max.
	void InitializeStamina(float NewMaxStamina, float NewRegenRate);

	// Server-only: refills Stamina to MaxStamina (used on respawn).
	void ResetStaminaToMax();

	// Server-only: sets Stamina directly (clamped to [0, MaxStamina]). Used by UMDStaminaComponent.
	void SetStaminaClamped(float NewStamina);

	// Stores the attribute set (created as a default subobject by the owner actor).
	void RegisterAttributeSet(UMDAttributeSet* InSet);

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDDeathSignature OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnMaxHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnAttackPowerChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnDefenseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnMaxStaminaChanged;

	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Server sets this when Health reaches 0 (and clears it when Health > 0). Replicated so every
	// client fires OnDeath exactly once per death via OnRep_IsDeadState.
	UPROPERTY(ReplicatedUsing = OnRep_IsDeadState)
	bool bIsDeadState = false;

	UFUNCTION()
	void OnRep_IsDeadState(bool bOldValue);

	void BindAttributeDelegates();
	bool bDelegatesBound = false;

	void RecalculateDefense();

	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData);
	void HandleDefenseChanged(const FOnAttributeChangeData& ChangeData);
	void HandleStaminaChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxStaminaChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY()
	TObjectPtr<UMDAttributeSet> MazeDemonsAttributes;

	TMap<FName, float> EquippedArmorDamageReductions;

	float BaseDefense = 0.0f;
};
