#include "MDMeleeAbility.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "MDGameplayStatics.h"

UMDMeleeAbility::UMDMeleeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UMDMeleeAbility::ResolveCombat(const AActor* Avatar, UActorComponent*& OutComponent, UFunction*& OutFunction) const
{
	OutComponent = nullptr;
	OutFunction = nullptr;
	if (!Avatar)
	{
		return false;
	}
	if (CachedAvatar.Get() == Avatar && CachedCombat.IsValid() && CachedLightAttack.IsValid())
	{
		OutComponent = CachedCombat.Get();
		OutFunction = CachedLightAttack.Get();
		return true;
	}

	CachedAvatar = Avatar;
	CachedCombat.Reset();
	CachedLightAttack.Reset();
	if (UActorComponent* Component = UMDGameplayStatics::FindCombatComponent(Avatar))
	{
		if (UFunction* Function = Component->FindFunction(FName(TEXT("DoLightAttack"))))
		{
			CachedCombat = Component;
			CachedLightAttack = Function;
			OutComponent = Component;
			OutFunction = Function;
			return true;
		}
	}
	return false;
}

bool UMDMeleeAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const APawn* Pawn = Cast<APawn>(Avatar);
	UActorComponent* CombatComponent = nullptr;
	UFunction* LightAttackFunction = nullptr;
	const bool bHasCombat = ResolveCombat(Avatar, CombatComponent, LightAttackFunction);

	const EMDAttackBlock Block = UMDGameplayStatics::EvaluateAttackGate(Pawn != nullptr, UMDGameplayStatics::IsPawnDead(Pawn),
		Pawn && Pawn->IsLocallyControlled(), UMDGameplayStatics::IsLocalMenuBlockingPawn(Pawn), bHasCombat);
	return Block == EMDAttackBlock::None;
}

void UMDMeleeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UActorComponent* CombatComponent = nullptr;
	UFunction* LightAttackFunction = nullptr;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!ResolveCombat(Avatar, CombatComponent, LightAttackFunction))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot activate melee: no AC_Combat component with DoLightAttack was found."),
			*GetNameSafe(Avatar));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CombatComponent->ProcessEvent(LightAttackFunction, nullptr);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
