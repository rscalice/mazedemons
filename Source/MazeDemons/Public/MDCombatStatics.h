#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDCombatTypes.h"
#include "MDCombatStatics.generated.h"

class UGameplayAbility;
class UMDLocalSettings;

UCLASS()
class MAZEDEMONS_API UMDCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Server only. The one entry point AC_Combat's weapon trace calls for each target it hits (once per target per swing).
	// Builds the hit context from the attacker's active combo step (or a default light step without one), runs
	// ApplyDamage(FMDHitContext) (team rules, crit, Executioner, i-frames), and only if damage went through: poise and reaction,
	// knockback (a horizontal slide for players, a launch for enemies), then Client_ConfirmHit to the attacker.
	// Returns true when damage was applied.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static bool ApplyMeleeHit(AActor* Attacker, AActor* Victim, const FHitResult& Hit);

	// Where a held weapon's blade is: Top = the tip end, Bottom = a point toward the hilt. Uses the weapon root's sockets
	// "TopTrace" / "BottomTrace" when both exist, else the long axis of the weapon's mesh bounds (the Fab swords carry no trace
	// sockets, and this keeps them hit-capable without editing Content/Fab). Both zero when Weapon is null/invalid.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static void GetWeaponTracePoints(const AActor* Weapon, FVector& Top, FVector& Bottom);

	// Which weapon(s) the attacker's swing in progress traces (Main when there is no combo component or no active step).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static EMDWeaponHand GetAttackerHand(const AActor* Attacker);

	// One weapon's sweep for this tick (AC_Combat.DoWeaponTrace calls it once per hand). Sweeps a blade-aligned box from the
	// previous blade position to the current one, then stores the current position in PrevTop/PrevBottom. A zero PrevTop means
	// "first tick": no sweep, just record. Authority only applies hits: each new target goes through ApplyMeleeHit (anything
	// without an ability system falls back to plain ApplyDamage) and into HitThisAttack so it is not hit again this swing.
	// Safe with a null / destroyed Weapon (an empty off hand): does nothing.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static void SweepWeaponHand(AActor* Attacker, AActor* Weapon, UPARAM(ref) FVector& PrevTop, UPARAM(ref) FVector& PrevBottom, UPARAM(ref) TArray<AActor*>& HitThisAttack);

	// The Heavy button was released. The melee abilities are granted with no GAS input id and the BP_Player input events are not
	// bound to the ability system, so GAS never calls InputReleased by itself: this does, on the avatar's active instance of
	// AbilityClass (GA_PlayerHeavy). No-op when the ability is not active. Call on the owning client.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static void ReleaseAbilityInput(AActor* Avatar, TSubclassOf<UGameplayAbility> AbilityClass);

	// md.Combat.DrawTraces, for AC_Combat's trace to draw its debug lines.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static bool ShouldDrawTraces();

	// The local combat options (shake %, hit-stop, rumble, damage numbers, VFX level, after-images). Change a field, then call
	// SaveCombatOptions.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static UMDLocalSettings* GetCombatOptions();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static void SaveCombatOptions();
};
