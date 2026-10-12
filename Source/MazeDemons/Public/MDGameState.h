#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MDGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDFriendlyFireChanged, bool, bEnabled);

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

protected:
	UFUNCTION()
	void OnRep_FriendlyFire();
};
