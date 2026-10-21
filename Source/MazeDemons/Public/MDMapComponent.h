#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MDQuestTypes.h"
#include "MDMapComponent.generated.h"

class AMDMapZone;
class AMDObjectiveMarker;
class UMDFogComponent;
class UMDQuestComponent;
class UTexture;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDMapZoneChanged, AMDMapZone*, Zone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDMapFogUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDMinimapOptionsChanged);

// Local minimap state on the player controller (not replicated). Follows the pawn into its map zone, turns the party's shared fog
// bits (UMDFogComponent) into a 1-pixel-per-cell texture (red = revealed) and holds the per-user minimap options.
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

	// Runtime capture of the current zone, else its baked texture (may be null).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	UTexture* GetMapTexture() const;

	// 1 pixel per fog cell, red channel 1 = revealed, 0 = hidden. Null until the player is in a zone.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	UTexture2D* GetFogTexture() const { return FogTexture; }

	// World position to 0..1 UV of the current zone's map (0,0 when there is no zone).
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

	// Local debug: re-renders the current zone's map capture.
	void ForceRecaptureCurrentZone();

	// Multi-line summary for DumpMap.
	FString BuildDump() const;

	// The pawn entered another zone (null = left every zone).
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

private:
	void BindSources();
	void RebuildGrid();
	void RebuildFogTexture();
	void RefreshMarkers();

	TWeakObjectPtr<AMDMapZone> CurrentZone;
	TWeakObjectPtr<UMDFogComponent> BoundFog;
	TWeakObjectPtr<UMDQuestComponent> BoundQuests;

	FMDFogGrid FogGrid;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMDObjectiveMarker>> CachedMarkers;

	bool bFogDirty = true;
	int32 RefreshCount = 0;
	FTimerHandle RefreshTimer;
};
