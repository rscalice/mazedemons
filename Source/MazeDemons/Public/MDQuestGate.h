#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "MDQuestGate.generated.h"

// The Zone 3 gate. Opens (server) when ZoneIndex's quest is complete and closes again if the quests are reset. The open
// animation lives in the Blueprint child (OnGateOpenChanged); late joiners get bSnap = true and skip the animation.
// Placed by marker in the level, never referenced from the quest data.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDQuestGate : public AActor
{
	GENERATED_BODY()

public:
	AMDQuestGate();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The zone whose quest opens this gate (1..4).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = "1", ClampMax = "4"))
	int32 ZoneIndex = 3;

	UPROPERTY(ReplicatedUsing = OnRep_Open, BlueprintReadOnly, Category = "Gate")
	bool bOpen = false;

	// Every machine: play the open / close animation (bSnap = jump to the end state, used for late joiners).
	UFUNCTION(BlueprintImplementableEvent, Category = "Gate")
	void OnGateOpenChanged(bool bNowOpen, bool bSnap);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_Open();

	UFUNCTION()
	void HandleQuestsChanged();

private:
	void TryBindQuests();
	void Evaluate();

	TWeakObjectPtr<class UMDQuestComponent> BoundQuests;
	FTimerHandle BindTimer;
};
