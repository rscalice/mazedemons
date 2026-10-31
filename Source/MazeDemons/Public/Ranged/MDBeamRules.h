// R5 beams: the pure rules (heat, overheat lock, charge, tick damage, hit-once-per-tick, damage number aggregation). No world access, so the
// automation tests cover every branch. UMDBeamComponent runs these on the server and predicts them on the owning client.
#pragma once

#include "CoreMinimal.h"
#include "MDCombatFeedbackComponent.h"
#include "Ranged/MDRangedMath.h"

class AActor;

// Heat of one beam weapon. LockRemaining > 0 means overheated (the weapon is locked, venting).
struct FMDHeatState
{
	float Heat = 0.0f;
	float LockRemaining = 0.0f;

	bool IsOverheated() const { return LockRemaining > 0.0f; }
};

struct FMDHeatParams
{
	float HeatPerSecond = 30.0f; // while firing (already scaled by upgrades)
	float VentPerSecond = 8.0f;  // trigger up
	float MaxHeat = 100.0f;
	float OverheatLock = 2.5f;   // seconds locked after hitting MaxHeat
};

// Charge of a staff beam (V2): drains while firing, regenerates while the trigger is up. Dry = ran out, locked until it recovers.
struct FMDChargeState
{
	float Charge = 100.0f;
	bool bDry = false;
};

struct FMDChargeParams
{
	float MaxCharge = 100.0f;
	float DrainPerSecond = 25.0f;
	float RegenPerSecond = 12.0f;
	// A dry staff fires again once this fraction of the pool is back.
	float RecoverFraction = 0.15f;
};

// Why a beam cannot fire right now.
enum class EMDBeamBlock : uint8
{
	None = 0,
	NotRangedMode,
	NoBeamWeapon,
	Dead,
	Staggered,
	Reloading,
	Overheated,
	NoCharge
};

struct MAZEDEMONS_API FMDBeamRules
{
	// ---- Duskray numbers (ranged-design section 9, re-based on BALANCE.md) ----
	static constexpr float DefaultHeatPerSecond = 30.0f;
	static constexpr float DefaultVentPerSecond = 8.0f;
	static constexpr float DefaultMaxHeat = 100.0f;
	static constexpr float DefaultOverheatLock = 2.5f;
	// Each upgrade level of the beam weapon cuts the heat it builds by 8 %, never below FloorHeatMultiplier.
	static constexpr float UpgradeHeatReduction = 0.08f;
	static constexpr float FloorHeatMultiplier = 0.2f;
	// Heat points one owner-side heat sample may differ from the server's before the client snaps to the server.
	static constexpr float ReconcileTolerance = 10.0f;
	// Damage numbers of a beam roll into one number per victim per this many seconds.
	static constexpr float NumberWindowSeconds = 0.5f;

	// Heat built per second after upgrades.
	static float UpgradedHeatPerSecond(float BaseHeatPerSecond, int32 UpgradeLevel)
	{
		const float Mult = FMath::Max(1.0f - UpgradeHeatReduction * static_cast<float>(FMath::Max(UpgradeLevel, 0)), FloorHeatMultiplier);
		return FMath::Max(BaseHeatPerSecond, 0.0f) * Mult;
	}

	// Advances the heat by DeltaSeconds. Firing builds heat; reaching MaxHeat overheats (locked for OverheatLock, the heat vents
	// linearly to 0 over the lock so the HUD radial and the meter agree). A locked weapon ignores the trigger. Idle heat vents.
	static FMDHeatState StepHeat(FMDHeatState State, bool bFiring, const FMDHeatParams& Params, float DeltaSeconds)
	{
		const float Dt = FMath::Max(DeltaSeconds, 0.0f);
		const float Max = FMath::Max(Params.MaxHeat, 1.0f);
		if (State.IsOverheated())
		{
			const float Lock = FMath::Max(Params.OverheatLock, KINDA_SMALL_NUMBER);
			State.LockRemaining -= Dt;
			if (State.LockRemaining <= 0.0f)
			{
				State.LockRemaining = 0.0f;
				State.Heat = 0.0f;
			}
			else
			{
				State.Heat = FMath::Max(State.Heat - (Max / Lock) * Dt, 0.0f);
			}
			return State;
		}
		if (bFiring)
		{
			State.Heat += FMath::Max(Params.HeatPerSecond, 0.0f) * Dt;
			if (State.Heat >= Max)
			{
				State.Heat = Max;
				State.LockRemaining = FMath::Max(Params.OverheatLock, KINDA_SMALL_NUMBER);
			}
		}
		else
		{
			State.Heat = FMath::Max(State.Heat - FMath::Max(Params.VentPerSecond, 0.0f) * Dt, 0.0f);
		}
		return State;
	}

	// Seconds of continuous fire from cold to overheat (the "about 3.3 s" of the design).
	static float SecondsToOverheat(const FMDHeatParams& Params)
	{
		return Params.HeatPerSecond > KINDA_SMALL_NUMBER ? Params.MaxHeat / Params.HeatPerSecond : 0.0f;
	}

	// 0..1 of the lock still to run (1 right after overheating, 0 when free). The HUD radial.
	static float OverheatLockFraction(const FMDHeatState& State, float OverheatLock)
	{
		return State.IsOverheated() && OverheatLock > KINDA_SMALL_NUMBER ? FMath::Clamp(State.LockRemaining / OverheatLock, 0.0f, 1.0f) : 0.0f;
	}

	// The owner's predicted heat after a server sample arrived: a small difference is smoothed (the server's value is older), a big one snaps.
	static float ReconcileHeat(float Predicted, float ServerValue, float Tolerance = ReconcileTolerance, float Blend = 0.1f)
	{
		if (FMath::Abs(Predicted - ServerValue) > Tolerance)
		{
			return ServerValue;
		}
		return FMath::Lerp(Predicted, ServerValue, FMath::Clamp(Blend, 0.0f, 1.0f));
	}

	// ---- Charge (staff) ----
	static FMDChargeState StepCharge(FMDChargeState State, bool bFiring, const FMDChargeParams& Params, float DeltaSeconds)
	{
		const float Dt = FMath::Max(DeltaSeconds, 0.0f);
		const float Max = FMath::Max(Params.MaxCharge, 1.0f);
		if (bFiring && !State.bDry)
		{
			State.Charge = FMath::Max(State.Charge - FMath::Max(Params.DrainPerSecond, 0.0f) * Dt, 0.0f);
			if (State.Charge <= 0.0f)
			{
				State.bDry = true;
			}
		}
		else
		{
			State.Charge = FMath::Min(State.Charge + FMath::Max(Params.RegenPerSecond, 0.0f) * Dt, Max);
			if (State.bDry && State.Charge >= Max * FMath::Clamp(Params.RecoverFraction, 0.0f, 1.0f))
			{
				State.bDry = false;
			}
		}
		return State;
	}

	// ---- Can the beam fire? First failing gate wins. ----
	static EMDBeamBlock CanFire(bool bRangedMode, bool bBeamWeapon, bool bDead, bool bStaggered, bool bReloading, bool bOverheated, bool bNoCharge)
	{
		if (!bRangedMode) { return EMDBeamBlock::NotRangedMode; }
		if (!bBeamWeapon) { return EMDBeamBlock::NoBeamWeapon; }
		if (bDead) { return EMDBeamBlock::Dead; }
		if (bStaggered) { return EMDBeamBlock::Staggered; }
		if (bReloading) { return EMDBeamBlock::Reloading; }
		if (bOverheated) { return EMDBeamBlock::Overheated; }
		if (bNoCharge) { return EMDBeamBlock::NoCharge; }
		return EMDBeamBlock::None;
	}

	// ---- Damage ----
	// Damage of one tick before perks and condition: DPS / Hz times the range falloff (100 % to FalloffStart, FalloffMinMult from FalloffEnd).
	static float TickDamage(float DamagePerSecond, float TickHz, float Distance, float FalloffStart, float FalloffEnd, float FalloffMinMult)
	{
		if (TickHz <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		return FMath::Max(DamagePerSecond, 0.0f) / TickHz * FMDRangedMath::FalloffMultiplier(Distance, FalloffStart, FalloffEnd, FalloffMinMult);
	}

	// Durability a beam costs for one tick that landed (BeamDurabilityPerSecond / Hz). Misses cost nothing.
	static float WearPerLandedTick(float DurabilityPerSecond, float TickHz)
	{
		return TickHz > KINDA_SMALL_NUMBER ? FMath::Max(DurabilityPerSecond, 0.0f) / TickHz : 0.0f;
	}

	// Quantises a beam end the way the net property does (whole centimetres), so tests and clients agree with the wire.
	static FVector QuantizeBeamEnd(const FVector& End)
	{
		return FVector(FMath::RoundToDouble(End.X), FMath::RoundToDouble(End.Y), FMath::RoundToDouble(End.Z));
	}
};

// A victim is hurt at most once per beam tick, however many of its components the sweep touches.
struct FMDBeamHitLog
{
	TArray<TWeakObjectPtr<AActor>> Hit;

	// True the first time Victim is registered this tick; false for a repeat.
	bool TryRegister(AActor* Victim)
	{
		if (!Victim)
		{
			return false;
		}
		for (const TWeakObjectPtr<AActor>& Existing : Hit)
		{
			if (Existing.Get() == Victim)
			{
				return false;
			}
		}
		Hit.Add(Victim);
		return true;
	}

	void Reset() { Hit.Reset(); }
};

// Beam damage numbers: the ticks of one victim roll into one number per FMDBeamRules::NumberWindowSeconds (a kill flushes at once).
struct FMDBeamNumberBucket
{
	FMDHitConfirm Confirm;
	double Start = 0.0;
};

struct MAZEDEMONS_API FMDBeamNumbers
{
	// Adds one tick's confirm: summed into the open bucket of the same victim and damage type, else a new bucket opens at Now.
	static void Add(TArray<FMDBeamNumberBucket>& Buckets, const FMDHitConfirm& Hit, double Now);

	// Moves every bucket that is due (window over, a kill, or bFlushAll) to Out, oldest first.
	static void Collect(TArray<FMDBeamNumberBucket>& Buckets, double Now, bool bFlushAll, TArray<FMDHitConfirm>& Out);
};
