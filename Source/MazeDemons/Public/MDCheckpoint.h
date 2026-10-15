#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDCheckpoint.generated.h"

class UBoxComponent;
class UArrowComponent;
class UPrimitiveComponent;

// Zone checkpoint. When a living player pawn enters the box on the server, this actor's transform becomes the
// team's respawn point (AMDGameState::SetCheckpoint). The arrow shows the facing used when respawning here.
UCLASS()
class MAZEDEMONS_API AMDCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AMDCheckpoint();

	// Higher order = further into the level. A lower-order checkpoint never replaces a higher one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	int32 Order = 0;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	TObjectPtr<UArrowComponent> Facing;

	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
