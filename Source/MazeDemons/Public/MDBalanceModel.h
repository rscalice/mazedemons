#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDPerkTypes.h"
#include "MDBalanceModel.generated.h"

// Player numbers the model starts from (the BALANCE.md start values; defaults equal a fresh character).
struct FMDBalanceBase
{
	float MaxHealth = 100.0f;
	float AttackPower = 10.0f;
	float BaseDefense = 0.0f;
	float MaxStamina = 100.0f;
	float StaminaRegen = 20.0f;
	float RegenDelay = 1.0f;
	float SprintDrainPerSec = 15.0f;
	float ExhaustedRecoverFraction = 0.3f;
	float DefenseCap = 65.0f;
};

// Outputs of one build against the reference enemies.
struct FMDBuildMetrics
{
	float EffectiveHPWerewolf = 0.0f; // total raw werewolf damage the player absorbs before dying
	float EffectiveHPBoss = 0.0f;
	float DPS = 0.0f;
	float SprintUptime = 0.0f;        // fraction of a continuous sprint / recover cycle spent sprinting, 0..1
	float TTKWerewolf = 0.0f;         // seconds
	float TTKBoss = 0.0f;
};

// Pure balance model (BALANCE section 10). World-free: takes the same FMDPerkModifiers the game applies.
UCLASS()
class MAZEDEMONS_API UMDBalanceModel : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Raw damage from hits of size EnemyHit the player survives: MaxHealth * EnemyHit / damageTakenPerHit, where
	// damageTakenPerHit = ApplyIncomingDamageRules(EnemyHit * (1 - DR)). EnemyHit <= 0 gives MaxHealth / (1 - DR).
	static float EffectiveHP(float MaxHealth, float DRPercent, float EnemyHit, float FlatReduction, bool bBulwark,
		const FMDPerkTuning& Tuning = FMDPerkTuning());

	// AttackPower * (1 + AttackPowerPct) * (1 + Crit * (CritDamage - 1)) * (1 + ExecutionerPct * ExecutionerShare) / AttackInterval.
	static float ExpectedDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare, float AttackInterval);

	// Continuous sprint then recover cycle: sprint Max / Drain seconds, then wait RegenDelay + RecoverFraction * Max / Regen.
	// Returns sprint / (sprint + recover), or 0 when the inputs cannot sprint.
	static float SprintUptime(float MaxStamina, float RegenPerSec, float RegenDelay, float DrainPerSec, float RecoverFraction);

	// Health / DPS in seconds; FLT_MAX when DPS <= 0.
	static float TimeToKill(float TargetHealth, float DPS);

	// Everything above for one modifier set. ExtraDRPercent / ExtraAPMult stand in for upgrades and gear.
	static FMDBuildMetrics Evaluate(const FMDPerkModifiers& Mods, const FMDBalanceBase& Base, const FMDBalanceReference& Ref,
		const FMDPerkTuning& Tuning, float ExtraDRPercent = 0.0f, float ExtraAPMult = 1.0f);
};
