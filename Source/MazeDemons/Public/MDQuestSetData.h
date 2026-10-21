#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MDQuestTypes.h"
#include "MDQuestSetData.generated.h"

// Text and objective of one zone's quest.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDQuestDef
{
	GENERATED_BODY()

	// "Zone 2 - The First Fork"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FMDObjectiveDef Objective;

	// Toast shown to every player when the zone completes. Empty uses a default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FText CompletionToast;
};

// DA_MDQuestSet_Slice: one FMDQuestDef per zone (4 entries, index 0 = Zone 1). Set on the GameState's UMDQuestComponent.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDQuestSetData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	TArray<FMDQuestDef> Zones;
};
