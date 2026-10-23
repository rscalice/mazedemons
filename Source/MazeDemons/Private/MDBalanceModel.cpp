#include "MDBalanceModel.h"

#include "MDPerkCatalog.h"
#include "MDStatCalculator.h"

float UMDBalanceModel::EffectiveHP(float MaxHealth, float DRPercent, float EnemyHit, float FlatReduction, bool bBulwark, const FMDPerkTuning& Tuning)
{
	const float Pass = 1.0f - FMath::Clamp(DRPercent, 0.0f, 100.0f) / 100.0f;
	if (EnemyHit <= 0.0f)
	{
		return Pass > 0.0f ? MaxHealth / Pass : MaxHealth;
	}
	const float Taken = UMDPerkCatalog::ApplyIncomingDamageRules(EnemyHit * Pass, MaxHealth, FlatReduction, bBulwark, Tuning);
	return Taken > KINDA_SMALL_NUMBER ? MaxHealth * EnemyHit / Taken : MaxHealth * EnemyHit / KINDA_SMALL_NUMBER;
}

float UMDBalanceModel::EffectiveHPStalwart(float MaxHealth, float DRAbovePercent, float DRBelowPercent, float Threshold, float EnemyHit,
	float FlatReduction, bool bBulwark, const FMDPerkTuning& Tuning)
{
	const float Below = FMath::Clamp(Threshold, 0.0f, 1.0f);
	return (1.0f - Below) * EffectiveHP(MaxHealth, DRAbovePercent, EnemyHit, FlatReduction, bBulwark, Tuning)
		+ Below * EffectiveHP(MaxHealth, DRBelowPercent, EnemyHit, FlatReduction, bBulwark, Tuning);
}

float UMDBalanceModel::ExpectedDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare, float AttackInterval)
{
	const float Interval = FMath::Max(AttackInterval, KINDA_SMALL_NUMBER);
	const float CritFactor = 1.0f + Mods.CritChance * FMath::Max(Mods.CritDamage - 1.0f, 0.0f);
	const float ExecFactor = 1.0f + Mods.ExecutionerPct * FMath::Clamp(ExecutionerShare, 0.0f, 1.0f);
	return AttackPower * (1.0f + Mods.AttackPowerPct) * CritFactor * ExecFactor / Interval;
}

float UMDBalanceModel::ChainAverageDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare,
	const TArray<float>& DamageMults, const TArray<float>& StepIntervals)
{
	const int32 Count = FMath::Min(DamageMults.Num(), StepIntervals.Num());
	float TotalMult = 0.0f;
	float TotalTime = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		TotalMult += FMath::Max(DamageMults[Index], 0.0f);
		TotalTime += FMath::Max(StepIntervals[Index], 0.0f);
	}
	if (TotalMult <= 0.0f || TotalTime <= 0.0f)
	{
		return 0.0f;
	}
	return ExpectedDPS(AttackPower, Mods, ExecutionerShare, TotalTime / TotalMult);
}

float UMDBalanceModel::SprintUptime(float MaxStamina, float RegenPerSec, float RegenDelay, float DrainPerSec, float RefillFraction)
{
	if (MaxStamina <= 0.0f || DrainPerSec <= 0.0f || RegenPerSec <= 0.0f)
	{
		return 0.0f;
	}
	const float Sprint = MaxStamina / DrainPerSec;
	const float Recover = FMath::Max(RegenDelay, 0.0f) + FMath::Clamp(RefillFraction, 0.0f, 1.0f) * MaxStamina / RegenPerSec;
	return Sprint / (Sprint + Recover);
}

void UMDBalanceModel::GetGearScenario(const FMDEconomyTuning& Economy, float& OutDRPercent, float& OutAPMult)
{
	// Starter cloth set: helmet, chest, legs, boots (there is no gloves piece in the set).
	const float ClothRowDR[4] = { 4.0f, 8.0f, 4.0f, 2.0f };
	TArray<float> Pieces;
	for (const float RowDR : ClothRowDR)
	{
		Pieces.Add(UMDStatCalculator::EffectivePieceDR(RowDR, Economy.MaxUpgradeLevel, 1.0f, Economy));
	}
	OutDRPercent = UMDStatCalculator::CombineDR(0.0f, Pieces, Economy.PlayerDRCap);
	OutAPMult = UMDStatCalculator::EffectiveWeaponAttackPower(1.0f, 0.0f, Economy.MaxUpgradeLevel, 1.0f, Economy);
}

float UMDBalanceModel::TimeToKill(float TargetHealth, float DPS)
{
	return DPS > 0.0f ? TargetHealth / DPS : MAX_FLT;
}

FMDBuildMetrics UMDBalanceModel::Evaluate(const FMDPerkModifiers& Mods, const FMDBalanceBase& Base, const FMDBalanceReference& Ref,
	const FMDPerkTuning& Tuning, float ExtraDRPercent, float ExtraAPMult)
{
	FMDBuildMetrics Out;

	// Damage reduction: base defense, Hardened, gear; Stalwart joins below its health threshold.
	TArray<float> Pieces;
	Pieces.Add(Mods.DamageReductionPP);
	Pieces.Add(ExtraDRPercent);
	const float DR = UMDStatCalculator::CombineDR(Base.BaseDefense, Pieces, Base.DefenseCap);
	Pieces.Add(Mods.StalwartPP);
	const float DRLow = UMDStatCalculator::CombineDR(Base.BaseDefense, Pieces, Base.DefenseCap);

	const float MaxHealth = Base.MaxHealth + Mods.MaxHealthBonus;
	// Ceilinged measure: no Thick Hide, Bulwark per hit against the boss hit (the only reference hit above 30% of 100 health).
	Out.EffectiveHP = EffectiveHPStalwart(MaxHealth, DR, DRLow, Tuning.StalwartHealthThreshold, Ref.BossHit, 0.0f, Mods.bBulwark, Tuning);
	// Informational: Thick Hide depends on the hit size, so report it per reference hit.
	Out.EffectiveHPWerewolf = EffectiveHPStalwart(MaxHealth, DR, DRLow, Tuning.StalwartHealthThreshold, Ref.WerewolfHit, Mods.FlatDamageReduction, Mods.bBulwark, Tuning);
	Out.EffectiveHPBoss = EffectiveHPStalwart(MaxHealth, DR, DRLow, Tuning.StalwartHealthThreshold, Ref.BossHit, Mods.FlatDamageReduction, Mods.bBulwark, Tuning);

	Out.DPS = ExpectedDPS(Base.AttackPower * ExtraAPMult, Mods, 0.0f, Ref.PlayerAttackInterval);
	Out.DPSExecutioner = ExpectedDPS(Base.AttackPower * ExtraAPMult, Mods, Ref.ExecutionerShare, Ref.PlayerAttackInterval);
	Out.TTKWerewolf = TimeToKill(Ref.WerewolfHealth, Out.DPSExecutioner);
	Out.TTKBoss = TimeToKill(Ref.BossHealth, Out.DPSExecutioner);

	Out.SprintUptime = SprintUptime(Base.MaxStamina + Mods.MaxStaminaBonus, Base.StaminaRegen * (1.0f + Mods.StaminaRegenPct),
		UMDPerkCatalog::EffectiveRegenDelay(Base.RegenDelay, Mods.RegenDelayReduction),
		Base.SprintDrainPerSec * FMath::Clamp(Mods.StaminaCostScale, 0.0f, 1.0f), Base.SprintRefillFraction);
	return Out;
}
