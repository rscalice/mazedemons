#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDUITypes.h"
#include "MDGameplayStatics.generated.h"

class UMDAbilitySystemComponent;
class UMDInventoryComponent;
class UMDStaminaComponent;
class APawn;
class APlayerState;
class AController;

// One row of the character stats screen. Texts are pre-formatted ("85 / 100", "12 /s", "16.9%") so UI only displays them.
// With a compare row, New/NewText show the value after the swap and DeltaSign is +1 / -1 / 0 (0 = unchanged or no compare).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDStatLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Current = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float New = 0.0f;

	// Value is a percentage (Current / New are 0..100).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	bool bPercent = false;

	// Value is shown as "value / Max" (health, stamina); Max is valid.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	bool bRatio = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Max = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 DeltaSign = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FText CurrentText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FText NewText;
};

// One row of the Character panel's "Active effects" list: a perk rank, an upgrade bonus, worn gear or Exhausted.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDEffectLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FText Detail;

	// "Perks", "Upgrades", "Condition", "Status".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FText Source;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	EMDColor Tone = EMDColor::TextPrimary;

	// Drawn with the Danger bar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	bool bDebuff = false;

	// Icon lookup key (perk node id, status id); empty = no icon.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FName IconKey = NAME_None;
};

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

	// Player-facing item name: DT_ItemDisplay (UMDUISettings::ItemDisplayTable) DisplayName, else the row name split on
	// CamelCase ("HealthPotion" -> "Health Potion").
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	static FText GetItemDisplayName(FName Row);

	// Pure helper: inserts spaces at CamelCase boundaries and turns underscores into spaces.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	static FString SplitCamel(const FString& In);

	// ---- Character stats screen ----
	// Rows in order: Health, Stamina, Stamina regen, Attack power, Defense (base), Damage reduction (total).
	// CompareRow = an item row to preview equipping / using (NAME_None = no compare). Works on clients (replicated data only).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static TArray<FMDStatLine> GetCharacterStatLines(APlayerState* PS, FName CompareRow);

	// ---- Perks ----
	// Unspent perk points of a PlayerState (earned minus spent), 0 when it has no perk component. Owner-only data: on a
	// remote player's PlayerState this is just what that machine can see.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	static int32 GetPerkPointsAvailable(APlayerState* PS);

	// ---- Character panel: active effects (works on clients from replicated data) ----
	// Order: Perks (one line per ranked node), Upgrades, Condition debuffs, Exhausted. Status effects join with V1. A fresh
	// character with nothing equipped and no perks returns no lines.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Stats")
	static TArray<FMDEffectLine> GetActiveEffectLines(APlayerState* PS);

	// Pure: the "Worn <item>: 81% effectiveness" debuff line for an item whose condition multiplier is below 1. False (and
	// OutLine untouched) when the item is at full strength.
	static bool MakeConditionLine(const FText& ItemName, float CondMult, FMDEffectLine& OutLine);

	// Pure: total damage reduction % after replacing a piece (OldPiecePct, 0 if the slot is empty) with NewPiecePct.
	// 1 - (1-c)(1-n)/(1-o), in percent, clamped to [0, 90].
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float SwapDamageReduction(float CurrentPct, float OldPiecePct, float NewPiecePct);

	// Pure: health after using a heal, capped at max.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stats")
	static float HealthAfterHeal(float Health, float MaxHealth, float HealAmount);

	// Pure: colour token for a durability bar. >= 50% Success, 25..49% Warning, below 25% Danger. Fraction is clamped to [0,1].
	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static EMDColor DurabilityColor(float Fraction);

	// ---- Gold ----
	// Server only. Splits Gold evenly among the players within KillGoldRadius of Victim (dead players' corpses count); the
	// remainder goes to Killer if it is one of them, else to the nearest. Each recipient gets AddGold + a "+N Gold" toast.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Gold")
	static void AwardKillGold(const AActor* Victim, const AController* Killer, int32 Gold);

	// ---- Stamina (accepts a pawn, controller or PlayerState). Actors with no stamina component (AI) are never gated. ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	static UMDStaminaComponent* GetStaminaComponent(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	static void GetStamina(const AActor* Actor, float& Stamina, float& MaxStamina);

	// Sprint gate: strict for the locally controlled pawn, short grace on the server for remote clients' pawns.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	static bool CanSprint(const APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	static bool HasStaminaForJump(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Stamina")
	static bool HasStaminaForVault(const AActor* Actor);

	// Charge on the server when a jump / vault actually starts. No-ops on clients.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Stamina")
	static void NotifyJumped(const AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Stamina")
	static void NotifyTraversalStarted(const AActor* Actor);

	// Accepts a pawn, PlayerState or Controller. 1 = Players, 2 = Demons.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Teams")
	static uint8 GetTeamId(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Teams")
	static bool CanDamage(const AActor* DamageInstigator, const AActor* Target);
};
