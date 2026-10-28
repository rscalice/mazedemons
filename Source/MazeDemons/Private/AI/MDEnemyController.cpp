#include "AI/MDEnemyController.h"

#include "AI/MDAIRules.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "MDGameplayStatics.h"
#include "Components/StateTreeAIComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "StateTree.h"

// The path follower is a UCrowdFollowingComponent (Detour crowd: avoidance and separation between enemies, v1-design 2.6).
AMDEnemyController::AMDEnemyController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
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

	// Hearing consumes any noise tag (Attack / Sprint / Gunshot ...); the loudness the reporter gives scales the range.
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = MDPerception::HearingRange(EMDIntelligenceTier::T0);
	HearingConfig->SetMaxAge(static_cast<float>(MDPerception::LoseSightTime));
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
	Perception->ConfigureSense(*HearingConfig);

	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
	DamageConfig->SetMaxAge(static_cast<float>(MDPerception::LoseSightTime));
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
	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		// RVO avoidance plus separation so a pack spreads around the player instead of stacking on one point.
		Crowd->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Medium);
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(2.0f);
		Crowd->SetCrowdAnticipateTurns(true);
		Crowd->SetCrowdOptimizeVisibility(true);
		Crowd->SetCrowdRotateToVelocity(false); // facing belongs to AMDEnemyCharacter::FaceMovement / FaceTarget, not the crowd
	}
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
	SightConfig->LoseSightRadius = Tier.SightRadius + MDPerception::LoseSightExtraRadius;
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(Tier.SightFOV * 0.5f, 1.0f, 180.0f);
	SightConfig->SetMaxAge(static_cast<float>(MDPerception::LoseSightTime));
	Perception->ConfigureSense(*SightConfig);
	if (HearingConfig)
	{
		HearingConfig->HearingRange = MDPerception::HearingRange(Tier.Tier);
		Perception->ConfigureSense(*HearingConfig);
	}
	Perception->RequestStimuliListenerUpdate();
}

void AMDEnemyController::SetSensesActive(bool bActive)
{
	if (!Perception)
	{
		return;
	}
	Perception->SetSenseEnabled(UAISense_Sight::StaticClass(), bActive);
	Perception->SetSenseEnabled(UAISense_Hearing::StaticClass(), bActive);
	// The damage sense is never switched off: being hurt wakes a dormant enemy.
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
	if (!Brain || !Actor)
	{
		return;
	}
	const bool bSensed = Stimulus.WasSuccessfullySensed();
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
	{
		if (bSensed)
		{
			Brain->ReportHeard(Actor, Stimulus.StimulusLocation, Stimulus.Tag); // any tag; Gunshot is loud
		}
		return; // a noise that ages out is not "lost sight": the brain's own 3 s memory covers it
	}
	const EMDStimulusKind Kind = Stimulus.Type == UAISense::GetSenseID<UAISense_Damage>() ? EMDStimulusKind::Damage : EMDStimulusKind::Sight;
	Brain->ReportStimulus(Actor, bSensed, Stimulus.StimulusLocation, Kind);
}
