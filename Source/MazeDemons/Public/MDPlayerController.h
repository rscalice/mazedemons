#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MDPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDPickupNotified, FName, ItemRowName, int32, Quantity);

UCLASS()
class MAZEDEMONS_API AMDPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server world time (AGameStateBase::GetServerWorldTimeSeconds) at which this player respawns; 0 while alive.
	// Replicated to the owner only; the HUD derives the death countdown from it.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	double RespawnAtServerTime = 0.0;

	// Owning client: the server added Quantity of ItemRowName to this player's inventory (HUD toast).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDPickupNotified OnPickupNotified;

	// Authority only: tells the owning client a pickup succeeded.
	void NotifyPickup(FName ItemRowName, int32 Quantity);

	// Seconds between death and respawn.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Respawn", meta = (ClampMin = "0"))
	float RespawnDelay = 5.0f;

	// Radius around a living party member within which a respawning player is placed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Respawn", meta = (ClampMin = "50"))
	float PartySpawnRadius = 250.0f;

	// Authority only. Respawns a dead player: near a living party member if there is one, else at the last zone
	// checkpoint (AMDGameState), else at a level PlayerStart. The old pawn (corpse) is destroyed.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Respawn")
	void Respawn();

	virtual void OnPossess(APawn* InPawn) override;

	// Max distance between the pawn and a pickup for a client pickup request to be honoured.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory", meta = (ClampMin = "0"))
	float MaxPickupDistance = 400.0f;

	// Local call (any machine): asks the server to pick Pickup up for this player. Interact input runs locally, but
	// pickups are server-authoritative, so clients must go through this RPC.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestPickup(AActor* Pickup);

	UFUNCTION(Server, Reliable)
	void Server_RequestPickup(AActor* Pickup);

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

protected:
	// Server: bound to the ASC's OnDeath; starts the respawn timer.
	UFUNCTION()
	void HandleOwnerDeath();

	// Owning client: undo anything the death left behind (disabled input, ignore flags).
	UFUNCTION(Client, Reliable)
	void Client_OnRespawned();

	UFUNCTION(Client, Reliable)
	void Client_NotifyPickup(FName ItemRowName, int32 Quantity);

private:
	bool FindRespawnTransform(FTransform& OutTransform) const;

	FTimerHandle RespawnTimer;
};
