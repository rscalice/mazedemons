#include "AI/MDAmbushMarker.h"

#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyBehaviourComponent.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "MDCheckpoint.h"
#include "MDGameplayTags.h"
#include "MDStation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

AMDAmbushMarker::AMDAmbushMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // the cue plays from here: the hidden ambushers are not net-relevant

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(300.0, 300.0, 150.0));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);

	Tags.Add(FName(TEXT("Spawn.Ambush")));
}

void AMDAmbushMarker::GatherSafeSpots(const UWorld* World, TArray<FVector>& OutSpots)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AMDStation> It(const_cast<UWorld*>(World)); It; ++It)
	{
		OutSpots.Add(It->GetActorLocation());
	}
	for (TActorIterator<AMDCheckpoint> It(const_cast<UWorld*>(World)); It; ++It)
	{
		OutSpots.Add(It->GetActorLocation());
	}
}

void AMDAmbushMarker::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMDAmbushMarker::HandleTriggerOverlap);
		// The enemies join their packs in their own BeginPlay / possession; arm a moment later.
		GetWorldTimerManager().SetTimer(ArmTimer, this, &AMDAmbushMarker::Arm, 0.5f, false);
	}
}

void AMDAmbushMarker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ArmTimer);
	GetWorldTimerManager().ClearTimer(RevealTimer);
	Super::EndPlay(EndPlayReason);
}

void AMDAmbushMarker::Arm()
{
	if (!HasAuthority() || bArmed || bTriggered)
	{
		return;
	}
	TArray<FVector> Safe;
	GatherSafeSpots(GetWorld(), Safe);
	if (MDBehaviour::IsNearSafeSpot(GetActorLocation(), Safe))
	{
		return; // never ambush at a checkpoint or a station: the group stays an ordinary, visible pack
	}
	bArmed = true;
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	for (AMDEnemyCharacter* Enemy : Ambushers)
	{
		if (!Enemy || Enemy->IsEnemyDead())
		{
			continue;
		}
		Enemy->SetAmbushHidden(true);
		if (Director)
		{
			if (const FMDPack* Pack = Director->FindPackOf(Enemy))
			{
				Director->SetAmbushArmed(Pack->PackId, true);
			}
		}
	}
}

void AMDAmbushMarker::HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (Pawn && Pawn->GetPlayerState())
	{
		TriggerAmbush(OtherActor);
	}
}

bool AMDAmbushMarker::TriggerAmbush(AActor* Player)
{
	if (!HasAuthority() || !bArmed || bTriggered || !Player)
	{
		return false;
	}
	bTriggered = true;
	TriggeringPlayer = Player;

	// The pack's tier decides: T2+ ambushes, below that the group simply appears (no cue, no grace).
	const UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	const AActor* First = nullptr;
	for (const AMDEnemyCharacter* Enemy : Ambushers)
	{
		if (Enemy && !Enemy->IsEnemyDead())
		{
			First = Enemy;
			break;
		}
	}
	const EMDIntelligenceTier Tier = (Director && First) ? Director->GetTierOf(First) : EMDIntelligenceTier::T0;
	TArray<FVector> Safe;
	GatherSafeSpots(GetWorld(), Safe);
	if (!MDBehaviour::CanAmbush(Tier, bIgnoreTier, MDBehaviour::IsNearSafeSpot(GetActorLocation(), Safe)))
	{
		RevealNow(false);
		return true;
	}
	Multicast_PlayAmbushCue();
	GetWorldTimerManager().SetTimer(RevealTimer, this, &AMDAmbushMarker::Reveal, FMath::Max(CueLead, 0.01f), false);
	return true;
}

void AMDAmbushMarker::Multicast_PlayAmbushCue_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!CueSound)
	{
		CueSound = LoadObject<USoundBase>(nullptr, *UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue::AmbushGrowl).ToString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (CueSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CueSound, GetActorLocation(), 1.0f);
	}
}

bool AMDAmbushMarker::IsPlayerTooClose() const
{
	for (const AMDEnemyCharacter* Enemy : Ambushers)
	{
		if (!Enemy || Enemy->IsEnemyDead())
		{
			continue;
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr;
			if (Pawn && FVector::Dist(Pawn->GetActorLocation(), Enemy->GetActorLocation()) < MDBehaviour::AmbushMinPlayerDistance)
			{
				return true;
			}
		}
	}
	return false;
}

void AMDAmbushMarker::Reveal()
{
	if (IsPlayerTooClose())
	{
		// Never appear inside a player's capsule: wait a moment.
		GetWorldTimerManager().SetTimer(RevealTimer, this, &AMDAmbushMarker::Reveal, 0.25f, false);
		return;
	}
	RevealNow(true);
}

void AMDAmbushMarker::RevealNow(bool bWithGrace)
{
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	AActor* Player = TriggeringPlayer.Get();
	for (AMDEnemyCharacter* Enemy : Ambushers)
	{
		if (!Enemy || Enemy->IsEnemyDead())
		{
			continue;
		}
		Enemy->SetAmbushHidden(false);
		if (UMDEnemyBehaviourComponent* Behaviour = Enemy->Behaviour; Behaviour && bWithGrace)
		{
			Behaviour->StartGrace(ArrivalGrace, true); // 0.5 s: no attacks, the group just arrived
		}
		if (UMDEnemyBrainComponent* Brain = UMDEnemyBrainComponent::FindFor(Enemy))
		{
			Brain->SetDormant(false, GetWorld()->GetTimeSeconds());
			if (Player)
			{
				Brain->ReportStimulus(Player, true, Player->GetActorLocation());
			}
		}
		if (Director)
		{
			if (const FMDPack* Pack = Director->FindPackOf(Enemy))
			{
				Director->SetAmbushArmed(Pack->PackId, false);
			}
		}
	}
	bArmed = false;
}
