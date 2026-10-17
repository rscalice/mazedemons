#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDPickupBase.generated.h"

// Replicated world pickup. Blueprint children add the mesh/visuals and call TryPickup from the interaction path.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDPickupBase : public AActor
{
	GENERATED_BODY()

public:
	AMDPickupBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Maze Demons|Pickup", meta = (ExposeOnSpawn = "true"))
	FName ItemRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Maze Demons|Pickup", meta = (ExposeOnSpawn = "true", ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Pickup")
	bool bPickedUp = false;

	// True while a player wields this actor: it must not be interactable / pick-up-able by anyone. Replicated to all.
	UPROPERTY(ReplicatedUsing = OnRep_Equipped, BlueprintReadOnly, Category = "Maze Demons|Pickup")
	bool bEquipped = false;

	// Authority only: call on equip (true) and on drop / unequip (false).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Pickup")
	void SetEquipped(bool bNewEquipped);

	// Interaction gate for the BP trace / prompt: false while equipped or already picked up.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Pickup")
	bool CanInteract() const { return !bEquipped && !bPickedUp; }

	// Fired on every machine when bEquipped changes (BP may adjust collision / visuals).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Pickup")
	void OnEquippedChanged(bool bNowEquipped);

	// Server only. Looks for a UMDInventoryComponent on Interactor (or its controller / PlayerState / pawn),
	// adds the item, and destroys the pickup on success.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Pickup")
	bool TryPickup(AActor* Interactor);

private:
	UFUNCTION()
	void OnRep_Equipped();
};
