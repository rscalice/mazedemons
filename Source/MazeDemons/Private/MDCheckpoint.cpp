#include "MDCheckpoint.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AMDCheckpoint::AMDCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // server-only logic; the result lives on AMDGameState

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetBoxExtent(FVector(200.0f, 200.0f, 120.0f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	SetRootComponent(Trigger);

	Facing = CreateDefaultSubobject<UArrowComponent>(TEXT("Facing"));
	Facing->SetupAttachment(Trigger);
}

void AMDCheckpoint::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMDCheckpoint::OnTriggerOverlap);
	}
}

void AMDCheckpoint::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!HasAuthority() || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Pawn);
	if (ASC && ASC->IsDead())
	{
		return;
	}
	if (AMDGameState* State = GetWorld() ? GetWorld()->GetGameState<AMDGameState>() : nullptr)
	{
		// Respawn on the floor under the trigger centre, facing along the arrow.
		FTransform Spot = Facing->GetComponentTransform();
		Spot.SetScale3D(FVector::OneVector);
		State->SetCheckpoint(Spot, Order);
	}
}
