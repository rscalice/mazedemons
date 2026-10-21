#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "MDAttributeSet.generated.h"

UCLASS(BlueprintType)
class MAZEDEMONS_API UMDAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMDAttributeSet();

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, AttackPower)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, Defense)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_IncomingDamage)
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, IncomingDamage)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, MaxStamina)

	// Stamina regenerated per second once the regen delay has passed.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_StaminaRegenRate)
	FGameplayAttributeData StaminaRegenRate;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, StaminaRegenRate)

	// ---- Perk attributes (U3). Defaults are the "no perks" values; hard caps are BALANCE section 7. ----

	// Probability of a critical hit, 0 .. 0.25.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Perks", ReplicatedUsing = OnRep_CritChance)
	FGameplayAttributeData CritChance;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, CritChance)

	// Critical hit damage multiplier, 1 .. 2.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Perks", ReplicatedUsing = OnRep_CritDamage)
	FGameplayAttributeData CritDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, CritDamage)

	// Health per second restored out of combat, 0 .. 1.5.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Perks", ReplicatedUsing = OnRep_HealthRegen)
	FGameplayAttributeData HealthRegen;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, HealthRegen)

	// Multiplier on sprint drain, jump and vault stamina costs, 0.7 .. 1.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Perks", ReplicatedUsing = OnRep_StaminaCostScale)
	FGameplayAttributeData StaminaCostScale;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, StaminaCostScale)

	// Multiplier on sprint speed, 1 .. 1.05.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Perks", ReplicatedUsing = OnRep_SprintSpeedScale)
	FGameplayAttributeData SprintSpeedScale;
	ATTRIBUTE_ACCESSORS_BASIC(UMDAttributeSet, SprintSpeedScale)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

protected:
	// Clamps one of the perk attributes (and MaxStamina / StaminaRegenRate caps) to its hard cap. False when Attribute is not one of them.
	bool ClampCappedAttribute(const FGameplayAttribute& Attribute, float& Value) const;

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_AttackPower(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Defense(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_IncomingDamage(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_CritChance(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_CritDamage(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_HealthRegen(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_StaminaCostScale(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_SprintSpeedScale(const FGameplayAttributeData& OldValue);
};
