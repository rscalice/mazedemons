// R1: LMB in ranged mode. A LocalPredicted gameplay ability on the player's ASC (PlayerState). One activation = one shot attempt:
// the owning client builds the shot request, plays the cosmetics at once and sends Server_Fire; the server validates and spawns the
// projectile (UMDRangedComponent::ProcessShot). Held fire (the repeater) activates the ability again every shot from the component's
// tick; the gating tags (State.RangedMode required, Staggered and Overheated block) are the GAS side of the same rules ValidateShot enforces.
// R5: for a beam weapon (Duskray) the activation starts the hold-to-fire beam instead (UMDBeamComponent::OwnerStartBeam); the trigger stays
// down, the component keeps the beam going and ends it on release, overheat or a closed gate.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDRangedFireAbility.generated.h"

UCLASS()
class MAZEDEMONS_API UMDRangedFireAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDRangedFireAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
