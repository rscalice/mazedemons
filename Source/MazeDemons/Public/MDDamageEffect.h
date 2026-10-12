#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MDDamageEffect.generated.h"

// Instant GE adding SetByCaller "Data.Damage" to the IncomingDamage meta attribute.
UCLASS()
class MAZEDEMONS_API UMDDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMDDamageEffect();
};
