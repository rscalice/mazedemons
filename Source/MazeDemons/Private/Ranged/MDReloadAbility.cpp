#include "Ranged/MDReloadAbility.h"

#include "AbilitySystemComponent.h"
#include "MDGameplayTags.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"

UMDReloadAbility::UMDReloadAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationRequiredTags.AddTag(TAG_State_RangedMode.GetTag());
	ActivationBlockedTags.AddTag(TAG_State_Staggered.GetTag());
	ActivationBlockedTags.AddTag(TAG_State_Reloading.GetTag());
}

void UMDReloadAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Only the machine that pressed the key asks (the host starts it directly, a client sends Server_Reload). The server's mirror of a
	// remote client's predicted activation does nothing: the reload request arrives as the RPC and is validated there.
	UMDRangedComponent* Ranged = ActorInfo ? UMDRangedComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	if (Ranged && Ranged->IsOwnerLocal())
	{
		Ranged->RequestReload();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
