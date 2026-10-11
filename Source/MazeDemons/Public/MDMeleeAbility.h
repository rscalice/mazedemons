#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDMeleeAbility.generated.h"

// Triggers the owner's AC_Combat.DoLightAttack, then applies a delayed melee sweep.
UCLASS(Blueprintable, Abstract)
class MAZEDEMONS_API UMDMeleeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDMeleeAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	float BonusDamage = 0.0f;

	// Seconds after activation when the hit sweep happens (align with the swing).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	float HitDelay = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	float TraceRange = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
	float TraceRadius = 60.0f;

	// Fired after each damaged target, for hit VFX/SFX in Blueprint.
	UFUNCTION(BlueprintImplementableEvent, Category = "Melee")
	void OnTargetHit(AActor* Target, float DamageDealt);

protected:
	UFUNCTION()
	void HandleHitWindow();
};
