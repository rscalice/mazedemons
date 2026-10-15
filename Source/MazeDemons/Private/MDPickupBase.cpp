#include "MDPickupBase.h"

#include "MDInventoryComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AMDPickupBase::AMDPickupBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void AMDPickupBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMDPickupBase, ItemRowName);
	DOREPLIFETIME(AMDPickupBase, Quantity);
	DOREPLIFETIME(AMDPickupBase, bPickedUp);
}

bool AMDPickupBase::TryPickup(AActor* Interactor)
{
	if (!HasAuthority() || bPickedUp || !Interactor || ItemRowName.IsNone() || Quantity <= 0)
	{
		return false;
	}

	UMDInventoryComponent* Inventory = Interactor->FindComponentByClass<UMDInventoryComponent>();
	if (!Inventory)
	{
		// The inventory may live on the controller or PlayerState rather than the pawn.
		if (const APawn* Pawn = Cast<APawn>(Interactor))
		{
			if (const AController* Controller = Pawn->GetController())
			{
				Inventory = Controller->FindComponentByClass<UMDInventoryComponent>();
			}
			if (!Inventory && Pawn->GetPlayerState())
			{
				Inventory = Pawn->GetPlayerState()->FindComponentByClass<UMDInventoryComponent>();
			}
		}
	}
	if (!Inventory || !Inventory->AddItem(ItemRowName, Quantity))
	{
		return false;
	}

	bPickedUp = true;
	Destroy();
	return true;
}
