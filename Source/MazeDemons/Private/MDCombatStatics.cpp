#include "MDCombatStatics.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatDebug.h"
#include "MDCombatFeedbackComponent.h"
#include "MDCombatRules.h"
#include "MDComboComponent.h"
#include "MDComboSet.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "MDPoiseProfile.h"
#include "MDUISettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

bool UMDCombatStatics::ApplyMeleeHit(AActor* Attacker, AActor* Victim, const FHitResult& Hit)
{
	if (!Attacker || !Victim || Attacker == Victim || !Victim->HasAuthority())
	{
		return false;
	}
	UMDAbilitySystemComponent* VictimASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Victim);
	UMDAbilitySystemComponent* AttackerASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Attacker);
	if (!VictimASC || !AttackerASC)
	{
		return false;
	}

	UMDComboComponent* Combo = UMDComboComponent::FindFor(Attacker);
	FMDAttackStep Step; // default: a plain light hit
	const bool bHasStep = Combo && Combo->GetActiveStep(Step);
	if (bHasStep && !Combo->TryRegisterSwingHit(Victim))
	{
		return false; // this swing already hit this target
	}

	FMDHitContext Context;
	Context.Instigator = Attacker;
	Context.Causer = Attacker;
	Context.BaseDamage = AttackerASC->GetAttackPower();
	Context.Kind = bHasStep ? Combo->GetActiveKind() : EMDAttackKind::Light;
	Context.Step = Step;
	Context.PoiseDamage = Step.PoiseDamage;
	Context.Reaction = Step.Reaction;
	Context.Knockback = Step.KnockbackStrength;
	Context.ImpactPoint = Hit.ImpactPoint.IsNearlyZero() ? Victim->GetActorLocation() : FVector(Hit.ImpactPoint);
	Context.ImpactNormal = Hit.ImpactNormal;
	Context.AttackerLocation = Attacker->GetActorLocation();
	Context.ImpactTag = Step.ImpactTag;
	Context.PhysMaterial = Hit.PhysMaterial.Get();

	FMDDamageResult Damage;
	if (!VictimASC->ApplyDamage(Context, Damage))
	{
		return false; // blocked (team rules, i-frames, dead) or fully absorbed
	}

	if (MDCombatDebug::ShouldDrawTraces())
	{
		DrawDebugSphere(Victim->GetWorld(), Context.ImpactPoint, 6.0f, 8, Damage.bCrit ? FColor::Yellow : FColor::Red, false, 0.5f);
	}

	// Poise and reaction.
	UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Victim);
	FMDPoiseHitResult PoiseResult;
	if (Poise)
	{
		PoiseResult = Poise->ApplyHit(Context, Damage);
	}
	const EMDHitReaction Reaction = PoiseResult.bValid ? PoiseResult.Reaction : Context.Reaction;

	// Knockback: only when the hit staggered (a flinch or hyper-armor does not push).
	if (Context.Knockback > 0.0f && static_cast<int32>(Reaction) >= static_cast<int32>(EMDHitReaction::Stagger))
	{
		const APawn* VictimPawn = Cast<APawn>(Victim);
		const bool bPlayer = VictimPawn && VictimPawn->GetPlayerState() != nullptr;
		const float Resist = Poise ? Poise->GetProfile()->KnockbackResist : 0.0f;
		const FVector Velocity = UMDCombatRules::KnockbackVelocity(Context.Knockback, Context.AttackerLocation, Victim->GetActorLocation(), bPlayer, Resist);
		if (Poise)
		{
			Poise->ApplyKnockback(Velocity, bPlayer);
		}
		else if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim))
		{
			VictimCharacter->LaunchCharacter(Velocity, true, !bPlayer);
		}
	}

	// Tell the attacker: hit-stop, shake, rumble, damage number.
	UMDCombatFeedbackComponent* Feedback = nullptr;
	if (const APawn* AttackerPawn = Cast<APawn>(Attacker))
	{
		if (const AController* Controller = AttackerPawn->GetController())
		{
			Feedback = Controller->FindComponentByClass<UMDCombatFeedbackComponent>();
		}
	}
	if (!Feedback)
	{
		Feedback = Attacker->FindComponentByClass<UMDCombatFeedbackComponent>();
	}
	if (Feedback)
	{
		const float StopScale = (Combo && Combo->ComboSet) ? Combo->ComboSet->HitStopScale : 1.0f;
		FMDHitConfirm Confirm;
		Confirm.Victim = Victim;
		Confirm.Damage = Damage.Applied;
		Confirm.bCrit = Damage.bCrit;
		Confirm.Location = Context.ImpactPoint;
		Confirm.HitStopFrames = FMath::RoundToInt(static_cast<float>(Step.HitStopFrames) * StopScale);
		Confirm.ShakeScale = Step.ShakeScale;
		Confirm.Kind = Context.Kind;
		Confirm.Reaction = Reaction;
		Feedback->Client_ConfirmHit(Confirm);
	}
	return true;
}

void UMDCombatStatics::ReleaseAbilityInput(AActor* Avatar, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Avatar);
	if (!ASC || !AbilityClass)
	{
		return;
	}
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->IsA(AbilityClass) || !Spec.IsActive())
		{
			continue;
		}
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			if (Instance && Instance->IsActive())
			{
				Instance->InputReleased(Spec.Handle, ASC->AbilityActorInfo.Get(), Instance->GetCurrentActivationInfo());
			}
		}
	}
}

bool UMDCombatStatics::ShouldDrawTraces()
{
	return MDCombatDebug::ShouldDrawTraces();
}

UMDLocalSettings* UMDCombatStatics::GetCombatOptions()
{
	return GetMutableDefault<UMDLocalSettings>();
}

void UMDCombatStatics::SaveCombatOptions()
{
	GetMutableDefault<UMDLocalSettings>()->SaveConfig();
}
