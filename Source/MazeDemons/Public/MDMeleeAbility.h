#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDMeleeAbility.generated.h"

// Triggers the owner's AC_Combat.DoLightAttack; AC_Combat owns the weapon trace and damage.
UCLASS(Blueprintable, Abstract)
class MAZEDEMONS_API UMDMeleeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDMeleeAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
