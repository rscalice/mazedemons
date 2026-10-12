#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDInteractAbility.generated.h"

// Finds the closest actor in front of the avatar and hands it to Blueprint via OnInteractTarget.
// The Blueprint child performs the interaction (door, chest, etc.) and must call EndAbility.
UCLASS(Blueprintable, Abstract)
class MAZEDEMONS_API UMDInteractAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDInteractAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact")
	float InteractRange = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact")
	float InteractRadius = 80.0f;

	// Only actors with this tag are valid targets. None = any actor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact")
	FName RequiredActorTag = NAME_None;

	UFUNCTION(BlueprintImplementableEvent, Category = "Interact")
	void OnInteractTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	AActor* FindInteractTarget() const;
};
