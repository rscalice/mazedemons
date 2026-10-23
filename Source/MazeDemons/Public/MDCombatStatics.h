#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDCombatStatics.generated.h"

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
