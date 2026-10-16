#include "MDGameplayStatics.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDInventoryComponent.h"
#include "MDPlayerState.h"
#include "MDPlayerController.h"
#include "MDStaminaComponent.h"

UMDInventoryComponent* UMDGameplayStatics::GetInventory(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	// Accepts a pawn, controller or PlayerState. Checks the actor itself, then controller, then PlayerState.
	if (UMDInventoryComponent* Inv = Actor->FindComponentByClass<UMDInventoryComponent>())
	{
		return Inv;
	}
	const AController* Controller = Cast<AController>(Actor);
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		Controller = Pawn->GetController();
		PS = Pawn->GetPlayerState();
	}
	if (Controller)
	{
		if (UMDInventoryComponent* Inv = Controller->FindComponentByClass<UMDInventoryComponent>())
		{
			return Inv;
		}
		if (!PS)
		{
			PS = Controller->PlayerState;
		}
	}
	return PS ? PS->FindComponentByClass<UMDInventoryComponent>() : nullptr;
}

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

UMDStaminaComponent* UMDGameplayStatics::GetStaminaComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDStaminaComponent* Comp = Actor->FindComponentByClass<UMDStaminaComponent>())
	{
		return Comp;
	}
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		PS = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		PS = Controller->PlayerState;
	}
	return PS ? PS->FindComponentByClass<UMDStaminaComponent>() : nullptr;
}

void UMDGameplayStatics::GetStamina(const AActor* Actor, float& Stamina, float& MaxStamina)
{
	const UMDAbilitySystemComponent* ASC = GetMDAbilitySystemComponent(Actor);
	Stamina = ASC ? ASC->GetStamina() : 0.0f;
	MaxStamina = ASC ? ASC->GetMaxStamina() : 0.0f;
}

bool UMDGameplayStatics::CanSprint(const APawn* Pawn)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Pawn);
	return !Comp || Comp->IsSprintAllowedFor(Pawn && Pawn->IsLocallyControlled());
}

bool UMDGameplayStatics::HasStaminaForJump(const AActor* Actor)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Actor);
	return !Comp || Comp->CanJump();
}

bool UMDGameplayStatics::HasStaminaForVault(const AActor* Actor)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Actor);
	return !Comp || Comp->CanVault();
}

void UMDGameplayStatics::NotifyJumped(const AActor* Actor)
{
	if (UMDStaminaComponent* Comp = GetStaminaComponent(Actor))
	{
		Comp->NotifyJumped();
	}
}

void UMDGameplayStatics::NotifyTraversalStarted(const AActor* Actor)
{
	if (UMDStaminaComponent* Comp = GetStaminaComponent(Actor))
	{
		Comp->NotifyTraversalStarted();
	}
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

void UMDGameplayStatics::RequestPickup(AActor* Pickup, const APawn* Interactor)
{
	if (!Pickup || !Interactor)
	{
		return;
	}
	if (AMDPlayerController* Controller = Cast<AMDPlayerController>(Interactor->GetController()))
	{
		Controller->RequestPickup(Pickup);
	}
}