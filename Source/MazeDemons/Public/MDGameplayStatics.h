#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDGameplayStatics.generated.h"

class UMDAbilitySystemComponent;
class UMDInventoryComponent;
class APawn;

namespace MDTeams
{
	constexpr uint8 Players = 1;
	constexpr uint8 Demons = 2;
}

UCLASS()
class MAZEDEMONS_API UMDGameplayStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Finds the ASC for an actor, checking the actor, its PlayerState (pawns), then components.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	static UMDAbilitySystemComponent* GetMDAbilitySystemComponent(const AActor* Actor);

	// The single lookup path for a player's inventory. Accepts a pawn, controller or PlayerState and finds the
	// UMDInventoryComponent on whichever of them hosts it.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	static UMDInventoryComponent* GetInventory(const AActor* Actor);

	// Interact input runs locally; this routes a pickup to the server from any machine. Interactor is the pawn
	// pressing interact (its controller must be an AMDPlayerController); Pickup is any actor (non-pickups are ignored).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	static void RequestPickup(AActor* Pickup, const APawn* Interactor);

	// Accepts a pawn, PlayerState or Controller. 1 = Players, 2 = Demons.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Teams")
	static uint8 GetTeamId(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Teams")
	static bool CanDamage(const AActor* DamageInstigator, const AActor* Target);
};
