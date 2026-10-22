#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MDQuestTypes.h"
#include "MDMapComponent.generated.h"

class AMDLevelMap;
class AMDMapZone;
class AMDObjectiveMarker;
class UMDFogComponent;
class UMDQuestComponent;
class UTexture;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDMapZoneChanged, AMDMapZone*, Zone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDMapFogUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDLevelMapChanged, AMDLevelMap*, Map);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDMinimapOptionsChanged);

// Local minimap state on the player controller (not replicated). Owns the one minimap texture of the whole level: the level map's
// floor plan (AMDLevelMap) with the party's shared fog (UMDFogComponent) composed in on the CPU, updated in place so the ProHUD
// brush never changes. Also follows the pawn into its map zone (labels, tracker) and holds the per-user minimap options.
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDMapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDMapComponent();

	// The local controller's map component, or null.
	static UMDMapComponent* FindFor(const AActor* ControllerOrPawn);

	// Seconds between zone / fog refreshes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "0.05"))
	float RefreshInterval = 0.25f;

	// The map zone the local pawn is in (null outside every zone).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	AMDMapZone* GetCurrentZone() const { return CurrentZone.Get(); }

	// The level map this component draws (null until one exists in the world).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	AMDLevelMap* GetLevelMap() const { return LevelMap.Get(); }

	// The whole-level minimap: floor plan with the fog applied. The same object is updated in place on fog changes and recaptures.
	// Null until the level map has captured.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	UTexture2D* GetMapTexture() const { return MapTexture; }

	// 1 pixel per fog cell, red channel 1 = revealed, 0 = hidden. Null until the fog has a layout.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	UTexture2D* GetFogTexture() const { return FogTexture; }

	// World position to 0..1 UV of the level map (0,0 when there is none).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FVector2D WorldToMapUV(FVector World) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	float GetRevealedFraction() const { return FogGrid.GetRevealedFraction(); }

	// Active objective markers worth drawing (MapMarker != None, not consumed), refreshed on quest changes.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	TArray<AMDObjectiveMarker*> GetObjectiveMarkers() const;

	// ---- Minimap options (per user, saved in GameUserSettings.ini) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	bool IsMinimapNorthUp() const;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	void SetMinimapNorthUp(bool bNorthUp);

	// Default is rotate-with-camera; this flips to north-up and back.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	void ToggleMinimapRotation();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	float GetMinimapZoom() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	float GetMinimapRadiusUU() const;

	// Local debug: re-renders the level map.
	void ForceRecaptureLevelMap();

	// Multi-line summary for DumpMap.
	FString BuildDump() const;

	// The level map was found or recaptured: feed GetMapTexture to the minimap and place the corners again.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Map")
	FMDLevelMapChanged OnLevelMapChanged;

	// The pawn entered another zone (null = left every zone). Labels / tracker only: the map never changes with the zone.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Map")
	FMDMapZoneChanged OnZoneChanged;

	// The fog texture content changed.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Map")
	FMDMapFogUpdated OnFogUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Map")
	FMDMinimapOptionsChanged OnMinimapOptionsChanged;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void Refresh();

	UFUNCTION()
	void HandleFogChanged();

	UFUNCTION()
	void HandleQuestsChanged();

	UFUNCTION()
	void HandleLevelMapCaptured(AMDLevelMap* Map);

private:
	void BindSources();
	void RebuildGrid();
	void RebuildFogTexture();
	void RebuildMapTexture();
	void RefreshMarkers();

	TWeakObjectPtr<AMDMapZone> CurrentZone;
	TWeakObjectPtr<AMDLevelMap> LevelMap;
	TWeakObjectPtr<UMDFogComponent> BoundFog;
	TWeakObjectPtr<UMDQuestComponent> BoundQuests;

	FMDFogGrid FogGrid;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MapTexture;

	// Capture count of the plan in MapTexture (-1 = none yet).
	int32 MapTextureCapture = -1;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMDObjectiveMarker>> CachedMarkers;

	bool bFogDirty = true;
	int32 RefreshCount = 0;
	FTimerHandle RefreshTimer;
};
