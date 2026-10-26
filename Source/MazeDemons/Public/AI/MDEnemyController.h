#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MDEnemyController.generated.h"

// Server-only AI controller for AMDEnemyCharacter. A shell in V1 WP0: it possesses the enemy and nothing else. The brain,
// StateTree and perception arrive with WP1 / WP3. AI controllers do not replicate, so clients never see this actor.
UCLASS()
class MAZEDEMONS_API AMDEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	AMDEnemyController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
