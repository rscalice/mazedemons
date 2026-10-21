#include "MDQuestGate.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MDQuestComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMDQuestGate::AMDQuestGate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AMDQuestGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDQuestGate, bOpen);
}

void AMDQuestGate::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		TryBindQuests();
	}
	else if (bOpen)
	{
		OnGateOpenChanged(true, true); // late join: snap open
	}
}

void AMDQuestGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindTimer);
	}
	if (UMDQuestComponent* Quests = BoundQuests.Get())
	{
		Quests->OnQuestsChanged.RemoveDynamic(this, &AMDQuestGate::HandleQuestsChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void AMDQuestGate::TryBindQuests()
{
	if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
	{
		BoundQuests = Quests;
		Quests->OnQuestsChanged.AddUniqueDynamic(this, &AMDQuestGate::HandleQuestsChanged);
		Evaluate();
	}
	else if (UWorld* World = GetWorld())
	{
		// The game state may not exist yet: retry.
		World->GetTimerManager().SetTimer(BindTimer, this, &AMDQuestGate::TryBindQuests, 0.5f, false);
	}
}

void AMDQuestGate::HandleQuestsChanged()
{
	Evaluate();
}

void AMDQuestGate::Evaluate()
{
	const UMDQuestComponent* Quests = BoundQuests.Get();
	if (!HasAuthority() || !Quests)
	{
		return;
	}
	const bool bShouldOpen = Quests->IsZoneQuestComplete(ZoneIndex);
	if (bShouldOpen != bOpen)
	{
		bOpen = bShouldOpen;
		OnGateOpenChanged(bOpen, false); // host / standalone: no OnRep
	}
}

void AMDQuestGate::OnRep_Open()
{
	if (HasActorBegunPlay()) // before BeginPlay the initial value is handled there (snap)
	{
		OnGateOpenChanged(bOpen, false);
	}
}
