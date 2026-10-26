#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDBlinkComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDBlinkSignature, FVector, Location);

// Enemy teleport hook (stub for V1 / V2; no enemy content in C1). The server moves the actor; everyone gets the cosmetics through
// a multicast: an after-image and a Blueprint VFX hook at the departure point and again at the arrival point. Local effects only,
// so the after-image respects the Options toggle.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDBlinkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDBlinkComponent();

	// Furthest allowed blink in cm; 0 = unlimited.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0"))
	float MaxRange = 0.0f;

	// Seconds between blinks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0"))
	float Cooldown = 2.0f;

	// Server only. Teleports the owner to Location (keeps its rotation) and plays the cosmetics on every machine. False when the
	// cooldown is running, the range is exceeded, the owner is missing or the move was blocked.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	bool TeleportTo(FVector Location);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool CanBlink() const;

	// Every machine: the owner is leaving Origin. Spawn a puff / sound here.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnBlinkDeparture(FVector Origin);

	// Every machine: the owner appeared at Destination.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnBlinkArrival(FVector Destination);

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDBlinkSignature OnDeparted;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDBlinkSignature OnArrived;

	static UMDBlinkComponent* FindFor(const AActor* Actor);

private:
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Departure(FVector Origin);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Arrival(FVector Destination);

	double LastBlinkTime = -1000.0;
};
