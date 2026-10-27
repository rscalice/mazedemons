#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDTargetableComponent.generated.h"

// Marker component: this actor may be soft-locked (V1 WP7) and gets an enemy health bar (V1 WP8). No behaviour of its own.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDTargetableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDTargetableComponent();

	// Lock-on and health bars skip the actor while false. The enemy character clears it on death (every machine).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bTargetable = true;

	// Skeletal-mesh socket (or bone) the health bar and reticle anchor to. Falls back to the actor origin when missing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	FName HealthBarSocket = TEXT("head");

	static UMDTargetableComponent* FindFor(const AActor* Actor);

	// True when Actor has the component and it is enabled.
	static bool IsActorTargetable(const AActor* Actor);
};
