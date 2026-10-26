#include "MDMeleeAbility.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "MDComboComponent.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

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
	const UMDComboComponent* Combo = UMDComboComponent::FindFor(Avatar);
	const bool bHasComboSet = Combo && Combo->HasComboSet();
	const bool bHasCombat = ResolveCombat(Avatar, CombatComponent, LightAttackFunction) || bHasComboSet;

	const EMDAttackBlock Block = UMDGameplayStatics::EvaluateAttackGate(Pawn != nullptr, UMDGameplayStatics::IsPawnDead(Pawn),
		Pawn && Pawn->IsLocallyControlled(), UMDGameplayStatics::IsLocalMenuBlockingPawn(Pawn), bHasCombat,
		UMDPoiseComponent::IsActorStunned(Pawn));
	return Block == EMDAttackBlock::None;
}

void UMDMeleeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UActorComponent* CombatComponent = nullptr;
	UFunction* LightAttackFunction = nullptr;
	AActor* Avatar = GetAvatarActorFromActorInfo();

	// C1 path: the combo component drives the swing.
	if (UMDComboComponent* Combo = UMDComboComponent::FindFor(Avatar))
	{
		if (Combo->HasComboSet())
		{
			if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}

			// Input is processed where the player is; the server gets it from the combo component's own RPC, so the server's
			// copy of this predicted ability (remote pawn, not locally controlled) has nothing to do.
			const APawn* Pawn = Cast<APawn>(Avatar);
			if (!Pawn || !Pawn->IsLocallyControlled())
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
				return;
			}

			switch (InputKind)
			{
			case EMDComboInput::Heavy:
				bHeavyHeld = true;
				Combo->HeavyPressed();
				if (const UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(HeavyTimer, this, &UMDMeleeAbility::OnHeavyTimeout, FMath::Max(MaxHeavyHold, 0.5f), false);
				}
				return; // stays active until the button is released
			case EMDComboInput::Dodge:
				Combo->RequestInput(EMDComboInput::Dodge);
				break;
			case EMDComboInput::Light:
			case EMDComboInput::None:
			default:
				Combo->RequestInput(EMDComboInput::Light);
				break;
			}
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}
	}

	if (InputKind != EMDComboInput::Light)
	{
		// Heavy / dodge need a combo set; the old path only knows the light attack.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

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

void UMDMeleeAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);
	if (bHeavyHeld)
	{
		if (UMDComboComponent* Combo = UMDComboComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			bHeavyHeld = false;
			Combo->HeavyReleased();
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
	}
}

void UMDMeleeAbility::OnHeavyTimeout()
{
	if (bHeavyHeld && IsActive())
	{
		InputReleased(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo());
	}
}

void UMDMeleeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeavyTimer);
	}
	if (bHeavyHeld)
	{
		// Cancelled while the button was down (stagger, death): the combo component was reset by the interrupt; just forget the hold.
		bHeavyHeld = false;
		if (UMDComboComponent* Combo = UMDComboComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			Combo->NotifyInterrupted();
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
