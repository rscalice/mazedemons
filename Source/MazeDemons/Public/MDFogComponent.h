#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MDQuestTypes.h"
#include "MDFogComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDFogChanged);

// Shared party fog of war, on AMDGameState: ONE grid over the level map (AMDLevelMap, u4 §9), tunnels included. The server
// reveals around every living player pawn every UpdateInterval and replicates the layout plus one bit per cell, so late joiners
// get the party's fog. Reveal only: bits are cleared only by ResetFog (debug) or when the level map's layout changes.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDFogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDFogComponent();

	// The fog component of the world's AMDGameState, or null.
	static UMDFogComponent* FindFor(const UObject* WorldContextObject);

	// Unreal units around each living pawn that are revealed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Fog", meta = (ClampMin = "0"))
	float RevealRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Fog", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.25f;

	UPROPERTY(ReplicatedUsing = OnRep_Fog)
	FMDFogLevelBits LevelFog;

	// Fires on the server after a change and on clients after replication.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Fog")
	FMDFogChanged OnFogChanged;

	// Builds OutGrid from the replicated layout and bits. False when there is no layout yet (no level map on the server).
	bool BuildGrid(FMDFogGrid& OutGrid) const;

	// Server debug: reveal everything / hide everything.
	void RevealAll();
	void ResetFog();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void OnRep_Fog();

private:
	bool IsServer() const;
	void UpdateFog();
	// Server: (re)initialises Grid from the level map when its layout changed. False when there is no level map.
	bool EnsureGrid();
	void Publish();

	// Server-side working grid (not replicated; LevelFog is its replicated copy).
	FMDFogGrid Grid;
	FTimerHandle UpdateTimer;
};
