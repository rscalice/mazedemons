#include "MDQuestComponent.h"

#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MDGameState.h"
#include "MDMapZone.h"
#include "MDObjectiveMarker.h"
#include "MDPlayerController.h"
#include "MDQuestSetData.h"
#include "MDUITypes.h"
#include "Net/UnrealNetwork.h"

UMDQuestComponent::UMDQuestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UMDQuestComponent* UMDQuestComponent::FindFor(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AMDGameState* State = World ? World->GetGameState<AMDGameState>() : nullptr;
	return State ? State->QuestComponent.Get() : nullptr;
}

void UMDQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDQuestComponent, Quests);
}

bool UMDQuestComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UMDQuestComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsServer())
	{
		BuildInitialQuests();
		OnQuestsChanged.Broadcast();
	}
}

void UMDQuestComponent::BuildInitialQuests()
{
	Quests.Reset();
	for (int32 Zone = 1; Zone <= 4; ++Zone)
	{
		FMDObjectiveDef Def;
		const bool bHasDef = GetObjectiveDef(Zone, Def);

		FMDQuestProgress Progress;
		Progress.ZoneIndex = Zone;
		Progress.State = Zone == 1 ? EMDQuestState::Active : EMDQuestState::Locked;
		Progress.Count = 0;
		Progress.Required = bHasDef ? FMath::Max(Def.Required, 1) : 1;
		Quests.Add(Progress);
	}
}

bool UMDQuestComponent::GetObjectiveDef(int32 Zone, FMDObjectiveDef& OutDef) const
{
	if (QuestSet && QuestSet->Zones.IsValidIndex(Zone - 1))
	{
		OutDef = QuestSet->Zones[Zone - 1].Objective;
		return true;
	}
	return false;
}

int32 UMDQuestComponent::FindActiveIndex() const
{
	return Quests.IndexOfByPredicate([](const FMDQuestProgress& Quest) { return Quest.State == EMDQuestState::Active; });
}

bool UMDQuestComponent::GetActiveQuest(FMDQuestProgress& OutQuest) const
{
	const int32 Index = FindActiveIndex();
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutQuest = Quests[Index];
	return true;
}

FText UMDQuestComponent::GetQuestTitle(int32 Zone) const
{
	if (QuestSet && QuestSet->Zones.IsValidIndex(Zone - 1))
	{
		return QuestSet->Zones[Zone - 1].Title;
	}
	return FText::GetEmpty();
}

FText UMDQuestComponent::GetObjectiveText(int32 Zone) const
{
	if (!Quests.IsValidIndex(Zone - 1))
	{
		return FText::GetEmpty();
	}
	FMDObjectiveDef Def;
	GetObjectiveDef(Zone, Def);
	return UMDQuestRules::FormatObjective(Def, Quests[Zone - 1]);
}

bool UMDQuestComponent::IsZoneQuestComplete(int32 Zone) const
{
	return Quests.IsValidIndex(Zone - 1) && Quests[Zone - 1].State == EMDQuestState::Completed;
}

bool UMDQuestComponent::AreAllQuestsDone() const
{
	return Quests.Num() > 0 && UMDQuestRules::CountZonesCompleted(Quests) >= Quests.Num();
}

bool UMDQuestComponent::ReportEvent(EMDObjectiveType Type, const FGameplayTagContainer& Tags, int32 Delta, APlayerState* Instigator, FVector Location)
{
	if (!IsServer() || Delta <= 0)
	{
		return false;
	}
	const int32 Index = FindActiveIndex();
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FMDObjectiveDef Def;
	if (!GetObjectiveDef(Quests[Index].ZoneIndex, Def) || !UMDQuestRules::MatchesObjective(Def, Type, Tags))
	{
		return false;
	}
	if (Type == EMDObjectiveType::Kill)
	{
		const AMDMapZone* VictimZone = AMDMapZone::FindZoneAt(this, Location);
		if (!UMDQuestRules::KillCountsForZone(VictimZone ? VictimZone->ZoneIndex : 0, Quests[Index].ZoneIndex))
		{
			return false;
		}
	}
	return ApplyToQuest(Index, Delta);
}

void UMDQuestComponent::ReportKill(const UObject* WorldContextObject, const FGameplayTagContainer& VictimTags, const FVector& VictimLocation, APlayerState* Killer)
{
	if (UMDQuestComponent* Component = FindFor(WorldContextObject))
	{
		Component->ReportEvent(EMDObjectiveType::Kill, VictimTags, 1, Killer, VictimLocation);
	}
}

bool UMDQuestComponent::ApplyToQuest(int32 Index, int32 Delta)
{
	if (!Quests.IsValidIndex(Index))
	{
		return false;
	}
	FMDQuestProgress& Quest = Quests[Index];
	if (!UMDQuestRules::ApplyProgress(Quest, Delta))
	{
		return false;
	}
	if (Quest.State == EMDQuestState::Completed)
	{
		HandleZoneCompleted(Quest.ZoneIndex);
	}
	else
	{
		OnQuestsChanged.Broadcast();
	}
	return true;
}

void UMDQuestComponent::HandleZoneCompleted(int32 Zone)
{
	UMDQuestRules::UnlockNext(Quests);
	if (AMDGameState* State = Cast<AMDGameState>(GetOwner()))
	{
		State->SetZonesCompleted(UMDQuestRules::CountZonesCompleted(Quests)); // perk points follow this
	}
	// No toast here: the HUD (W_MDHUD -> ProHUD) shows its own "Zone N cleansed"
	// notification when it sees the zone complete, so a quest toast would double it.
	OnQuestsChanged.Broadcast();
}

void UMDQuestComponent::ForceCompleteZone(int32 Zone)
{
	if (!IsServer() || !Quests.IsValidIndex(Zone - 1) || Quests[Zone - 1].State == EMDQuestState::Completed)
	{
		return;
	}
	FMDQuestProgress& Quest = Quests[Zone - 1];
	Quest.Count = Quest.Required;
	Quest.State = EMDQuestState::Completed;
	HandleZoneCompleted(Zone);
}

void UMDQuestComponent::DebugAddProgress(int32 Delta)
{
	if (IsServer())
	{
		ApplyToQuest(FindActiveIndex(), Delta);
	}
}

void UMDQuestComponent::ResetQuests()
{
	if (!IsServer())
	{
		return;
	}
	BuildInitialQuests();
	if (AMDGameState* State = Cast<AMDGameState>(GetOwner()))
	{
		State->SetZonesCompleted(0);
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMDObjectiveMarker> It(World); It; ++It)
		{
			It->ResetConsumed();
		}
	}
	OnQuestsChanged.Broadcast();
}

void UMDQuestComponent::OnRep_Quests()
{
	OnQuestsChanged.Broadcast();
}
