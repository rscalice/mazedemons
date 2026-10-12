#include "MDGameplayStatics.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDPlayerState.h"

UMDAbilitySystemComponent* UMDGameplayStatics::GetMDAbilitySystemComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	if (!ASC)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (const IAbilitySystemInterface* PSInterface = Cast<IAbilitySystemInterface>(Pawn->GetPlayerState()))
			{
				ASC = PSInterface->GetAbilitySystemComponent();
			}
		}
	}
	if (!ASC)
	{
		return Actor->FindComponentByClass<UMDAbilitySystemComponent>();
	}
	return Cast<UMDAbilitySystemComponent>(ASC);
}

uint8 UMDGameplayStatics::GetTeamId(const AActor* Actor)
{
	if (!Actor)
	{
		return MDTeams::Demons;
	}

	const APawn* Pawn = Cast<APawn>(Actor);
	const AController* Controller = Cast<AController>(Actor);
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (Pawn)
	{
		PS = Pawn->GetPlayerState();
		Controller = Pawn->GetController();
	}
	else if (Controller)
	{
		PS = Controller->PlayerState;
	}

	if (Cast<AMDPlayerState>(PS) || (Controller && Controller->IsPlayerController()))
	{
		return MDTeams::Players;
	}

	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Actor))
	{
		const FGenericTeamId Id = TeamAgent->GetGenericTeamId();
		if (Id != FGenericTeamId::NoTeam)
		{
			return Id.GetId();
		}
	}
	if (const IGenericTeamAgentInterface* ControllerAgent = Cast<IGenericTeamAgentInterface>(Controller))
	{
		const FGenericTeamId Id = ControllerAgent->GetGenericTeamId();
		if (Id != FGenericTeamId::NoTeam)
		{
			return Id.GetId();
		}
	}
	return MDTeams::Demons;
}

bool UMDGameplayStatics::CanDamage(const AActor* DamageInstigator, const AActor* Target)
{
	if (!DamageInstigator || DamageInstigator == Target)
	{
		return true;
	}

	const uint8 InstigatorTeam = GetTeamId(DamageInstigator);
	const uint8 TargetTeam = GetTeamId(Target);
	if (InstigatorTeam != TargetTeam)
	{
		return true;
	}
	if (InstigatorTeam == MDTeams::Players)
	{
		return AMDGameState::IsFriendlyFireEnabled(DamageInstigator);
	}
	return false;
}
