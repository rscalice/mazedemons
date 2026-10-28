// R0 ranged weapons: shared enums, loose tag and the pure rules (no UObject state, unit tested).
#pragma once

#include "CoreMinimal.h"
#include "MDCombatTypes.h"
#include "NativeGameplayTags.h"
#include "MDRangedTypes.generated.h"

// Loose tag on the player's ASC (server) while the pawn is in ranged mode. The replicated truth is UMDRangedComponent::bRangedMode.
MAZEDEMONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_RangedMode);

// How the weapon is held. Drives the support-hand IK alpha (see FMDRangedRules::SupportHandIKAlpha).
UENUM(BlueprintType)
enum class EMDGripType : uint8
{
	OneHanded = 0 UMETA(DisplayName = "One Handed"),
	OneHandedSupportWhenAimed UMETA(DisplayName = "One Handed, Support Hand When Aimed"),
	TwoHanded UMETA(DisplayName = "Two Handed")
};

// How shots reach the target. Hitscan was dropped 2026-10-27: every gun fires an AMDProjectile, beams are server traces.
UENUM(BlueprintType)
enum class EMDRangedHitModel : uint8
{
	Projectile = 0,
	Beam
};

UENUM(BlueprintType)
enum class EMDFireMode : uint8
{
	Semi = 0,
	Auto,
	Burst,
	Bolt,
	Beam
};

// R1: what the shooter's client tells the server for one shot (Server_Fire). The server never trusts it blindly:
// UMDRangedStatics::ValidateShot checks magazine, cadence, origin and direction. Dir already includes the spread (rolled from
// SpreadSeed on the client); the seed travels so other machines can reproduce the cosmetic tracer.
USTRUCT()
struct MAZEDEMONS_API FMDShotRequest
{
	GENERATED_BODY()

	// Wraps at 65535. The server acknowledges every request (accepted or rejected) with the same id.
	UPROPERTY()
	uint16 ShotId = 0;

	// Shooter's estimate of the server clock when the trigger was pulled (AGameStateBase::GetServerWorldTimeSeconds).
	UPROPERTY()
	float ClientTime = 0.0f;

	UPROPERTY()
	FVector_NetQuantize Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal Dir = FVector::ForwardVector;

	UPROPERTY()
	int32 SpreadSeed = 0;

	// Soft lock-on target (V1 WP7); validated by the server when used, never required.
	UPROPERTY()
	TObjectPtr<AActor> AimTargetHint = nullptr;
};

// Pure rules of ranged mode, grips and stances. No world access, so the automation tests cover every branch.
struct MAZEDEMONS_API FMDRangedRules
{
	// Melee combo input only works outside ranged mode; fire / aim / reload input only inside it. Dodge is never gated.
	static bool IsMeleeInputAllowed(bool bRangedMode) { return !bRangedMode; }
	static bool IsRangedInputAllowed(bool bRangedMode) { return bRangedMode; }

	// A gun was just equipped: the player goes straight to ranged mode only when no melee weapon is equipped.
	static bool ShouldEnterRangedModeOnEquip(bool bMeleeWeaponEquipped, bool bGunJustEquipped)
	{
		return bGunJustEquipped && !bMeleeWeaponEquipped;
	}

	// Ranged mode needs a gun: with none equipped the mode ends.
	static bool ShouldLeaveRangedMode(bool bRangedMode, bool bGunEquipped) { return bRangedMode && !bGunEquipped; }

	// May the swap key change the mode to bWantRanged right now (server decides). Leaving ranged mode is always allowed
	// unless dead; entering needs a gun. A swing in progress, a stagger and the swap cooldown block both directions.
	static bool CanSwapMode(bool bWantRanged, bool bGunEquipped, bool bDead, bool bAttacking, bool bStaggered, float SecondsSinceLastSwap, float Cooldown)
	{
		if (bDead || bAttacking || bStaggered || SecondsSinceLastSwap < Cooldown)
		{
			return false;
		}
		return !bWantRanged || bGunEquipped;
	}

	static bool IsRangedStance(EMDWeaponStance Stance)
	{
		return Stance == EMDWeaponStance::Pistol || Stance == EMDWeaponStance::Rifle || Stance == EMDWeaponStance::Staff;
	}

	// The stance the anim BP reads: the held gun's stance in ranged mode (Unarmed when the data is missing), else the melee stance.
	static EMDWeaponStance ResolveStance(bool bRangedMode, EMDWeaponStance MeleeStance, EMDWeaponStance RangedStance)
	{
		if (!bRangedMode)
		{
			return MeleeStance;
		}
		return IsRangedStance(RangedStance) ? RangedStance : EMDWeaponStance::Unarmed;
	}

	// Support-hand (left) IK weight 0..1. Two-handers always hold with both hands, pistols only while aimed, one-handers never.
	// DisableCurve is the DisableLHandIK anim curve (reload and swap montages set it to 1): it scales the alpha down to 0.
	static float SupportHandIKAlpha(EMDGripType Grip, bool bAimed, float DisableCurve, bool bRangedMode, bool bHasWeapon)
	{
		if (!bRangedMode || !bHasWeapon)
		{
			return 0.0f;
		}
		float Base = 0.0f;
		switch (Grip)
		{
		case EMDGripType::TwoHanded: Base = 1.0f; break;
		case EMDGripType::OneHandedSupportWhenAimed: Base = bAimed ? 1.0f : 0.0f; break;
		default: Base = 0.0f; break;
		}
		return Base * (1.0f - FMath::Clamp(DisableCurve, 0.0f, 1.0f));
	}

	// True when the grip needs a left-hand target (a Grip_L socket or a per-weapon offset).
	static bool GripNeedsSupportHand(EMDGripType Grip) { return Grip != EMDGripType::OneHanded; }

	// Support-hand reach rule. ArmLength = upper arm + forearm (cm, measured on the posed skeleton so it follows mesh scale);
	// DistanceToTarget = shoulder (upperarm_l) to the support grip. The two-bone IK never stretches the arm, so a target beyond
	// reach would leave the arm ramrod straight and the hand short of the gun. Instead the weight is 1 while the target is within
	// FullFraction of the arm, then fades linearly to 0 at ZeroFraction (1.0 = exactly the arm length): the hand falls back to the
	// animation pose and never hangs on a straight arm. Multiply with SupportHandIKAlpha.
	static float SupportHandReachAlpha(float DistanceToTarget, float ArmLength, float FullFraction = 0.85f, float ZeroFraction = 1.0f)
	{
		if (ArmLength <= KINDA_SMALL_NUMBER || DistanceToTarget < 0.0f || !FMath::IsFinite(DistanceToTarget))
		{
			return 0.0f;
		}
		const float Full = ArmLength * FullFraction;
		const float Zero = ArmLength * FMath::Max(ZeroFraction, FullFraction + KINDA_SMALL_NUMBER);
		if (DistanceToTarget <= Full)
		{
			return 1.0f;
		}
		if (DistanceToTarget >= Zero)
		{
			return 0.0f;
		}
		return (Zero - DistanceToTarget) / (Zero - Full);
	}
};
