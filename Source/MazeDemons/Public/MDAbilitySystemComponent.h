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
	void ApplyDamage(float Damage);

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

protected:
	virtual void BeginPlay() override;

private:
	void RecalculateDefense();

	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData);
	void HandleDefenseChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY()
	TObjectPtr<UMDAttributeSet> MazeDemonsAttributes;

	TMap<FName, float> EquippedArmorDamageReductions;

	float BaseDefense = 0.0f;
};
