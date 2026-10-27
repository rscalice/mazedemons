#include "AI/MDEnemyController.h"

#include "AI/MDAIRules.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "MDGameplayStatics.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Sight.h"
#include "StateTree.h"

AMDEnemyController::AMDEnemyController()
{
	bWantsPlayerState = false;
	SetGenericTeamId(FGenericTeamId(MDTeams::Demons));

	Brain = CreateDefaultSubobject<UMDEnemyBrainComponent>(TEXT("Brain"));

	StateTree = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTree"));
	StateTree->SetStartLogicAutomatically(false); // started once the pawn is possessed and the asset is loaded

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1500.0f;
	SightConfig->LoseSightRadius = 1800.0f;
	SightConfig->PeripheralVisionAngleDegrees = 55.0f;
	SightConfig->SetMaxAge(static_cast<float>(MDAI::LoseTargetTime));
	// Players are not on a team the generic solver knows as hostile on every setup; the brain filters targets itself.
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	Perception->ConfigureSense(*SightConfig);

	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
	Perception->ConfigureSense(*DamageConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());

	EnemyStateTree = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/AI/ST_Enemy.ST_Enemy")));
}

AMDEnemyCharacter* AMDEnemyController::GetEnemy() const
{
	return Cast<AMDEnemyCharacter>(GetPawn());
}

void AMDEnemyController::BeginPlay()
{
	Super::BeginPlay();
	if (Perception)
	{
		Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AMDEnemyController::HandleTargetPerceptionUpdated);
	}
	TryStartExecutor();
}

void AMDEnemyController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Brain)
	{
		Brain->OnIntentSelected.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AMDEnemyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (Brain)
	{
		Brain->OnIntentSelected.RemoveAll(this);
		Brain->OnPossessed(InPawn);
		Brain->OnIntentSelected.AddUObject(this, &AMDEnemyController::HandleIntentSelected);
	}
	ConfigureSenses();
	TryStartExecutor();
}

void AMDEnemyController::OnUnPossess()
{
	if (Brain)
	{
		Brain->OnIntentSelected.RemoveAll(this);
		Brain->OnUnpossessed();
	}
	if (StateTree && StateTree->IsRunning())
	{
		StateTree->StopLogic(TEXT("Unpossessed"));
	}
	bExecutorStarted = false;
	Super::OnUnPossess();
}

void AMDEnemyController::ConfigureSenses()
{
	if (!Perception || !SightConfig || !Brain)
	{
		return;
	}
	const FMDTierParams& Tier = Brain->GetTier();
	SightConfig->SightRadius = Tier.SightRadius;
	SightConfig->LoseSightRadius = Tier.SightRadius + 300.0f;
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(Tier.SightFOV * 0.5f, 1.0f, 180.0f);
	Perception->ConfigureSense(*SightConfig);
	Perception->RequestStimuliListenerUpdate();
}

void AMDEnemyController::TryStartExecutor()
{
	if (bExecutorStarted || !StateTree || !HasActorBegunPlay() || !GetPawn())
	{
		return;
	}
	UStateTree* Asset = EnemyStateTree.IsNull() ? nullptr : EnemyStateTree.LoadSynchronous();
	if (!Asset)
	{
		return; // no executor (tests, or the asset is not built yet): the brain still decides and replicates its intent
	}
	if (!StateTree->IsRunning())
	{
		StateTree->SetStateTree(Asset);
		StateTree->StartLogic();
	}
	bExecutorStarted = StateTree->IsRunning();
	if (bExecutorStarted)
	{
		ResendIntentEvent();
	}
}

void AMDEnemyController::ResendIntentEvent()
{
	const AMDEnemyCharacter* Enemy = GetEnemy();
	if (Enemy)
	{
		HandleIntentSelected(Enemy->GetIntent(), true);
	}
}

void AMDEnemyController::HandleIntentSelected(EMDIntent Intent, bool bRestart)
{
	if (!StateTree || !StateTree->IsRunning())
	{
		return;
	}
	const FGameplayTag Tag = MDIntentToTag(Intent);
	if (Tag.IsValid())
	{
		StateTree->SendStateTreeEvent(Tag, FConstStructView(), TEXT("Brain"));
	}
}

void AMDEnemyController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (Brain && Actor)
	{
		Brain->ReportStimulus(Actor, Stimulus.WasSuccessfullySensed(), Stimulus.StimulusLocation);
	}
}
