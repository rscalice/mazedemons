#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MDQuestTypes.h"
#include "MDQuestComponent.generated.h"

class APlayerState;
class UMDQuestSetData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDQuestsChanged);

// The run's four zone quests, on AMDGameState. Server-authoritative: events come in through ReportEvent, state replicates as a
// plain array (late joiners get everything), and this component is the only gameplay writer of AMDGameState::ZonesCompleted.
// Zone numbers everywhere are 1-based (Zone 1..4).
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDQuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDQuestComponent();

	// The quest component of the world's AMDGameState, or null.
	static UMDQuestComponent* FindFor(const UObject* WorldContextObject);

	// DA_MDQuestSet_Slice, set in the GameState Blueprint. Empty: four quests with Required 1 that no event matches.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Quests")
	TObjectPtr<UMDQuestSetData> QuestSet;

	// Four entries after BeginPlay (server) / first replication (clients).
	UPROPERTY(ReplicatedUsing = OnRep_Quests, BlueprintReadOnly, Category = "Maze Demons|Quests")
	TArray<FMDQuestProgress> Quests;

	// Fires on the server after a change and on clients after replication (same rule as OnRunProgressChanged).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Quests")
	FMDQuestsChanged OnQuestsChanged;

	// ---- Queries (all machines) ----

	// The first Active quest. False when every quest is done (or none exist yet).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Quests")
	bool GetActiveQuest(FMDQuestProgress& OutQuest) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Quests")
	FText GetQuestTitle(int32 Zone) const;

	// "Slay ghouls 3/6" for the zone's quest (empty when the zone does not exist).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Quests")
	FText GetObjectiveText(int32 Zone) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Quests")
	bool IsZoneQuestComplete(int32 Zone) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Quests")
	bool AreAllQuestsDone() const;

	// Authoring data for a zone's objective (false when there is no quest set entry).
	bool GetObjectiveDef(int32 Zone, FMDObjectiveDef& OutDef) const;

	// ---- Server ----

	// An objective event happened. Only the Active quest listens: type and tag must match, and a Kill must happen inside the
	// quest's map zone. True when progress was applied (marker actors consume themselves only then).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Quests")
	bool ReportEvent(EMDObjectiveType Type, const FGameplayTagContainer& Tags, int32 Delta, APlayerState* Instigator, FVector Location);

	// Kill hook (AMDAbilitySystemComponent, next to AMDPlayerState::AddKill). Server only.
	static void ReportKill(const UObject* WorldContextObject, const FGameplayTagContainer& VictimTags, const FVector& VictimLocation, APlayerState* Killer);

	// Debug: completes Zone's quest whatever its state (earlier zones are left alone).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Quests")
	void ForceCompleteZone(int32 Zone);

	// Debug: adds Delta progress to the Active quest, ignoring its objective type and tag.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Quests")
	void DebugAddProgress(int32 Delta);

	// Zone 1 Active, the rest Locked, zero progress, ZonesCompleted 0, objective markers un-consumed.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Quests")
	void ResetQuests();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

protected:
	UFUNCTION()
	void OnRep_Quests();

private:
	bool IsServer() const;
	void BuildInitialQuests();
	int32 FindActiveIndex() const;
	bool ApplyToQuest(int32 Index, int32 Delta);
	void HandleZoneCompleted(int32 Zone);
};
