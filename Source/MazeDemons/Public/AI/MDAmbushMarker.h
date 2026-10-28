#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDAmbushMarker.generated.h"

class AMDEnemyCharacter;
class UBoxComponent;

// V1 WP5 ambush (v1-design 1.2 Ambush): a Spawn.Ambush marker with a trigger volume and the level-placed enemies it hides.
//   - On BeginPlay (server) the Ambushers are hidden and dormant (no collision, not targetable, not net-relevant) and their
//     pack is armed (EMDPackState::AmbushArmed: no retreat, no pressing).
//   - A player crossing the trigger starts the sequence: the 12_Ambush_Growl_Cue plays at the marker (this actor is always
//     relevant, the hidden ambushers are not), 0.75 s later the group appears, and each ambusher may not attack for 0.5 s.
//   - T0 / T1 never ambush (unless bIgnoreTier, the demo switch): the group appears at once with no cue and no grace.
//   - Never at a checkpoint or a station: a marker within 15 m of one disarms itself and the group is an ordinary pack.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDAmbushMarker : public AActor
{
	GENERATED_BODY()

public:
	AMDAmbushMarker();

	// The enemies this marker hides (level-placed, usually sharing one SpawnGroupId).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambush")
	TArray<TObjectPtr<AMDEnemyCharacter>> Ambushers;

	// Seconds between the audio cue and the group appearing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambush", meta = (ClampMin = "0"))
	float CueLead = 0.75f;

	// Seconds the ambushers may not attack after appearing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambush", meta = (ClampMin = "0"))
	float ArrivalGrace = 0.5f;

	// Demo / testing: ambush at every tier (the tier gate is T2+ otherwise).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambush")
	bool bIgnoreTier = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ambush")
	TObjectPtr<UBoxComponent> Trigger;

	bool IsArmed() const { return bArmed; }
	bool HasTriggered() const { return bTriggered; }

	// Server. Hides the group and arms the pack (BeginPlay does this after the enemies have joined their pack).
	void Arm();

	// Server. A player entered the trigger (also the test entry point). False when nothing happened.
	bool TriggerAmbush(AActor* Player);

	// Locations of every checkpoint and station in the world.
	static void GatherSafeSpots(const UWorld* World, TArray<FVector>& OutSpots);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAmbushCue();

	void Reveal();
	void RevealNow(bool bWithGrace);
	bool IsPlayerTooClose() const;

	bool bArmed = false;
	bool bTriggered = false;
	TWeakObjectPtr<AActor> TriggeringPlayer;
	FTimerHandle ArmTimer;
	FTimerHandle RevealTimer;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CueSound;
};
