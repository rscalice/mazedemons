#include "MDPickupBase.h"

#include "MDGameplayStatics.h"
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

	// The inventory may live on the pawn, controller or PlayerState; GetInventory checks all of them.
	UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(Interactor);
	if (!Inventory || !Inventory->AddItem(ItemRowName, Quantity))
	{
		return false;
	}

	bPickedUp = true;
	Destroy();
	return true;
}
