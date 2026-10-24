#pragma once

#include "CoreMinimal.h"
#include "MDCombatTypes.h"

// Pure helpers for native impact playback (no world access, unit tested).
namespace MDImpactMath
{
	// Final VFX scale: entry scale, times 0.6 when the player chose Reduced effects.
	inline float VFXScaleForLevel(float EntryScale, EMDVFXLevel Level)
	{
		return FMath::Max(0.0f, EntryScale) * (Level == EMDVFXLevel::Reduced ? 0.6f : 1.0f);
	}

	// Decals are skipped on Reduced.
	inline bool AllowDecals(EMDVFXLevel Level)
	{
		return Level != EMDVFXLevel::Reduced;
	}

	// How many of the oldest decals must go before one more is added so the total stays within Max.
	inline int32 DecalsToEvict(int32 Current, int32 Max)
	{
		return Max <= 0 ? Current : FMath::Max(0, Current - Max + 1);
	}

	// Direction the effect points: the hit normal when usable, else attacker -> victim, else up.
	inline FVector ImpactDirection(const FVector& Normal, const FVector& AttackerLoc, const FVector& VictimLoc)
	{
		if (!Normal.IsNearlyZero())
		{
			return Normal.GetSafeNormal();
		}
		const FVector Dir = (VictimLoc - AttackerLoc).GetSafeNormal();
		return Dir.IsNearlyZero() ? FVector::UpVector : Dir;
	}

	// Cooldown gate for the hurt sound.
	inline bool CooldownElapsed(double Now, double Last, double Cooldown)
	{
		return Now - Last >= Cooldown;
	}
}
