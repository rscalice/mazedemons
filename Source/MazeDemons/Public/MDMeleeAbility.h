#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
class UActorComponent;
class UFunction;

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

	// Refuses before anything commits: no pawn, dead, a local menu open, or no AC_Combat (see UMDGameplayStatics::CanPawnAttack).
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

private:
	// Finds AC_Combat and DoLightAttack on Avatar, caching them per avatar (re-resolved after a respawn).
	bool ResolveCombat(const AActor* Avatar, UActorComponent*& OutComponent, UFunction*& OutFunction) const;

	mutable TWeakObjectPtr<const AActor> CachedAvatar;
	mutable TWeakObjectPtr<UActorComponent> CachedCombat;
	mutable TWeakObjectPtr<UFunction> CachedLightAttack;
};
