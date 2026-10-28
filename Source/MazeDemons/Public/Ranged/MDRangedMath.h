// R1 ranged weapons: the pure maths and validation rules (no world, no UObject state; every branch is unit tested).
#pragma once

#include "CoreMinimal.h"
#include "MDCombatTypes.h"

// Why the server refused a shot (ValidateShot). None = accepted.
enum class EMDShotReject : uint8
{
	None = 0,
	State,      // dead, staggered, not in ranged mode, no gun, reloading, overheated
	NoAmmo,     // magazine empty
	Cadence,    // faster than RoundsPerSecond x 1.15
	Time,       // the shot claims to come from the future
	Origin,     // too far from the muzzle the server sees
	Direction   // too far from where the server thinks the shooter aims
};

// Everything ValidateShot looks at. Plain data so tests build it directly.
struct FMDShotValidationInput
{
	// State gates (any false/true combination that forbids firing rejects with State).
	bool bRangedMode = true;
	bool bGunEquipped = true;
	bool bDead = false;
	bool bStaggered = false;
	bool bBusy = false; // reloading / overheated (R3, R5)

	int32 MagRounds = 8;
	float RoundsPerSecond = 3.0f;

	// Seconds between this request's ClientTime and the previous accepted request's ClientTime (large for the first shot).
	float SecondsSinceLastShot = 1000.0f;
	// This request's ClientTime minus the server clock now (positive = claims to be ahead of the server).
	float ClientTimeAheadOfServer = 0.0f;

	FVector Origin = FVector::ZeroVector;
	FVector ServerMuzzle = FVector::ZeroVector;

	FVector Dir = FVector::ForwardVector;
	FVector ServerAimDir = FVector::ForwardVector;

	// Current spread half-angle (degrees) the server computes for this shooter (hip / aimed / moving / bloom).
	float SpreadDeg = 3.0f;
	// Extra allowance for the server's older view of the shooter's aim (camera turning during half the round trip).
	float AimLagSlackDeg = 0.0f;
};

struct MAZEDEMONS_API FMDRangedMath
{
	// ---- Validation constants (ranged-design.md section 6) ----
	static constexpr float CadenceTolerance = 1.15f;     // up to RPS x 1.15 passes
	static constexpr float MaxOriginDistance = 150.0f;   // cm from the muzzle the server sees
	static constexpr float DirSpreadFactor = 2.0f;       // direction within Spread x 2 of the server's aim estimate
	static constexpr float MinDirToleranceDeg = 2.0f;    // floor so a perfectly accurate gun still tolerates network rounding
	static constexpr float MaxFutureSeconds = 0.25f;     // ClientTime may lead the server clock by this much
	static constexpr float MaxSpawnFastForward = 0.1f;   // 100 ms cap on the server spawn fast-forward
	static constexpr float FriendlyFireRangedScale = 0.5f; // Q6: bullets / beams hurt teammate players for half (the toggle still turns it off)

	static float AngleBetweenDeg(const FVector& A, const FVector& B)
	{
		const FVector NA = A.GetSafeNormal();
		const FVector NB = B.GetSafeNormal();
		if (NA.IsNearlyZero() || NB.IsNearlyZero())
		{
			return 180.0f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(NA, NB)), -1.0f, 1.0f)));
	}

	static EMDShotReject ValidateShot(const FMDShotValidationInput& In)
	{
		if (!In.bRangedMode || !In.bGunEquipped || In.bDead || In.bStaggered || In.bBusy)
		{
			return EMDShotReject::State;
		}
		if (In.MagRounds <= 0)
		{
			return EMDShotReject::NoAmmo;
		}
		const float MinInterval = In.RoundsPerSecond > KINDA_SMALL_NUMBER ? 1.0f / (In.RoundsPerSecond * CadenceTolerance) : 0.0f;
		if (In.SecondsSinceLastShot < MinInterval - 0.0005f)
		{
			return EMDShotReject::Cadence;
		}
		if (In.ClientTimeAheadOfServer > MaxFutureSeconds)
		{
			return EMDShotReject::Time;
		}
		if (FVector::Dist(In.Origin, In.ServerMuzzle) > MaxOriginDistance)
		{
			return EMDShotReject::Origin;
		}
		const float Allowed = FMath::Max(In.SpreadDeg * DirSpreadFactor, MinDirToleranceDeg) + FMath::Max(In.AimLagSlackDeg, 0.0f);
		if (AngleBetweenDeg(In.Dir, In.ServerAimDir) > Allowed)
		{
			return EMDShotReject::Direction;
		}
		return EMDShotReject::None;
	}

	// ---- Spread and bloom (degrees, half-angle of the cone) ----
	// Base = SpreadHip or SpreadAimed; MovingMult multiplies it while the shooter moves; bloom is added on top.
	static float ComputeSpread(float Base, bool bMoving, float MovingMult, float Bloom)
	{
		return FMath::Max(Base, 0.0f) * (bMoving ? FMath::Max(MovingMult, 1.0f) : 1.0f) + FMath::Max(Bloom, 0.0f);
	}

	static float AddBloom(float Bloom, float PerShot, float MaxBloom)
	{
		return FMath::Clamp(Bloom + FMath::Max(PerShot, 0.0f), 0.0f, FMath::Max(MaxBloom, 0.0f));
	}

	static float DecayBloom(float Bloom, float DecayPerSecond, float DeltaSeconds)
	{
		return FMath::Max(Bloom - FMath::Max(DecayPerSecond, 0.0f) * FMath::Max(DeltaSeconds, 0.0f), 0.0f);
	}

	// A direction inside the cone of HalfAngleDeg around Dir; the same seed always gives the same direction (client and remote
	// machines roll it identically).
	static FVector SpreadDirection(const FVector& Dir, float HalfAngleDeg, int32 Seed)
	{
		if (HalfAngleDeg <= KINDA_SMALL_NUMBER)
		{
			return Dir.GetSafeNormal();
		}
		const FRandomStream Stream(Seed);
		return Stream.VRandCone(Dir.GetSafeNormal(), FMath::DegreesToRadians(HalfAngleDeg));
	}

	// ---- Damage falloff over distance (cm): full damage up to Start, MinMult from End on, linear between ----
	static float FalloffMultiplier(float Distance, float Start, float End, float MinMult)
	{
		const float Min = FMath::Clamp(MinMult, 0.0f, 1.0f);
		if (Distance <= Start)
		{
			return 1.0f;
		}
		if (End <= Start || Distance >= End)
		{
			return Min;
		}
		return FMath::Lerp(1.0f, Min, (Distance - Start) / (End - Start));
	}

	// ---- Recoil ----
	// Camera kick of one shot: x = pitch up, y = yaw (random sign and size per shot, deterministic in Seed).
	static FVector2D RecoilKick(float Pitch, float Yaw, int32 Seed)
	{
		const FRandomStream Stream(Seed);
		return FVector2D(Pitch, Yaw * Stream.FRandRange(-1.0f, 1.0f));
	}

	// How much of an un-recovered kick is left after DeltaSeconds. Exponential: about 5% remains after RecoverySeconds.
	static float RecoilRemaining(float Remaining, float RecoverySeconds, float DeltaSeconds)
	{
		if (RecoverySeconds <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		const float Left = Remaining * FMath::Exp(-3.0f * FMath::Max(DeltaSeconds, 0.0f) / RecoverySeconds);
		return FMath::Abs(Left) < 0.001f ? 0.0f : Left;
	}

	// ---- Spawn compensation: the server fast-forwards a fresh projectile by min(RTT / 2, 100 ms) ----
	static float ComputeFastForward(float RoundTripSeconds, float CapSeconds = MaxSpawnFastForward)
	{
		return FMath::Clamp(RoundTripSeconds * 0.5f, 0.0f, FMath::Max(CapSeconds, 0.0f));
	}

	// Position after T seconds of flight (constant velocity plus gravity in cm/s^2 pulling down).
	static FVector ProjectilePosition(const FVector& Origin, const FVector& Velocity, float Gravity, float T)
	{
		return Origin + Velocity * T + FVector(0.0, 0.0, -0.5 * Gravity * T * T);
	}

	static FVector ProjectileVelocity(const FVector& Velocity, float Gravity, float T)
	{
		return Velocity + FVector(0.0, 0.0, -Gravity * T);
	}

	// ---- Hits ----
	// A headshot: the bone is a head bone, or (for capsule hits without a bone) the impact is in the top HeadHeight cm of the capsule.
	static bool IsHeadshot(FName BoneName, double ImpactZ, double ActorZ, double CapsuleHalfHeight, double HeadHeight = 30.0)
	{
		if (!BoneName.IsNone() && BoneName.ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return CapsuleHalfHeight > 0.0 && ImpactZ >= ActorZ + CapsuleHalfHeight - HeadHeight;
	}

	static EMDHitReaction ClampReaction(EMDHitReaction Reaction, EMDHitReaction Cap)
	{
		return static_cast<uint8>(Reaction) > static_cast<uint8>(Cap) ? Cap : Reaction;
	}

	// ---- Durability: guns wear 0.5 per landed hit; the accumulator pays out whole points ----
	static int32 AccumulateWear(float& Accumulator, float PerHit)
	{
		Accumulator += FMath::Max(PerHit, 0.0f);
		const int32 Points = FMath::FloorToInt(Accumulator + KINDA_SMALL_NUMBER);
		Accumulator = FMath::Max(Accumulator - static_cast<float>(Points), 0.0f);
		return Points;
	}

	// ---- Shot ids (uint16, wrap-safe) ----
	// True when Id is at or before Ack in wrapping order.
	static bool IsShotAcked(uint16 Id, uint16 Ack)
	{
		return static_cast<int16>(static_cast<uint16>(Ack - Id)) >= 0;
	}
};
