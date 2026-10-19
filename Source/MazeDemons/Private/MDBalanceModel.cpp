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

float UMDBalanceModel::ExpectedDPS(float AttackPower, const FMDPerkModifiers& Mods, float ExecutionerShare, float AttackInterval)
{
	const float Interval = FMath::Max(AttackInterval, KINDA_SMALL_NUMBER);
	const float CritFactor = 1.0f + Mods.CritChance * FMath::Max(Mods.CritDamage - 1.0f, 0.0f);
	const float ExecFactor = 1.0f + Mods.ExecutionerPct * FMath::Clamp(ExecutionerShare, 0.0f, 1.0f);
	return AttackPower * (1.0f + Mods.AttackPowerPct) * CritFactor * ExecFactor / Interval;
}

float UMDBalanceModel::SprintUptime(float MaxStamina, float RegenPerSec, float RegenDelay, float DrainPerSec, float RecoverFraction)
{
	if (MaxStamina <= 0.0f || DrainPerSec <= 0.0f || RegenPerSec <= 0.0f)
	{
		return 0.0f;
	}
	const float Sprint = MaxStamina / DrainPerSec;
	const float Recover = FMath::Max(RegenDelay, 0.0f) + FMath::Clamp(RecoverFraction, 0.0f, 1.0f) * MaxStamina / RegenPerSec;
	return Sprint / (Sprint + Recover);
}

float UMDBalanceModel::TimeToKill(float TargetHealth, float DPS)
{
	return DPS > 0.0f ? TargetHealth / DPS : MAX_FLT;
}

FMDBuildMetrics UMDBalanceModel::Evaluate(const FMDPerkModifiers& Mods, const FMDBalanceBase& Base, const FMDBalanceReference& Ref,
	const FMDPerkTuning& Tuning, float ExtraDRPercent, float ExtraAPMult)
{
	FMDBuildMetrics Out;

	TArray<float> Pieces;
	Pieces.Add(Mods.DamageReductionPP);
	Pieces.Add(ExtraDRPercent);
	const float DR = UMDStatCalculator::CombineDR(Base.BaseDefense, Pieces, Base.DefenseCap);
	const float MaxHealth = Base.MaxHealth + Mods.MaxHealthBonus;
	Out.EffectiveHPWerewolf = EffectiveHP(MaxHealth, DR, Ref.WerewolfHit, Mods.FlatDamageReduction, Mods.bBulwark, Tuning);
	Out.EffectiveHPBoss = EffectiveHP(MaxHealth, DR, Ref.BossHit, Mods.FlatDamageReduction, Mods.bBulwark, Tuning);

	Out.DPS = ExpectedDPS(Base.AttackPower * ExtraAPMult, Mods, Ref.ExecutionerShare, Ref.PlayerAttackInterval);
	Out.TTKWerewolf = TimeToKill(Ref.WerewolfHealth, Out.DPS);
	Out.TTKBoss = TimeToKill(Ref.BossHealth, Out.DPS);

	Out.SprintUptime = SprintUptime(Base.MaxStamina + Mods.MaxStaminaBonus, Base.StaminaRegen * (1.0f + Mods.StaminaRegenPct),
		UMDPerkCatalog::EffectiveRegenDelay(Base.RegenDelay, Mods.RegenDelayReduction),
		Base.SprintDrainPerSec * FMath::Clamp(Mods.StaminaCostScale, 0.0f, 1.0f), Base.ExhaustedRecoverFraction);
	return Out;
}
