#pragma once

#include "CoreMinimal.h"
#include "MDAttributeSet.h"
#include "MDEnemyAttributeSet.generated.h"

// Enemy attributes. Health and MaxHealth are the inherited UMDAttributeSet ones, on purpose: the whole C1 damage path
// (UMDAbilitySystemComponent::ApplyDamage, UMDDamageEffect, kill report, GetHealth) reads that class, and two Health attributes
// on one ASC would be ambiguous. This set adds the poise mirror so health bars (V1 WP8) can bind to attributes.
//
// Poise stays owned by UMDPoiseComponent (C1 decision 3: hidden meter, replicated component). These two attributes are a
// replicated mirror the enemy character refreshes after each reaction; nothing reads them for gameplay.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDEnemyAttributeSet : public UMDAttributeSet
{
	GENERATED_BODY()

public:
	UMDEnemyAttributeSet();

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Poise", ReplicatedUsing = OnRep_Poise)
	FGameplayAttributeData Poise;
	ATTRIBUTE_ACCESSORS_BASIC(UMDEnemyAttributeSet, Poise)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Poise", ReplicatedUsing = OnRep_MaxPoise)
	FGameplayAttributeData MaxPoise;
	ATTRIBUTE_ACCESSORS_BASIC(UMDEnemyAttributeSet, MaxPoise)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Poise(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxPoise(const FGameplayAttributeData& OldValue);
};
