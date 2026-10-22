#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDMapPlan.h"
#include "MDLevelMap.generated.h"

class UBoxComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDLevelMapCaptured, AMDLevelMap*, Map);

// The one minimap of the whole level (u4-design §9). Place one per level, or spawn it at runtime after generation (PDG) and
// call Recapture. Never replicated: every machine renders its own copy once. MapBox is read in XY (yaw 0) for the area and in Z
// for the capture range: the orthographic camera looks down from the top of the box, anything below the bottom is "nothing".
// The capture is scene depth, turned on the CPU into a floor / wall plan (FMDMapPlanRules); pawns and gameplay actors are hidden.
UCLASS()
class MAZEDEMONS_API AMDLevelMap : public AActor
{
	GENERATED_BODY()

public:
	AMDLevelMap();

	// The level map of this world (the first one found), or null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map", meta = (WorldContext = "WorldContextObject"))
	static AMDLevelMap* FindLevelMap(const UObject* WorldContextObject);

	// Fit MapBox (XY) to the union of every AMDMapZone plus AutoFitPadding before each capture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|Map")
	bool bAutoFitToZones = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|Map", meta = (ClampMin = "0"))
	float AutoFitPadding = 400.0f;

	// Capture the frame after BeginPlay (skipped on dedicated servers).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	bool bCaptureOnBeginPlay = true;

	// Square image size in pixels.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 CaptureResolution = 2048;

	// Unreal units per fog cell of the level fog. 0 or less uses UMDUISettings::FogCellSize. Raised so the grid fits 128x128.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	float FogCellSize = 200.0f;

	// ProHUD minimap canvas pixels per world unit: keeps the minimap's world scale independent of the level size.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "0.01"))
	float MinimapCanvasPixelsPerUU = 0.4f;

	// Actors with this tag are left out of the capture (in addition to pawns, objective markers, gates and replicated actors).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	FName HiddenActorTag = TEXT("MinimapHidden");

	// Leave every replicated actor out (gameplay actors replicate, level geometry does not).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	bool bHideReplicatedActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	FMDMapPlanStyle Style;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> MapBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	// Fires after every successful capture (also the first one).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Map")
	FMDLevelMapCaptured OnMapCaptured;

	// Captures once; later calls do nothing (use Recapture).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	void CaptureMap();

	// Refits (if bAutoFitToZones) and renders again, e.g. after runtime generation.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	void Recapture();

	// Sets MapBox XY to the union of every AMDMapZone plus AutoFitPadding. False when there is no zone.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	bool FitToZones();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	bool HasPlan() const { return PlanSize > 0; }

	// The floor plan without fog (Size x Size, sRGB). Empty before the first capture.
	const TArray<FColor>& GetPlanPixels() const { return PlanPixels; }
	int32 GetPlanSize() const { return PlanSize; }

	// Increments with every capture, so consumers can tell a new plan from the old one.
	int32 GetCaptureCount() const { return CaptureCount; }

	// The XY rectangle of MapBox.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FBox2D GetLevelBounds() const;

	// Square world rectangle the map image covers: centred on MapBox, side = the longer axis.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FBox2D GetMapBounds() const;

	// ProHUD calibration corner positions (bottom-left = map min, top-right = map max, at the box centre height).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FVector GetMinimapCornerBL() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FVector GetMinimapCornerTR() const;

	// ProHUD minimap canvas size in pixels for this map (map side x MinimapCanvasPixelsPerUU).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	float GetMinimapCanvasSize() const;

	float GetFogCellSize() const;

	// World position to 0..1 map UV (U follows X, V follows Y).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FVector2D WorldToMapUV(FVector World) const;

	// One line for DumpMap.
	FString BuildDump() const;

protected:
	virtual void BeginPlay() override;

private:
	void DoCapture();
	void CollectHidden(TArray<AActor*>& OutActors) const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> DepthTarget;

	TArray<FColor> PlanPixels;
	TArray<int32> PlanCounts;
	int32 PlanSize = 0;
	int32 CaptureCount = 0;
	int32 HiddenActorCount = 0;
	float FloorHeight = 0.0f;
	bool bCaptured = false;
};
