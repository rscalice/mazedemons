#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MDQuestTypes.h"
#include "MDObjectiveMarker.generated.h"

class UPrimitiveComponent;
class USphereComponent;

// A placed objective source. Reach and Collect fire when a living player pawn overlaps the trigger (server); Interact fires from
// Interact(APawn*), the same entry as AMDStation (the Blueprint interact path calls it) and carries the "Interactable" tag so the
// focus trace finds it. Reports once: after the quest accepted the event the marker is consumed (replicated), disabled and hidden
// from the minimap. Events the active quest does not want (wrong zone, wrong tag) leave the marker usable.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDObjectiveMarker : public AActor
{
	GENERATED_BODY()

public:
	AMDObjectiveMarker();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	EMDObjectiveType ObjectiveType = EMDObjectiveType::Reach;

	// Matched against the active quest's TargetTag (Objective.Zone1.Shrine, ...).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FGameplayTagContainer ObjectiveTags;

	// Progress reported when this marker fires.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "1"))
	int32 Amount = 1;

	// Minimap icon kind; None hides the marker from the minimap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	EMDMapMarkerType MapMarker = EMDMapMarkerType::Objective;

	// Interact objectives: furthest pawn distance for Interact() to count.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "0"))
	float MaxInteractDistance = 400.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Consumed, BlueprintReadOnly, Category = "Objective")
	bool bConsumed = false;

	// Authority only. Collect / Reach markers call this from the overlap; Blueprint pickups may call it too.
	// True when the active quest accepted the event and the marker was consumed.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Objective")
	bool TryActivate(APawn* User);

	// Authority only: the Interact entry (same signature as AMDStation::Interact).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Objective")
	void Interact(APawn* User);

	// Authority only: makes the marker usable again (ResetQuests).
	void ResetConsumed();

	// Every machine, after bConsumed changed: hide visuals / play a sound.
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective")
	void OnConsumedChanged(bool bNowConsumed);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Trigger;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Consumed();

private:
	void ApplyConsumedState();
};
