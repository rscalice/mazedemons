#include "AI/MDEnemyController.h"

#include "MDGameplayStatics.h"

AMDEnemyController::AMDEnemyController()
{
	bWantsPlayerState = false;
	SetGenericTeamId(FGenericTeamId(MDTeams::Demons));
}

void AMDEnemyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// Server only by construction (AAIController::OnPossess runs where the controller exists). No brain yet.
}
