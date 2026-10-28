// R3: a pouch of ammo lying in the world. The first player pawn that touches it takes it (auto on overlap, never split between
// players: gold is shared, ammo is not), the bag gets the stack and the owner sees the pickup toast. A full bag leaves it lying.
// Visual: a small flat box from engine content, tinted amber (Light) or red-brown (Heavy); the shard placeholder is left alone.
#pragma once

#include "CoreMinimal.h"
#include "MDPickupBase.h"
#include "MDAmmoPickup.generated.h"

class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class MAZEDEMONS_API AMDAmmoPickup : public AMDPickupBase
{
	GENERATED_BODY()

public:
	AMDAmmoPickup();

	virtual void BeginPlay() override;

	// Server: a pawn touched the pouch. Takes it for that player when the bag has room; false for anything else (a second toucher
	// finds it already gone). Overlap events call this; tests call it directly.
	bool Touch(AActor* Toucher);

	// Seconds a pouch stays in the world before it fades (0 = forever).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|Pickup", meta = (ClampMin = "0"))
	float LifetimeSeconds = 120.0f;

protected:
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

private:
	UPROPERTY(VisibleAnywhere, Category = "Maze Demons|Pickup")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "Maze Demons|Pickup")
	TObjectPtr<UStaticMeshComponent> Pouch;
};
