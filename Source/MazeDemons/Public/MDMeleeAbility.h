#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "MDCombatTypes.h"
class UActorComponent;
class UFunction;

#include "MDMeleeAbility.generated.h"

// Melee input ability. With a UMDComboComponent that has a combo set (C1) it forwards its InputKind to the combo component
// (Light / Heavy / Dodge); Heavy stays active while the button is held and sends the hold time on release (tap = heavy, held for
// the set's ChargeTime = charged). Without a combo set it falls back to the old path: the owner's AC_Combat.DoLightAttack
// (Light only). AC_Combat owns the weapon trace and damage either way.
UCLASS(Blueprintable, Abstract)
class MAZEDEMONS_API UMDMeleeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDMeleeAbility();

	// GA_PlayerMelee = Light, GA_PlayerHeavy = Heavy. Dodge normally uses UMDDodgeAbility (stamina, i-frames); this kind only
	// feeds the combo state machine.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	EMDComboInput InputKind = EMDComboInput::Light;

	// Heavy only: the ability ends by itself after this many seconds if the release never arrives.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0.5"))
	float MaxHeavyHold = 3.0f;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	// Refuses before anything commits: no pawn, dead, a local menu open, or no AC_Combat (see UMDGameplayStatics::CanPawnAttack).
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void OnHeavyTimeout();

	bool bHeavyHeld = false;
	FTimerHandle HeavyTimer;

	// Finds AC_Combat and DoLightAttack on Avatar, caching them per avatar (re-resolved after a respawn).
	bool ResolveCombat(const AActor* Avatar, UActorComponent*& OutComponent, UFunction*& OutFunction) const;

	mutable TWeakObjectPtr<const AActor> CachedAvatar;
	mutable TWeakObjectPtr<UActorComponent> CachedCombat;
	mutable TWeakObjectPtr<UFunction> CachedLightAttack;
};
