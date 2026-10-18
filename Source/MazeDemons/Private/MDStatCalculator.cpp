#include "MDStatCalculator.h"

#include "MDAbilitySystemComponent.h"

float UMDStatCalculator::ConditionMultiplier(int32 Durability, int32 MaxDurability, const FMDEconomyTuning& Tuning)
{
	if (MaxDurability <= 0)
	{
		return 1.0f;
	}
	const float Fraction = FMath::Clamp(static_cast<float>(Durability) / static_cast<float>(MaxDurability), 0.0f, 1.0f);
	const float FullAbove = FMath::Max(Tuning.FullStrengthAbove, KINDA_SMALL_NUMBER);
	if (Fraction >= FullAbove)
	{
		return 1.0f;
	}
	return FMath::Lerp(Tuning.ConditionFloor, 1.0f, Fraction / FullAbove);
}

float UMDStatCalculator::EffectivePieceDR(float RowDR, int32 UpgradeLevel, float CondMult, const FMDEconomyTuning& Tuning)
{
	return FMath::Max(0.0f, (RowDR + Tuning.ArmorDRPerLevel * FMath::Max(0, UpgradeLevel)) * CondMult);
}

float UMDStatCalculator::CombineDR(float BaseDefense, const TArray<float>& PiecePercents, float Cap)
{
	float RemainingDamageFraction = 1.0f - BaseDefense / 100.0f;
	for (const float Percent : PiecePercents)
	{
		RemainingDamageFraction *= 1.0f - Percent / 100.0f;
	}
	return FMath::Clamp((1.0f - RemainingDamageFraction) * 100.0f, 0.0f, FMath::Max(Cap, 0.0f));
}

float UMDStatCalculator::EffectiveAttackPower(float Base, const TMap<FName, float>& WeaponBonuses, int32 WeaponUpgradeLevel,
	float WeaponCondMult, const FMDEconomyTuning& Tuning)
{
	const float UpgradeScale = 1.0f + Tuning.WeaponAPPctPerLevel * FMath::Max(0, WeaponUpgradeLevel);
	return UMDAbilitySystemComponent::ComputeAttackPower(Base, WeaponBonuses) * UpgradeScale * WeaponCondMult;
}

float UMDStatCalculator::EffectiveWeaponAttackPower(float Base, float WeaponBonus, int32 WeaponUpgradeLevel, float WeaponCondMult,
	const FMDEconomyTuning& Tuning)
{
	TMap<FName, float> Bonuses;
	if (WeaponBonus != 0.0f)
	{
		Bonuses.Add(FName("MainHand"), WeaponBonus);
	}
	return EffectiveAttackPower(Base, Bonuses, WeaponUpgradeLevel, WeaponCondMult, Tuning);
}

int32 UMDStatCalculator::RepairCost(int32 Missing, int32 ItemValue, int32 UpgradeLevel, const FMDEconomyTuning& Tuning)
{
	if (Missing <= 0 || ItemValue <= 0)
	{
		return 0;
	}
	// double and a small epsilon so exact products (e.g. 50 * 40 * 0.01 = 20) are not bumped up by float error.
	const double Raw = static_cast<double>(Missing) * ItemValue * Tuning.RepairRate * (1.0 + Tuning.RepairUpgradeScale * FMath::Max(0, UpgradeLevel));
	return FMath::Max(0, FMath::CeilToInt(Raw - 1.0e-6));
}

bool UMDStatCalculator::GetUpgradeCost(int32 CurrentLevel, const FMDEconomyTuning& Tuning, int32& OutGold, int32& OutShards)
{
	OutGold = 0;
	OutShards = 0;
	if (CurrentLevel < 0 || CurrentLevel >= Tuning.MaxUpgradeLevel)
	{
		return false;
	}
	OutGold = Tuning.UpgradeGold.IsValidIndex(CurrentLevel) ? Tuning.UpgradeGold[CurrentLevel] : 0;
	OutShards = Tuning.UpgradeShards.IsValidIndex(CurrentLevel) ? Tuning.UpgradeShards[CurrentLevel] : 0;
	return true;
}

int32 UMDStatCalculator::DurabilityAfterHit(int32 Durability)
{
	return FMath::Max(0, Durability - 1);
}

int32 UMDStatCalculator::DurabilityAfterDeath(int32 Durability, int32 MaxDurability, const FMDEconomyTuning& Tuning)
{
	if (MaxDurability <= 0)
	{
		return Durability;
	}
	const int32 Loss = FMath::CeilToInt(static_cast<float>(MaxDurability) * Tuning.DeathDurabilityLossPct - 1.0e-4f);
	return FMath::Max(0, Durability - Loss);
}

TArray<int32> UMDStatCalculator::SplitKillGold(int32 Gold, int32 NumRecipients, int32 KillerIndex)
{
	TArray<int32> Shares;
	if (NumRecipients <= 0)
	{
		return Shares;
	}
	Shares.Init(0, NumRecipients);
	if (Gold <= 0)
	{
		return Shares;
	}
	const int32 Even = Gold / NumRecipients;
	for (int32& Share : Shares)
	{
		Share = Even;
	}
	const int32 RemainderTo = (KillerIndex >= 0 && KillerIndex < NumRecipients) ? KillerIndex : 0;
	Shares[RemainderTo] += Gold - Even * NumRecipients;
	return Shares;
}

void UMDStatCalculator::DistributeKillGold(int32 Gold, const TArray<float>& Distances, float Radius, int32 KillerIndex, TArray<int32>& OutShares)
{
	OutShares.Init(0, Distances.Num());

	TArray<int32> Recipients; // indices into Distances
	int32 Nearest = INDEX_NONE;
	for (int32 i = 0; i < Distances.Num(); ++i)
	{
		if (Distances[i] >= 0.0f && Distances[i] <= Radius)
		{
			Recipients.Add(i);
			if (Nearest == INDEX_NONE || Distances[i] < Distances[Nearest])
			{
				Nearest = i;
			}
		}
	}
	if (Recipients.IsEmpty())
	{
		return;
	}

	const int32 KillerSlot = Recipients.IndexOfByKey(KillerIndex); // INDEX_NONE if the killer is not a recipient
	const int32 RemainderSlot = KillerSlot != INDEX_NONE ? KillerSlot : Recipients.IndexOfByKey(Nearest);
	const TArray<int32> Shares = SplitKillGold(Gold, Recipients.Num(), RemainderSlot);
	for (int32 i = 0; i < Recipients.Num(); ++i)
	{
		OutShares[Recipients[i]] = Shares[i];
	}
}
