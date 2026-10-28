#pragma once

#include "CoreMinimal.h"
#include "MDStatusTypes.generated.h"

// V1 WP6: the status effects a creature can carry. Values are stable (saved in attack steps). Burn is reserved for a later Cultist variant.
UENUM(BlueprintType)
enum class EMDStatusType : uint8
{
	None = 0,
	Bleed,
	Frost,
	Poison,
	Burn,
	Count UMETA(Hidden)
};

// One active status as every machine sees it (replicated by UMDStatusEffectComponent). The server only rewrites an entry when it is applied,
// stacked, refreshed or ends, so it is cheap to replicate; clients derive the countdown from EndServerTime.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDStatusRep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	EMDStatusType Type = EMDStatusType::None;

	// 1..MaxStacks (Bleed stacks to 3, the others stay at 1).
	UPROPERTY(BlueprintReadOnly, Category = "Status")
	uint8 Stacks = 0;

	// Full length of the effect in seconds (for a radial timer).
	UPROPERTY(BlueprintReadOnly, Category = "Status")
	float Duration = 0.0f;

	// Server clock (AGameStateBase::GetServerWorldTimeSeconds) at which the effect ends unless refreshed.
	UPROPERTY(BlueprintReadOnly, Category = "Status")
	double EndServerTime = 0.0;

	bool IsSameAs(const FMDStatusRep& Other) const
	{
		return Type == Other.Type && Stacks == Other.Stacks && FMath::IsNearlyEqual(Duration, Other.Duration, 0.001f)
			&& FMath::IsNearlyEqual(EndServerTime, Other.EndServerTime, 0.001);
	}
};
