// R3: R / D-pad Down in ranged mode. A LocalPredicted gameplay ability on the player's ASC (PlayerState). The reload itself runs on the
// server (UMDRangedComponent::BeginReloadServer): it is the reload timer, the cancel on dodge / stagger / death, and the single place
// that moves ammo from the bag into the magazine. The ability is the GAS gate: State.RangedMode required; Staggered and Reloading
// (no double reload) block it.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDReloadAbility.generated.h"

UCLASS()
class MAZEDEMONS_API UMDReloadAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDReloadAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
