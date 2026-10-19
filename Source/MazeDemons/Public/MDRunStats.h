#pragma once

#include "CoreMinimal.h"
#include "MDRunStats.generated.h"

// Per-player running totals for the Run tab. Lives on AMDPlayerState, replicated to the owner only; the server is the only writer.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPlayerRunStats
{
	GENERATED_BODY()

	// Health the player's attacks actually removed (after mitigation), enemies and friendly fire alike, never self-damage.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float DamageDealt = 0.0f;

	// Health this player actually lost (after Defense and perks).
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float DamageTaken = 0.0f;

	// Non-player targets whose death this player's hit caused.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 Kills = 0;

	// Gold received from kill-gold shares.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 GoldEarned = 0;

	static constexpr float MaxAmount = 1.0e9f;
	static constexpr int32 MaxCount = 999999999;

	// Pure. Current + Delta clamped to [0, MaxAmount]; a negative, NaN or infinite Delta adds nothing.
	static float AddAmount(float Current, float Delta)
	{
		const float Base = FMath::Clamp(Current, 0.0f, MaxAmount);
		if (!FMath::IsFinite(Delta) || Delta <= 0.0f)
		{
			return Base;
		}
		return FMath::Min(Base + Delta, MaxAmount);
	}

	// Pure. Current + Delta clamped to [0, MaxCount] without overflow; a negative Delta adds nothing.
	static int32 AddCount(int32 Current, int32 Delta)
	{
		const int64 Base = FMath::Clamp<int64>(Current, 0, MaxCount);
		if (Delta <= 0)
		{
			return static_cast<int32>(Base);
		}
		return static_cast<int32>(FMath::Min<int64>(Base + Delta, MaxCount));
	}

	bool operator==(const FMDPlayerRunStats& Other) const
	{
		return DamageDealt == Other.DamageDealt && DamageTaken == Other.DamageTaken && Kills == Other.Kills && GoldEarned == Other.GoldEarned;
	}
};
