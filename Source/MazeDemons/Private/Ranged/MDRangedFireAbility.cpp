#include "Ranged/MDRangedFireAbility.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "MDGameplayTags.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"

UMDRangedFireAbility::UMDRangedFireAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationRequiredTags.AddTag(TAG_State_RangedMode.GetTag());
	ActivationBlockedTags.AddTag(TAG_State_Staggered.GetTag());
}

void UMDRangedFireAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// The client that pulled the trigger (and the host) does the work. The server's mirror instance of a remote client's predicted
	// activation only confirms the prediction key: the shot itself arrives as Server_Fire and is validated there.
	UMDRangedComponent* Ranged = ActorInfo ? UMDRangedComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	if (Ranged && Ranged->IsOwnerLocal())
	{
		Ranged->FireShotLocal();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
