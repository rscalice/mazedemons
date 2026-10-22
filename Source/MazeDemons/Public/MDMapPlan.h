#pragma once

#include "CoreMinimal.h"
#include "MDMapPlan.generated.h"

struct FMDFogGrid;

// What one pixel of the level map shows (see FMDMapPlanRules::Classify).
enum class EMDMapCell : uint8
{
	Void,     // nothing rendered there (outside the level)
	Low,      // below the floor (water channels)
	Floor,    // walkable floor
	Obstacle, // above the floor but below StepHeight (crates, steps)
	Solid     // walls, wall tops, pillars
};

// Colours and thresholds of the level map plan (sRGB colours).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDMapPlanStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor FloorColor = FColor(186, 178, 158);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor LowColor = FColor(128, 124, 112);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor ObstacleColor = FColor(98, 94, 86);

	// Floor pixels next to anything that is not floor: outlines walls, doorways and tunnel mouths.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor EdgeColor = FColor(46, 44, 42);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor SolidColor = FColor(28, 28, 32);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FColor VoidColor = FColor(10, 10, 12);

	// Surfaces within this many units of the floor height count as floor.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (ClampMin = "1"))
	float FloorTolerance = 30.0f;

	// Surfaces higher than this above the floor are Solid; between FloorTolerance and this they are Obstacle.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (ClampMin = "1"))
	float StepHeight = 120.0f;

	// Width of the floor outline in pixels (0 = no outline).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (ClampMin = "0", ClampMax = "8"))
	int32 EdgeWidth = 2;
};

// Pure level-map rules (world-free, unit tested). A plain static class, not a Blueprint library.
// Images are square, row-major, Size x Size; pixel x follows world X and pixel y follows world Y (as WorldToMapUV).
struct MAZEDEMONS_API FMDMapPlanRules
{
	// Union of the valid boxes, grown by Padding on every side. Invalid (bIsValid false) when there is no valid box.
	static FBox2D FitBounds(const TArray<FBox2D>& Boxes, float Padding);

	// Square with the same centre and side = the longer axis (at least 1 unit). Invalid in, invalid out.
	static FBox2D MakeSquare(const FBox2D& Bounds);

	// Most common height (histogram mode, BucketSize wide buckets) among heights >= MinValidHeight; the bucket's mean is
	// returned. Falls back to MinValidHeight when nothing is valid.
	static float EstimateFloorHeight(const TArray<float>& Heights, float MinValidHeight, float BucketSize = 10.0f);

	// Heights below MinValidHeight or not finite are Void.
	static EMDMapCell Classify(float Height, float FloorHeight, float MinValidHeight, const FMDMapPlanStyle& Style);

	// Classifies every pixel, outlines the floor and writes Size*Size colours. Optional OutCounts gets one count per
	// EMDMapCell (index = enum value) plus the edge count at index 5. False (and empty Out) when Heights is not Size*Size.
	static bool BuildPlan(const TArray<float>& Heights, int32 Size, float FloorHeight, float MinValidHeight,
		const FMDMapPlanStyle& Style, TArray<FColor>& Out, TArray<int32>* OutCounts = nullptr);

	// 0..1 revealed amount at map UV (bilinear over cell centres, so the fog edge is soft). 0 for an invalid grid.
	static float SampleFog(const FMDFogGrid& Grid, double U, double V);

	// Plan with the fog applied (hidden pixels become FogColor). Grid bounds are assumed to equal the map bounds.
	// False (and Out untouched) when Plan is not Size*Size.
	static bool ComposeFog(const TArray<FColor>& Plan, int32 Size, const FMDFogGrid& Grid, FColor FogColor, TArray<FColor>& Out);
};
