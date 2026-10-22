#include "MDObjectiveMarker.h"

#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDQuestComponent.h"
#include "Net/UnrealNetwork.h"

AMDObjectiveMarker::AMDObjectiveMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(150.0f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	SetRootComponent(Trigger);
}

void AMDObjectiveMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDObjectiveMarker, bConsumed);
}

void AMDObjectiveMarker::BeginPlay()
{
	Super::BeginPlay();

	if (ObjectiveType == EMDObjectiveType::Interact)
	{
		// Must equal GA_PlayerInteract's RequiredActorTag; the focus sweep needs a Visibility block to hit the trigger.
		Tags.AddUnique(FName(TEXT("Interactable")));
		Trigger->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}
	if (HasAuthority() && (ObjectiveType == EMDObjectiveType::Reach || ObjectiveType == EMDObjectiveType::Collect))
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMDObjectiveMarker::OnTriggerOverlap);
	}
	ApplyConsumedState(); // late joiners: bConsumed arrived with the actor
	if (bConsumed)
	{
		OnConsumedChanged(true);
	}
}

void AMDObjectiveMarker::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryActivate(Cast<APawn>(OtherActor));
}

bool AMDObjectiveMarker::TryActivate(APawn* User)
{
	if (!HasAuthority() || bConsumed || !User || !User->IsPlayerControlled())
	{
		return false;
	}
	const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(User);
	if (ASC && ASC->IsDead())
	{
		return false;
	}
	UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this);
	if (!Quests || !Quests->ReportEvent(ObjectiveType, ObjectiveTags, FMath::Max(Amount, 1), User->GetPlayerState(), GetActorLocation()))
	{
		return false;
	}
	bConsumed = true;
	ApplyConsumedState();
	OnConsumedChanged(true);
	return true;
}

void AMDObjectiveMarker::Interact(APawn* User)
{
	if (!HasAuthority() || !User || ObjectiveType != EMDObjectiveType::Interact)
	{
		return;
	}
	if (FVector::DistSquared(User->GetActorLocation(), GetActorLocation()) > FMath::Square(FMath::Max(MaxInteractDistance, 0.0f)))
	{
		return;
	}
	TryActivate(User);
}

void AMDObjectiveMarker::ResetConsumed()
{
	if (!HasAuthority() || !bConsumed)
	{
		return;
	}
	bConsumed = false;
	ApplyConsumedState();
	OnConsumedChanged(false);
}

void AMDObjectiveMarker::ApplyConsumedState()
{
	if (Trigger)
	{
		Trigger->SetCollisionEnabled(bConsumed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}
}

void AMDObjectiveMarker::OnRep_Consumed()
{
	ApplyConsumedState();
	OnConsumedChanged(bConsumed);
}
