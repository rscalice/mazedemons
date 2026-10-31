// R4: RMB / left trigger in ranged mode. A LocalPredicted gameplay ability on the player's ASC (PlayerState), the GAS gate for aiming:
// State.RangedMode required, Staggered blocks. It only asks for the aim; the state lives in UMDRangedComponent (replicated bAimed, the
// loose State.Aiming tag on every machine, the camera preset blend and the spread multiplier all follow it). The server validates the
// request (FMDAimRules::CanAim) in Server_SetAim, so a refused aim never reaches other clients. Releasing the button needs no ability.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MDAimAbility.generated.h"

UCLASS()
class MAZEDEMONS_API UMDAimAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMDAimAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
