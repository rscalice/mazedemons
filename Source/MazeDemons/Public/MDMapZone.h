#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDMapZone.generated.h"

class UBoxComponent;

// One zone of the maze for quests, kill credit, zone change and the zone label. Placed in the level or spawned at runtime (PDG);
// never replicated. The box is read in XY only and is assumed to be yaw 0. The minimap image belongs to AMDLevelMap (u4 §9).
UCLASS()
class MAZEDEMONS_API AMDMapZone : public AActor
{
	GENERATED_BODY()

public:
	AMDMapZone();

	// 1..4, matches the quest zone number.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map", meta = (ClampMin = "1", ClampMax = "4"))
	int32 ZoneIndex = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ZoneBox;

	// The XY rectangle of the zone box.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	FBox2D GetZoneBounds() const;

	// True when Location is inside the box in XY.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	bool ContainsXY(FVector Location) const;

	// The zone containing Location (the smallest one when boxes overlap), or null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map", meta = (WorldContext = "WorldContextObject"))
	static AMDMapZone* FindZoneAt(const UObject* WorldContextObject, FVector Location);

	// The zone with this ZoneIndex, or null.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map", meta = (WorldContext = "WorldContextObject"))
	static AMDMapZone* FindZoneByIndex(const UObject* WorldContextObject, int32 Index);
};
