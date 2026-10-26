#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "MDCombatTypes.generated.h"

class AActor;
class UAnimMontage;
class UPhysicalMaterial;
class USoundBase;

// What kind of swing a step is.
UENUM(BlueprintType)
enum class EMDAttackKind : uint8
{
	Light = 0,
	Heavy,
	Charged,
	DodgeAttack
};

// Which weapon stance the character is in. Drives motion-matching idles / locomotion (ABP_SandboxCharacter reads it, a Chooser
// Table enum column is bound to it). Values are stable: Chooser columns store them. Crossbow is reserved (no crossbow item yet).
UENUM(BlueprintType)
enum class EMDWeaponStance : uint8
{
	Unarmed = 0 UMETA(DisplayName = "Unarmed"),
	Greatsword UMETA(DisplayName = "Greatsword"),
	DualSword UMETA(DisplayName = "Dual Sword"),
	OneHanded UMETA(DisplayName = "One Handed"),
	Crossbow UMETA(DisplayName = "Crossbow")
};

UENUM(BlueprintType)
enum class EMDHitDirection : uint8
{
	Front = 0,
	Back,
	Left,
	Right
};

// Flinch = additive upper body, no interrupt. Stagger = full body, interrupts, stuns. Knockback = stagger + push. Knockdown is
// reserved for V2 boss slams (rules exist, no animation in C1). Order matters: higher = stronger.
UENUM(BlueprintType)
enum class EMDHitReaction : uint8
{
	None = 0,
	Flinch,
	Stagger,
	Knockback,
	Knockdown
};

// Replaces the BP enum E_InputBuffer.
UENUM(BlueprintType)
enum class EMDComboInput : uint8
{
	None = 0,
	Light,
	Heavy,
	Dodge
};

// Which hand swings a combo step (dual wield). Main = 0 so every existing step stays a main-hand swing.
UENUM(BlueprintType)
enum class EMDWeaponHand : uint8
{
	Main = 0,
	Off,
	Both
};

// Per-notify override of which weapon(s) a UMDANS_WeaponTrace window traces. Auto = decide from the active combo step and the
// loadout (UMDCombatStatics::ResolveTraceHand). Main / Off / Both force it (e.g. one dual montage swinging left then right).
UENUM(BlueprintType)
enum class EMDTraceHandOverride : uint8
{
	Auto = 0,
	Main,
	Off,
	Both
};

// What the combo state machine wants done next (output of UMDCombatRules::Advance).
UENUM(BlueprintType)
enum class EMDComboAction : uint8
{
	None = 0,
	PlayLight,
	PlayHeavy,
	PlayCharged,
	PlayDodgeAttack,
	Dodge,
	Reset
};

UENUM(BlueprintType)
enum class EMDDodgeDirection : uint8
{
	Forward = 0,
	Backward,
	Left,
	Right
};

// Options: whose damage numbers are shown.
UENUM(BlueprintType)
enum class EMDDamageNumberMode : uint8
{
	Off = 0,
	Own,
	All
};

// Options: impact VFX / flash intensity.
UENUM(BlueprintType)
enum class EMDVFXLevel : uint8
{
	Full = 0,
	Reduced
};

// One swing of a combo (a light chain step, the heavy, the charged release or the dodge attack).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDAttackStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// Montage section to jump to; None plays from the start.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	FName Section = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0.1"))
	float PlayRate = 1.0f;

	// Which weapon(s) trace during this step. Off / Both are only valid in a dual-wield set (UMDComboSet::bDualWield).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	EMDWeaponHand Hand = EMDWeaponHand::Main;

	// Multiplier on the attacker's AttackPower.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0"))
	float DamageMult = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0"))
	float PoiseDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	EMDHitReaction Reaction = EMDHitReaction::Flinch;

	// Horizontal push speed (cm/s); 0 = none.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0"))
	float KnockbackStrength = 0.0f;

	// Hit-stop length at 60 fps (clamped to 120 ms at runtime).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0"))
	int32 HitStopFrames = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0"))
	float ShakeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	FGameplayTag ImpactTag;

	// Swing whoosh played when the step starts (every machine). Empty = silent; fill from the "General Whoosh SFX" pack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	TObjectPtr<USoundBase> SwingSound = nullptr;

	// Data fallback for the combo window (fraction of the montage section, 0..1). Montage notify states win when present.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0", ClampMax = "1"))
	float ComboWindowStart = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0", ClampMax = "1"))
	float ComboWindowEnd = 0.9f;

	// Dodge may cancel the swing from here on (fraction, 0..1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0", ClampMax = "1"))
	float CancelWindowStart = 0.6f;

	// Unused (kept so saved data still loads). Root motion is now always on; use RootMotionScale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	bool bRootMotion = false;

	// Multiplier on the montage's root-motion translation when there is no warp target (0 = swing in place, 1 = as authored).
	// With a warp target the scale is 1 and the motion is skew-warped onto the target instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Warp", meta = (ClampMin = "0", ClampMax = "2"))
	float RootMotionScale = 1.0f;

	// Max distance (cm) to an enemy this swing may warp toward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Warp", meta = (ClampMin = "0"))
	float WarpRange = 500.0f;

	// The warp stops this far (cm) in front of the target, added to the target's capsule radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Warp", meta = (ClampMin = "0"))
	float WarpStopDistance = 90.0f;

	// Leave a ghost trail while this step plays (fast attacks).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	bool bAfterImage = false;

	// How many times one swing may hit the same target (1 = the usual one hit per target per swing).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "1"))
	int32 MaxHitsPerTarget = 1;
};

// Server only. One landed hit, built by UMDCombatStatics::ApplyMeleeHit and passed to ApplyDamage and the poise component.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDHitContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<AActor> Causer = nullptr;

	// The attacker's AttackPower before the step multiplier (ApplyDamage multiplies by Step.DamageMult).
	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float BaseDamage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	EMDAttackKind Kind = EMDAttackKind::Light;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FMDAttackStep Step;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float PoiseDamage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	EMDHitReaction Reaction = EMDHitReaction::Flinch;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float Knockback = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector ImpactNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector AttackerLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FGameplayTag ImpactTag;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<UPhysicalMaterial> PhysMaterial = nullptr;
};

// Replicated "last hit" record on the victim. Every machine plays the reaction from it (OnRep).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDHitResultRep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	EMDHitReaction Reaction = EMDHitReaction::None;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	EMDHitDirection Direction = EMDHitDirection::Front;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	FVector_NetQuantize10 ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	FGameplayTag ImpactTag;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	TEnumAsByte<EPhysicalSurface> Surface = SurfaceType_Default;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	bool bCrit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	TObjectPtr<AActor> Instigator = nullptr;

	// Bumped on every hit so an identical repeat hit still replicates.
	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	uint8 Seq = 0;

	// Server world time of the hit; clients ignore records older than a second (late join).
	UPROPERTY(BlueprintReadOnly, Category = "Hit")
	double ServerTime = 0.0;
};

// Outcome of ApplyDamage(FMDHitContext). Plain data.
struct FMDDamageResult
{
	bool bApplied = false;   // Health went down
	bool bCrit = false;
	float Damage = 0.0f;     // raw damage handed to the damage effect (after multiplier, crit and Executioner)
	float Applied = 0.0f;    // Health actually removed
};
