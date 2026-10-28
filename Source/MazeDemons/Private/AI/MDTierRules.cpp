#include "AI/MDTierRules.h"

#include "Math/RandomStream.h"

namespace MDTier
{
	float DepthScore(int32 ZoneIndex)
	{
		switch (ZoneIndex)
		{
		case 1: return 0.1f;
		case 2: return 0.4f;
		case 3: return 0.7f;
		case 4: return 1.0f;
		default: return 0.0f;
		}
	}

	float PartyScore(float AvgPerksSpent, float AvgUpgradeLevel)
	{
		const float Level = FMath::Clamp(AvgPerksSpent / PerksForMaxParty, 0.0f, 1.0f);
		const float Gear = FMath::Clamp(AvgUpgradeLevel / UpgradeForMaxParty, 0.0f, 1.0f);
		return Level * PartyLevelWeight + Gear * PartyGearWeight;
	}

	float SizeScore(int32 NumPlayers)
	{
		return FMath::Clamp(static_cast<float>(NumPlayers - 1) / 3.0f, 0.0f, 1.0f);
	}

	float ITValue(float Depth, float Party, float Size, float Modifier)
	{
		return DepthWeight * Depth + PartyWeight * Party + SizeWeight * Size + Modifier;
	}

	EMDIntelligenceTier TierFromValue(float Value)
	{
		int32 Tier = 0;
		for (const float Threshold : Thresholds)
		{
			if (Value >= Threshold)
			{
				++Tier;
			}
		}
		return static_cast<EMDIntelligenceTier>(Tier);
	}

	EMDIntelligenceTier ClampTier(EMDIntelligenceTier Tier, EMDIntelligenceTier Min, EMDIntelligenceTier Max)
	{
		const int32 Lo = FMath::Min(static_cast<int32>(Min), static_cast<int32>(Max));
		const int32 Hi = FMath::Max(static_cast<int32>(Min), static_cast<int32>(Max));
		return static_cast<EMDIntelligenceTier>(FMath::Clamp(static_cast<int32>(Tier), Lo, Hi));
	}

	float Jitter(int32 RunSeed, int32 PackId)
	{
		FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(RunSeed), GetTypeHash(PackId * 7919 + 13))));
		return Stream.FRandRange(-JitterAmplitude, JitterAmplitude);
	}

	FResult Compute(const FInputs& In)
	{
		FResult R;
		R.Depth = DepthScore(In.ZoneIndex);
		R.Party = PartyScore(In.AvgPerksSpent, In.AvgUpgradeLevel);
		R.Size = SizeScore(In.NumPlayers);
		R.Jitter = Jitter(In.RunSeed, In.PackId);
		R.Value = ITValue(R.Depth, R.Party, R.Size, In.Modifier) + R.Jitter;
		R.Tier = ClampTier(TierFromValue(R.Value), In.MinTier, In.MaxTier);
		return R;
	}
}
