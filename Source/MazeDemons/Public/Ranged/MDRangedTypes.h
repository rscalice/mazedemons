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
};
