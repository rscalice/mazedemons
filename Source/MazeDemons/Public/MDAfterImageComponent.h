#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDAfterImageComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

// One pooled ghost.
USTRUCT()
struct FMDAfterImageGhost
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Mesh = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	double SpawnTime = 0.0;
	bool bActive = false;
};

// Ghost trail for slides, dodges, fast attacks and blinks. Local cosmetic only: never runs on a dedicated server and obeys
// UMDLocalSettings::bAfterImagesEnabled. Each ghost is a pooled UPoseableMeshComponent holding a snapshot of the owner's pose, drawn
// with GhostMaterial (translucent; needs a scalar parameter named OpacityParameter) and faded over Lifetime. The pool is capped and
// requests are rate limited, so a held button cannot flood the scene.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDAfterImageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDAfterImageComponent();

	// Translucent ghost material (a parent with scalar "Opacity" and optional vector "Tint"). Null = ghosts are not spawned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image")
	FName OpacityParameter = TEXT("Opacity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image")
	FName TintParameter = TEXT("Tint");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image")
	FLinearColor Tint = FLinearColor(0.4f, 0.7f, 1.0f, 1.0f);

	// Opacity right after the snapshot; fades linearly to 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image", meta = (ClampMin = "0", ClampMax = "1"))
	float StartOpacity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image", meta = (ClampMin = "0.05"))
	float Lifetime = 0.35f;

	// Most ghosts alive at once; beyond that the oldest is recycled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxGhosts = 8;

	// Minimum time between two ghosts (rate limit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "After Image", meta = (ClampMin = "0.01"))
	float MinInterval = 0.05f;

	// Spawns one ghost now (rate limited). False when disabled, rate limited, or there is no mesh / material.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	bool RequestAfterImage();

	// Same, but the ghost is placed at Location (Blink departure / arrival) instead of where the mesh is now.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	bool RequestAfterImageAt(FVector Location);

	// Leaves a ghost every Interval seconds (<= 0 uses MinInterval) for Duration seconds.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void StartTrail(float Duration, float Interval = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void StopTrail();

	// Hides every ghost.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void ClearAll();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	int32 GetActiveGhostCount() const;

	// Local options: after-images on, not a dedicated server.
	bool IsEnabledHere() const;

	static UMDAfterImageComponent* FindFor(const AActor* Actor);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool SpawnGhost(const FVector* LocationOverride);
	USkeletalMeshComponent* GetSourceMesh() const;
	int32 AcquireGhostIndex();
	double Now() const;

	UPROPERTY(Transient)
	TArray<FMDAfterImageGhost> Pool;

	double LastSpawnTime = -1000.0;
	double TrailEndTime = 0.0;
	float TrailInterval = 0.05f;
	double NextTrailTime = 0.0;
};
