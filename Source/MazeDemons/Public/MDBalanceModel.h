#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDInventoryTypes.h"
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
	float ExhaustedRecoverFraction = 0.3f; // exhausted lock-out only; not used by the sprint-cycle model
	// Reference sprint cycle (BALANCE section 8): sprint from full to empty, wait RegenDelay, refill to full at the regen rate.
	// 1.0 reproduces BALANCE's two anchors: bare character ~53%, max Movement perks ~72%.
	float SprintRefillFraction = 1.0f;
	float DefenseCap = 65.0f;
};

// Outputs of one build against the reference enemies.
struct FMDBuildMetrics
{
	// BALANCE section 8 effective HP (the ceilinged measure): MaxHealth / (1 - DR), health-weighted for Stalwart, with Bulwark
	// applied per hit against the largest reference hit (the boss). Thick Hide is NOT part of it.
	float EffectiveHP = 0.0f;
	// Informational only (never ceilinged): the same including Thick Hide against the 15 damage werewolf hit / 30 damage boss hit.
	float EffectiveHPWerewolf = 0.0f;
	float EffectiveHPBoss = 0.0f;
	float DPS = 0.0f;                 // AttackPower x (1 + crit x (critDmg - 1)) / swing interval (the ceilinged measure)
	float DPSExecutioner = 0.0f;      // informational: DPS including Executioner over its share of a fight (used for time to kill)
	float SprintUptime = 0.0f;        // sprint / (sprint + recovery) of the reference cycle, 0..1
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

	// EffectiveHP over a health bar whose lower Threshold fraction has a second damage reduction (Stalwart): the upper part is
	// absorbed at DRAbove, the lower part at DRBelow. FlatReduction / Bulwark as in EffectiveHP.
	static float EffectiveHPStalwart(float MaxHealth, float DRAbovePercent, float DRBelowPercent, float Threshold, float EnemyHit,
		float FlatReduction, bool bBulwark, const FMDPerkTuning& Tuning = FMDPerkTuning());

	// AttackPower * (1 + AttackPowerPct) * (1 + Crit * (CritDamage - 1)) * (1 + ExecutionerPct * ExecutionerShare) / AttackInterval.
	static float ExpectedDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare, float AttackInterval);

	// Average DPS of a whole attack chain (C1): AttackPower x sum(DamageMults) over the chain's total time, with the same perk
	// modifiers as ExpectedDPS. StepIntervals[i] is the time from the start of step i to the start of the next (the last step's is
	// its recovery before the chain can repeat). Equals ExpectedDPS with interval = total time / total multiplier; 0 for an empty or
	// non-positive chain. Extra entries in the longer array are ignored.
	static float ChainAverageDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare,
		const TArray<float>& DamageMults, const TArray<float>& StepIntervals);

	// Reference cycle: sprint Max / Drain seconds, then recover for RegenDelay + RefillFraction * Max / Regen seconds.
	// Returns sprint / (sprint + recover), or 0 when the inputs cannot sprint. With RefillFraction 1: bare 100 / 15 = 6.67 s
	// against 1 + 100 / 20 = 6 s gives 52.6%; max Movement (150, drain 10.5, regen 29, delay 0.5) 14.3 s against 5.67 s gives 71.6%.
	static float SprintUptime(float MaxStamina, float RegenPerSec, float RegenDelay, float DrainPerSec, float RefillFraction);

	// The "upgrades and gear" scenario derived from the economy tuning: starter cloth set (helmet 4, chest 8, legs 4, boots 2 %)
	// with every piece at Tuning.MaxUpgradeLevel, full condition, combined multiplicatively and capped at PlayerDRCap
	// (13 / 17 / 13 / 11 -> 44.09%), and the main weapon at the same level (attack power x1.18).
	static void GetGearScenario(const FMDEconomyTuning& Economy, float& OutDRPercent, float& OutAPMult);

	// Health / DPS in seconds; FLT_MAX when DPS <= 0.
	static float TimeToKill(float TargetHealth, float DPS);

	// Everything above for one modifier set. ExtraDRPercent / ExtraAPMult stand in for upgrades and gear.
	static FMDBuildMetrics Evaluate(const FMDPerkModifiers& Mods, const FMDBalanceBase& Base, const FMDBalanceReference& Ref,
		const FMDPerkTuning& Tuning, float ExtraDRPercent = 0.0f, float ExtraAPMult = 1.0f);
};
