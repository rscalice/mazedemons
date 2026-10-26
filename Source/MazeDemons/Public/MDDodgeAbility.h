#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "MDCombatTypes.h"
#include "MDDodgeAbility.generated.h"

// Base for GA_PlayerDodge: a fast ground slide (4 directions) with i-frames, replacing a roll. Same stamina cost as the design
// (Decision 4: dodge counts as movement; attacks stay free).
//
// Activation: refused when dead, staggered, a menu is open, exhausted, short on stamina, or mid-swing outside the cancel window.
// On the server it spends stamina and grants State.Invulnerable for the i-frame time. The slide itself is driven in Blueprint:
// OnDodgeSlide gives the world direction, distance and duration (play the slide montage, or drive LaunchCharacter / a curve with
// GetSlideVelocity). The ability ends after SlideDuration. The combo component ends the swing, plays the set's slide montage
// when it has one, and starts the after-image trail.
UCLASS(Blueprintable, Abstract)
class MAZEDEMONS_API UMDDodgeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDDodgeAbility();

	// Unscaled stamina cost (the perk StaminaCostScale is applied by the stamina component).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge", meta = (ClampMin = "0"))
	float StaminaCost = 15.0f;

	// Seconds of invulnerability from the start of the slide. Negative = use the combo set's DodgeIFrames.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge")
	float IFrameSeconds = -1.0f;

	// How far the slide travels (cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge", meta = (ClampMin = "0"))
	float SlideDistance = 450.0f;

	// How long the slide lasts (s); the ability ends after this.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge", meta = (ClampMin = "0.05"))
	float SlideDuration = 0.45f;

	// Average slide speed (cm/s) along Direction; use it with LaunchCharacter or a braking curve.
	UFUNCTION(BlueprintPure, Category = "Dodge")
	FVector GetSlideVelocity(FVector Direction) const;

	// Called on every machine that runs the ability once the dodge starts. SlideDirection is a unit world vector.
	UFUNCTION(BlueprintImplementableEvent, Category = "Dodge")
	void OnDodgeSlide(EMDDodgeDirection Direction, FVector SlideDirection, float Distance, float Duration);

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void BeginIFrames(float Seconds);
	void EndIFrames();
	void FinishSlide();

	FTimerHandle IFrameTimer;
	FTimerHandle SlideTimer;
	TWeakObjectPtr<UAbilitySystemComponent> IFrameASC;
};
