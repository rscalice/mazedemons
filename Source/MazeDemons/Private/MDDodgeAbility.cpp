#include "MDDodgeAbility.h"

#include "AbilitySystemComponent.h"
#include "MDCombatRules.h"
#include "MDComboComponent.h"
#include "MDComboSet.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDPoiseComponent.h"
#include "MDStaminaComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

UMDDodgeAbility::UMDDodgeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

FVector UMDDodgeAbility::GetSlideVelocity(FVector Direction) const
{
	return Direction.GetSafeNormal() * (SlideDistance / FMath::Max(SlideDuration, 0.05f));
}

bool UMDDodgeAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const APawn* Pawn = Cast<APawn>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	const EMDAttackBlock Block = UMDGameplayStatics::EvaluateAttackGate(Pawn != nullptr, UMDGameplayStatics::IsPawnDead(Pawn),
		Pawn && Pawn->IsLocallyControlled(), UMDGameplayStatics::IsLocalMenuBlockingPawn(Pawn), true, UMDPoiseComponent::IsActorStunned(Pawn));
	if (Block != EMDAttackBlock::None)
	{
		return false;
	}

	// Stamina: actors without a stamina component (AI) are never gated.
	if (const UMDStaminaComponent* Stamina = UMDGameplayStatics::GetStaminaComponent(Pawn))
	{
		if (Stamina->IsExhausted() || !Stamina->HasStaminaFor(StaminaCost))
		{
			return false;
		}
	}

	// A swing in progress may only be cancelled from its cancel window.
	if (const UMDComboComponent* Combo = UMDComboComponent::FindFor(Pawn))
	{
		if (!Combo->CanDodgeNow())
		{
			return false;
		}
	}
	return true;
}

void UMDDodgeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APawn* Pawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	if (!Pawn || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const EMDDodgeDirection Direction = UMDCombatRules::DodgeDirectionFromInput(Pawn->GetLastMovementInputVector(), Pawn->GetActorForwardVector());
	FVector SlideDirection = Pawn->GetActorForwardVector();
	switch (Direction)
	{
	case EMDDodgeDirection::Backward:
		SlideDirection = -Pawn->GetActorForwardVector();
		break;
	case EMDDodgeDirection::Left:
		SlideDirection = -Pawn->GetActorRightVector();
		break;
	case EMDDodgeDirection::Right:
		SlideDirection = Pawn->GetActorRightVector();
		break;
	case EMDDodgeDirection::Forward:
	default:
		break;
	}
	SlideDirection.Z = 0.0f;
	SlideDirection = SlideDirection.GetSafeNormal();

	UMDComboComponent* Combo = UMDComboComponent::FindFor(Pawn);
	if (Combo)
	{
		Combo->BeginDodge(Direction, SlideDuration);
	}

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UMDStaminaComponent* Stamina = UMDGameplayStatics::GetStaminaComponent(Pawn))
		{
			Stamina->Consume(StaminaCost);
		}
		float IFrames = IFrameSeconds;
		if (IFrames < 0.0f)
		{
			IFrames = (Combo && Combo->ComboSet) ? Combo->ComboSet->DodgeIFrames : 0.25f;
		}
		BeginIFrames(IFrames);
	}

	OnDodgeSlide(Direction, SlideDirection, SlideDistance, SlideDuration);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SlideTimer, this, &UMDDodgeAbility::FinishSlide, FMath::Max(SlideDuration, 0.05f), false);
	}
	else
	{
		FinishSlide();
	}
}

void UMDDodgeAbility::BeginIFrames(float Seconds)
{
	if (Seconds <= 0.0f)
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = GetWorld();
	if (!ASC || !World)
	{
		return;
	}
	EndIFrames(); // never stack
	ASC->AddLooseGameplayTag(TAG_State_Invulnerable.GetTag());
	IFrameASC = ASC;
	World->GetTimerManager().SetTimer(IFrameTimer, this, &UMDDodgeAbility::EndIFrames, Seconds, false);
}

void UMDDodgeAbility::EndIFrames()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IFrameTimer);
	}
	if (UAbilitySystemComponent* ASC = IFrameASC.Get())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Invulnerable.GetTag());
	}
	IFrameASC.Reset();
}

void UMDDodgeAbility::FinishSlide()
{
	if (IsActive())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UMDDodgeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// However the ability ends (finished, cancelled, owner died), the i-frame tag must not outlive it.
	EndIFrames();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SlideTimer);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
