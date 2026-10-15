#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MDPlayerController.generated.h"

UCLASS()
class MAZEDEMONS_API AMDPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Debug console command: kills the local player via the server. Body is compiled out in shipping builds.
	UFUNCTION(Exec)
	void KillPlayer();

	// Debug (non-shipping): server applies lethal self-damage. Body is compiled out in shipping builds.
	UFUNCTION(Server, Reliable)
	void Server_KillPlayer();

	// Debug (non-shipping): asks the server to add Qty of DT_Items row RowName to this player's inventory. Bodies compiled out in shipping.
	UFUNCTION(Exec)
	void GiveItem(FName RowName, int32 Qty = 1);

	UFUNCTION(Server, Reliable)
	void Server_GiveItem(FName RowName, int32 Qty);

	// Debug (non-shipping): logs THIS machine's view of the local player's inventory (LogMDInventory, Warning).
	UFUNCTION(Exec)
	void DumpInventory();

	// Debug (non-shipping): same, for every pawn with an inventory component in the local world.
	UFUNCTION(Exec)
	void DumpAllInventories();

	// Debug (non-shipping): lists DT_Items row names with their equip slot, for use with GiveItem.
	UFUNCTION(Exec)
	void ListItemRows();
};
