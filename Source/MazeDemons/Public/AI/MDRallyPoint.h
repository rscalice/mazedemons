#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDRallyPoint.generated.h"

// Where a retreating pack runs to (v1-design 2.4). Any actor tagged "MDRallyPoint" works as a rally point; this class is
// the placeable one (it tags itself). Level markers from PDG replace it later (V1 WP3 MDAIQueries).
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDRallyPoint : public AActor
{
	GENERATED_BODY()

public:
	AMDRallyPoint();

	static const FName RallyTag;
};
