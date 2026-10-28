#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "MDEnvQueryContextPlayers.generated.h"

// EQS context: every living player pawn (the ambush query keeps points far from them). Used by UMDAIAmbushQuery.
UCLASS()
class MAZEDEMONS_API UMDEnvQueryContext_Players : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
