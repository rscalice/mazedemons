#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MDGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDFriendlyFireChanged, bool, bEnabled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDRunProgressChanged);

// Replicated match rules. If the game mode derives from AGameMode (not AGameModeBase),
// this class must derive from AGameState instead.
UCLASS()
class MAZEDEMONS_API AMDGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AMDGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Rules")
	void SetFriendlyFireEnabled(bool bEnabled);

	// True when no AMDGameState exists (default rule).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Rules", meta = (WorldContext = "WorldContextObject"))
	static bool IsFriendlyFireEnabled(const UObject* WorldContextObject);

	UPROPERTY(ReplicatedUsing = OnRep_FriendlyFire, BlueprintReadOnly, Category = "Maze Demons|Rules")
	bool bFriendlyFireEnabled = true;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Rules")
	FMDFriendlyFireChanged OnFriendlyFireChanged;

	// ---- Zone checkpoint (respawn point for a solo player; see AMDPlayerController::Respawn) ----

	// Authority only. Ignored when Order is lower than the current checkpoint's order (never walks backwards).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Respawn")
	void SetCheckpoint(const FTransform& Transform, int32 Order);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Respawn")
	bool GetCheckpoint(FTransform& OutTransform) const;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	bool bHasCheckpoint = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	FTransform CheckpointTransform;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	int32 CheckpointOrder = 0;

	// ---- Run progress (perk points are computed from these; U4 sets the zones, V3 the souls) ----

	// Authority only. Clamped to 0..4.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Progress")
	void SetZonesCompleted(int32 Zones);

	// Authority only. Souls Saved never goes down during a run; SetSoulsSaved exists for the debug exec.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Progress")
	void AddSoulsSaved(int32 Delta);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Progress")
	void SetSoulsSaved(int32 Souls);

	// Zones finished this run (0..4).
	UPROPERTY(ReplicatedUsing = OnRep_RunProgress, BlueprintReadOnly, Category = "Maze Demons|Progress")
	int32 ZonesCompleted = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunProgress, BlueprintReadOnly, Category = "Maze Demons|Progress")
	int32 SoulsSaved = 0;

	// Fires on the server after a change and on clients after replication.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Progress")
	FMDRunProgressChanged OnRunProgressChanged;

protected:
	UFUNCTION()
	void OnRep_FriendlyFire();

	UFUNCTION()
	void OnRep_RunProgress();
};
