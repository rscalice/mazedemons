#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Navigation/CrowdAgentInterface.h"
#include "MDCrowdObstacleComponent.generated.h"

// Makes a player pawn an obstacle for the Detour crowd (the enemies' UCrowdFollowingComponent avoidance), the way the
// crowd manager documents it: a non-simulated ICrowdAgentInterface registered with UCrowdManager. Pawns do not collide
// with each other yet (WP7), and a ring slot or a flank point on the player's far side is a straight path through the
// player's capsule; with this obstacle the crowd steers round the player instead of running through them. Added by
// UMDAISubsystem to every living player pawn on the server (the crowd runs there); harmless on a machine with no crowd.
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDCrowdObstacleComponent : public UActorComponent, public ICrowdAgentInterface
{
	GENERATED_BODY()

public:
	UMDCrowdObstacleComponent();

	// Extra radius around the pawn's capsule the crowd keeps clear (cm).
	UPROPERTY(EditAnywhere, Category = "Crowd", meta = (ClampMin = "0"))
	float Margin = 25.0f;

	// ICrowdAgentInterface
	virtual FVector GetCrowdAgentLocation() const override;
	virtual FVector GetCrowdAgentVelocity() const override;
	virtual void GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const override;
	virtual float GetCrowdAgentMaxSpeed() const override;

	bool IsRegisteredWithCrowd() const { return bRegistered; }

	// Ensures Pawn carries one (server only). Returns it, or null when the world has no crowd manager.
	static UMDCrowdObstacleComponent* EnsureOn(APawn* Pawn);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void RegisterWithCrowd();
	void UnregisterFromCrowd();
	bool bRegistered = false;
};
