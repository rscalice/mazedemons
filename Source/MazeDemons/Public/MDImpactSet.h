#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "MDImpactSet.generated.h"

class UFXSystemAsset;
class UMaterialInterface;
class USoundBase;

// Impact effects for one surface type.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDImpactEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TEnumAsByte<EPhysicalSurface> Surface = SurfaceType_Default;

	// Niagara system (or Cascade particle system) to spawn at the impact point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UFXSystemAsset> VFX = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<USoundBase> Sound = nullptr;

	// Decal for static surfaces only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UMaterialInterface> Decal = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	FVector DecalSize = FVector(8.0f, 24.0f, 24.0f);

	// Multiplies the VFX scale (Reduced effects level scales it further).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0"))
	float VFXScale = 1.0f;

	// Optional floor effect for hits on pawns (blood pooling / spattering on the ground under the victim). Niagara only (e.g. a
	// system that spawns decals). Skipped on Reduced effects; capped by the set's MaxDecalsPerZone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UFXSystemAsset> FloorVFX = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0"))
	float FloorVFXScale = 1.0f;
};

// Per weapon class impact effects (DA_Impact_Blade / Blunt / Heavy). Data only; the feedback component's Blueprint events use it.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDImpactSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TArray<FMDImpactEntry> Entries;

	// Used when no entry matches the surface.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	FMDImpactEntry Default;

	// Decal cap per zone.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0"))
	int32 MaxDecalsPerZone = 20;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	FMDImpactEntry FindEntry(EPhysicalSurface Surface) const;
};
