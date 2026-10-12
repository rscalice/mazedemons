#include "MDMeleeAbility.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

UMDMeleeAbility::UMDMeleeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UMDMeleeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	UActorComponent* CombatComponent = nullptr;
	UFunction* LightAttackFunction = nullptr;
	if (Avatar)
	{
		TArray<UActorComponent*> Components;
		Avatar->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component)
			{
				if (UFunction* Function = Component->FindFunction(FName(TEXT("DoLightAttack"))))
				{
					CombatComponent = Component;
					LightAttackFunction = Function;
					break;
				}
			}
		}
	}

	if (!CombatComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot activate melee: no AC_Combat component with DoLightAttack was found."),
			*GetNameSafe(Avatar));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CombatComponent->ProcessEvent(LightAttackFunction, nullptr);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
