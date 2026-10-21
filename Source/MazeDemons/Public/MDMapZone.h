#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "MDMapZone.generated.h"

class UBoxComponent;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

// One zone of the maze for the minimap, kill credit and fog. Placed in the level or spawned at runtime (PDG); never replicated,
// every machine captures its own map texture once. The box is read in XY only and is assumed to be yaw 0.
UCLASS()
class MAZEDEMONS_API AMDMapZone : public AActor
{
	GENERATED_BODY()

public:
	AMDMapZone();

	// 1..4, matches the quest zone number.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "1", ClampMax = "4"))
	int32 ZoneIndex = 1;

	// Unreal units per fog cell. 0 or less uses UMDUISettings::FogCellSize. Raised automatically so a zone has at most 64x64 cells.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	float FogCellSize = 400.0f;

	// Pre-rendered fallback used when no runtime capture exists.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	TObjectPtr<UTexture2D> BakedMap;

	// Optional painterly overlay: the minimap draws the map capture through this material when set (empty in U4).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	TObjectPtr<UMaterialInterface> OverlayMaterial;

	// Capture the map at BeginPlay (skipped on dedicated servers).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	bool bCaptureOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "64", ClampMax = "4096"))
	int32 CaptureResolution = 1024;

	// How far above the top of the zone box the orthographic camera sits.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "0"))
	float CaptureHeightAboveBox = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	TEnumAsByte<ESceneCaptureSource> CaptureSource = SCS_FinalColorLDR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ZoneBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	// The local capture (transient, never replicated). Null until CaptureMap ran.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Maze Demons|Map")
	TObjectPtr<UTextureRenderTarget2D> RuntimeMap;

	// Renders the zone once from above into RuntimeMap. Later calls do nothing (use ForceRecapture on the debug path).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	void CaptureMap();

	// Debug: renders again even if a capture exists.
	void ForceRecapture();

	// The runtime capture when there is one, else BakedMap (may be null).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	UTexture* GetMapTexture() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	bool HasRuntimeMap() const { return RuntimeMap != nullptr; }

	// Square world rectangle (X, Y) the map texture covers: centred on the box, side = the longer box axis.
	FBox2D GetMapBounds() const;

	float GetFogCellSize() const;

	// World position to 0..1 map UV (U follows X, V follows Y).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FVector2D WorldToMapUV(FVector World) const;

	// True when Location is inside the box in XY.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	bool ContainsXY(FVector Location) const;

	// The zone containing Location (the smallest one when boxes overlap), or null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map", meta = (WorldContext = "WorldContextObject"))
	static AMDMapZone* FindZoneAt(const UObject* WorldContextObject, FVector Location);

	// The zone with this ZoneIndex, or null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map", meta = (WorldContext = "WorldContextObject"))
	static AMDMapZone* FindZoneByIndex(const UObject* WorldContextObject, int32 Index);

protected:
	virtual void BeginPlay() override;

private:
	void DoCapture();
	bool bCaptured = false;
};
