// R3 ammo, reload and economy: the pure rules (no world, no UObject state; every branch is unit tested).
#pragma once

#include "CoreMinimal.h"

// Why a reload request was refused. None = accepted.
enum class EMDReloadReject : uint8
{
	None = 0,
	NotRangedMode,
	NoGun,
	NoAmmoType, // beams and anything with no ammo row or a zero magazine
	AlreadyReloading,
	Dead,
	Staggered,
	MagFull,
	NoReserve
};

struct MAZEDEMONS_API FMDAmmoRules
{
	// Rounds a reload moves from the bag stack into the magazine: min(MagSize - Mag, Reserve), never negative.
	static int32 ReloadAmount(int32 Mag, int32 MagSize, int32 Reserve)
	{
		return FMath::Max(0, FMath::Min(MagSize - Mag, Reserve));
	}

	static EMDReloadReject CanReload(bool bRangedMode, bool bGun, bool bHasAmmoType, bool bReloading, bool bDead, bool bStaggered,
		int32 Mag, int32 MagSize, int32 Reserve)
	{
		if (!bRangedMode) { return EMDReloadReject::NotRangedMode; }
		if (!bGun) { return EMDReloadReject::NoGun; }
		if (!bHasAmmoType || MagSize <= 0) { return EMDReloadReject::NoAmmoType; }
		if (bReloading) { return EMDReloadReject::AlreadyReloading; }
		if (bDead) { return EMDReloadReject::Dead; }
		if (bStaggered) { return EMDReloadReject::Staggered; }
		if (Mag >= MagSize) { return EMDReloadReject::MagFull; }
		if (Reserve <= 0) { return EMDReloadReject::NoReserve; }
		return EMDReloadReject::None;
	}

	// Drop chance with the pity bonus: Base + Bonus per full PerStep shots fired without an ammo drop, clamped to 0..1.
	static float PityDropChance(float Base, int32 ShotsSinceDrop, int32 PerStep = 20, float Bonus = 0.10f)
	{
		const int32 Steps = PerStep > 0 ? FMath::Max(ShotsSinceDrop, 0) / PerStep : 0;
		return FMath::Clamp(Base + Bonus * static_cast<float>(Steps), 0.0f, 1.0f);
	}

	// Roll01 in [0,1): chance 0 never drops, chance 1 always drops.
	static bool RollDrops(float Chance, float Roll01) { return Roll01 < Chance; }

	// Rounds a purchase adds: a bundle, but never past the cap (one full reserve stack).
	static int32 PurchaseQuantity(int32 Have, int32 Cap, int32 Bundle)
	{
		return FMath::Clamp(Cap - Have, 0, FMath::Max(Bundle, 0));
	}

	// Gold for Qty rounds at PricePerBundle: pro rata, rounded up, at least 1 for any purchase.
	static int32 PurchaseCost(int32 Qty, int32 Bundle, int32 PricePerBundle)
	{
		if (Qty <= 0 || Bundle <= 0)
		{
			return 0;
		}
		const int32 Cost = FMath::DivideAndRoundUp(Qty * FMath::Max(PricePerBundle, 0), Bundle);
		return FMath::Max(Cost, 1);
	}

	// HUD: the magazine number turns red at or below 20% of its size.
	static bool IsMagLow(int32 Mag, int32 MagSize) { return MagSize > 0 && Mag * 5 <= MagSize; }
};
