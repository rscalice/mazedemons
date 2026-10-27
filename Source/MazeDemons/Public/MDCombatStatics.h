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

	// Server only. Damage that is not a weapon swing (a status effect tick, a trap, a spell): runs the same team rules and i-frames
	// as a hit, never crits and never staggers, and shows the attacker a floating number colored by DamageType (a Damage.Type.* tag;
	// empty = Physical). Instigator may be null (no number then). Returns true when health went down. V1 WP6 status effects call this
	// with Damage.Type.Fire (Burn), Frost, Poison and Bleed.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static bool ApplyTypedDamage(AActor* Instigator, AActor* Victim, float Damage, FGameplayTag DamageType, FVector Location = FVector::ZeroVector);

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

	// Attach-socket names that carry the weapon actor(s) a swing of Hand traces: Main = MainHand + TwoHanded, Off = OffHand,
	// Both = MainHand + TwoHanded + OffHand.
	static TArray<FName> TraceSocketsForHand(EMDWeaponHand Hand);

	// Pure rule: which hand(s) a trace window swings. Override != Auto wins (Main / Off / Both). Auto: no combo component -> Both
	// (every weapon found); a step on Off or Both is used as is; a Main step becomes Both when the loadout is a dual pair
	// (MainHand and OffHand weapons both attached) and the active combo set is bDualWield; otherwise Main (which also covers the
	// TwoHanded socket, see TraceSocketsForHand).
	static EMDWeaponHand ResolveTraceHand(EMDTraceHandOverride Override, bool bHasCombo, EMDWeaponHand StepHand, bool bDualPairAttached, bool bSetDualWield);

	// Per-swing trace state (previous blade position per hand + targets already hit), kept per attacker in C++.
	// Begin = reset at the start of the trace window, End = clear. Called by UMDANS_WeaponTrace (server).
	static void BeginWeaponTrace(const AActor* Attacker);
	static void EndWeaponTrace(const AActor* Attacker);

	// Server: finds the weapon actors attached to Attacker by socket (MainHand / OffHand / TwoHanded), picks the ones the active
	// combo step swings (all found when there is no combo component) and runs SweepWeaponHand on each. No dependency on AC_Combat.
	// TraceHand overrides the choice (Auto = ResolveTraceHand). One hit list is shared by both hands in a trace window (a target is
	// hit at most once per window even if both blades touch it); the previous blade positions are kept per hand. Existing callers
	// that pass only Attacker keep working (Auto).
	// Logs once per window which weapons were found and which hands were resolved. Debug: console "md.Combat.DrawTraces 1".
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static void TraceEquippedWeapons(AActor* Attacker, EMDTraceHandOverride TraceHand = EMDTraceHandOverride::Auto);

	// Cosmetic, any machine: the weapon actors attached to Attacker (MainHand / TwoHanded / OffHand sockets) that a swing with the
	// given TraceHand override currently swings (same hand resolution as TraceEquippedWeapons). Used by the sword trail notify.
	static void GetSwingWeapons(const AActor* Attacker, EMDTraceHandOverride TraceHand, TArray<AActor*>& OutWeapons);

	// The Heavy button was released. The melee abilities are granted with no GAS input id and the BP_Player input events are not
	// bound to the ability system, so GAS never calls InputReleased by itself: this does, on the avatar's active instance of
	// AbilityClass (GA_PlayerHeavy). No-op when the ability is not active. Call on the owning client.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	static void ReleaseAbilityInput(AActor* Avatar, TSubclassOf<UGameplayAbility> AbilityClass);

	// The weapon stance of an actor's loadout, for the anim BP (feeds the Chooser Table stance column). Accepts a pawn,
	// controller or player state (the inventory lives on the PlayerState, see UMDGameplayStatics::GetInventory). Unarmed when
	// the actor is null or has no inventory. Works on every machine (Equipped is replicated).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static EMDWeaponStance GetWeaponStanceForActor(const AActor* Actor);

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
