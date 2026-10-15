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

	// Server only. Looks for a UMDInventoryComponent on Interactor (or its controller / PlayerState / pawn),
	// adds the item, and destroys the pickup on success.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Pickup")
	bool TryPickup(AActor* Interactor);
};
