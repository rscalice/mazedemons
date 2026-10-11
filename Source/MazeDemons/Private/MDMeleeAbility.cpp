#include "MDMeleeAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "MDAbilitySystemComponent.h"

UMDMeleeAbility::UMDMeleeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
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

	UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, FMath::Max(HitDelay, 0.01f));
	DelayTask->OnFinish.AddDynamic(this, &UMDMeleeAbility::HandleHitWindow);
	DelayTask->ReadyForActivation();
}

void UMDMeleeAbility::HandleHitWindow()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	float AttackPower = 0.0f;
	if (const UMDAbilitySystemComponent* ASC = Cast<UMDAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		AttackPower = ASC->GetAttackPower();
	}
	const float Damage = AttackPower * DamageMultiplier + BonusDamage;

	const FVector Start = Avatar->GetActorLocation();
	const FVector End = Start + Avatar->GetActorForwardVector() * TraceRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDMelee), false, Avatar);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects),
		FCollisionShape::MakeSphere(TraceRadius), Params);

	TSet<AActor*> Damaged;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!Target || Damaged.Contains(Target))
		{
			continue;
		}
		Damaged.Add(Target);

		// Targets with their own GAS take damage through it (mitigation applies);
		// others get engine damage so legacy Blueprint damage handlers still fire.
		if (UMDAbilitySystemComponent* TargetASC = Target->FindComponentByClass<UMDAbilitySystemComponent>())
		{
			TargetASC->ApplyDamage(Damage);
		}
		else
		{
			UGameplayStatics::ApplyDamage(Target, Damage, Avatar->GetInstigatorController(), Avatar, nullptr);
		}
		OnTargetHit(Target, Damage);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
