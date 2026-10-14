#include "MDPlayerController.h"

#include "MDAbilitySystemComponent.h"
#include "MDPlayerState.h"

void AMDPlayerController::KillPlayer()
{
#if !UE_BUILD_SHIPPING
	Server_KillPlayer();
#endif
}

void AMDPlayerController::Server_KillPlayer_Implementation()
{
#if !UE_BUILD_SHIPPING
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (ASC && !ASC->IsDead())
	{
		// Instigator == avatar pawn, so the team/friendly-fire gate in CanDamage lets self-damage through.
		ASC->ApplyDamage(1.0e9f, GetPawn(), GetPawn());
	}
#endif
}
