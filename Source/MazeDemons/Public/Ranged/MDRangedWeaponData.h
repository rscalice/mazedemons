// R0: data asset of one ranged weapon (ranged-design.md section 3.2). Referenced from FMDItemStatsRow::RangedData.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Ranged/MDRangedTypes.h"
#include "MDRangedWeaponData.generated.h"

class AActor;
class UAnimMontage;
class UCurveFloat;
class UMaterialInterface;
class UNiagaraSystem;
class UObject;
class USkeletalMesh;
class UStaticMesh;

USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDRangedProjectileParams
{
	GENERATED_BODY()

	// AMDProjectile subclass. Empty = AMDProjectile itself (players and archers share the class).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	TSoftClassPtr<AActor> ProjectileClass;

	// Sweep radius (cm). 8-12 keeps fast bolts forgiving at 120 ms of lag.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1"))
	float Radius = 10.0f;

	// The bolt disappears after flying this far (cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "100"))
	float MaxRange = 4000.0f;

	// cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0"))
	float Speed = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float Gravity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0"))
	int32 Pierce = 0;
};

USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDRangedBeamParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "1"))
	float TickHz = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float DamagePerSecond = 0.0f;

	// cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float Range = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float HeatPerSecond = 0.0f;

	// Unused (the idle vent rate is VentPerSecond); kept so existing assets still load.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float VentSeconds = 0.0f;

	// R5: heat lost per second while the trigger is up (Duskray: 8).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float VentPerSecond = 8.0f;

	// R5: heat at which the weapon overheats (Duskray: 100). Mirrors the MaxHeat attribute on the player state.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "1"))
	float MaxHeat = 100.0f;

	// R5: durability a landed beam costs per second (only ticks that hit something count).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float BeamDurabilityPerSecond = 0.5f;

	// R5 (staff, V2): charge regained per second while the trigger is up, and the size of the pool.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float ChargeRegenPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "1"))
	float MaxCharge = 100.0f;

	// Seconds the weapon is locked after overheating (Duskray: 2.5).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float OverheatLock = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float ChargeDrainPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float BurstDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float BurstRadius = 0.0f;
};

UCLASS(BlueprintType)
class MAZEDEMONS_API UMDRangedWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---- Firing ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	EMDFireMode FireMode = EMDFireMode::Semi;

	// Projectile or Beam. Hitscan was dropped 2026-10-27.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	EMDRangedHitModel HitModel = EMDRangedHitModel::Projectile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float RoundsPerSecond = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float DamagePerShot = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "1"))
	int32 Pellets = 1;

	// Distance (cm) -> damage multiplier. Empty = no falloff.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	TSoftObjectPtr<UCurveFloat> FalloffCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float PoiseDamage = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "1"))
	float HeadshotMult = 1.5f;

	// Damage.Type.* of every hit (empty = Physical). Duskray: Frost.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	FGameplayTag DefaultDamageType;

	// ---- Spread / recoil (degrees) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float SpreadHip = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float SpreadAimed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float SpreadMoving = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float SpreadBloom = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float SpreadBloomDecay = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy")
	float RecoilPitch = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy")
	float RecoilYaw = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float RecoilRecovery = 0.25f;

	// ---- Ammo ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MagSize = 8;

	// DT_Items row of the ammo (AmmoLight / AmmoHeavy). None for beams.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	FName AmmoRow = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	float ReloadSeconds = 1.2f;

	// ---- Projectile / beam ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	FMDRangedProjectileParams Projectile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam")
	FMDRangedBeamParams Beam;

	// ---- Stance, grip, mesh ----
	// Pistol, Rifle or Staff: the stance the anim BP reads while this gun is held in ranged mode.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance")
	EMDWeaponStance Stance = EMDWeaponStance::Pistol;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance")
	EMDGripType GripType = EMDGripType::OneHanded;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance")
	FName AimCameraPreset = NAME_None;

	// Exactly one of these is set. Fab meshes are only referenced, never saved or edited.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	// Socket on the player's visible mesh the weapon is attached to (the same hand socket the melee weapons use).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName HandSocket = FName(TEXT("MainHand"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName MuzzleSocket = FName(TEXT("Muzzle"));

	// Main-hand grip socket on the weapon mesh. If the mesh lacks it, HandOffset is used and bHasRightGripOffset must be true.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName GripRSocket = FName(TEXT("Grip_R"));

	// Support-hand grip socket (foregrip). If the mesh lacks it, SupportHandOffset is used and bHasLeftGripOffset must be true.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName GripLSocket = FName(TEXT("Grip_L"));

	// Weapon mesh relative to the hand socket (replaces Grip_R when the mesh has none; Fab meshes are never edited).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FTransform HandOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	bool bHasRightGripOffset = false;

	// Support-hand target relative to the weapon mesh (replaces Grip_L when the mesh has none).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FTransform SupportHandOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	bool bHasLeftGripOffset = false;

	// ---- Montages (empty until the animation set lands) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> SwapInMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> SwapOutMontage;

	// ---- Cue tags (R1) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	FGameplayTag MuzzleCue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	FGameplayTag TracerCue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	FGameplayTag ImpactCue;

	// ---- R1 fire pipeline ----
	// Damage falloff over distance (cm): full up to FalloffStart, FalloffMinMult from FalloffEnd on, linear between.
	// FMDRangedMath::FalloffMultiplier. (FalloffCurve above is unused; the three numbers are easier to tune and test.)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float FalloffStart = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float FalloffEnd = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0", ClampMax = "1"))
	float FalloffMinMult = 0.6f;

	// Highest bloom (degrees) added on top of the base spread.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float MaxBloom = 3.0f;

	// Strongest hit reaction a hit of this weapon causes, poise break included. Pistol / repeater: Flinch. Bolter: Stagger.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	EMDHitReaction ReactionCap = EMDHitReaction::Flinch;

	// Durability lost per landed hit (R1: 0.5).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = "0"))
	float WearPerHit = 0.5f;

	// Where bullets leave the gun, in the weapon mesh's own space (the Fab meshes have no Muzzle socket and are never edited).
	// Used when the mesh has no MuzzleSocket. gunsetup.py fills it from the mesh bounds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FVector MuzzleLocal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	bool bHasMuzzleLocal = false;

	// Weapon kick on every shot (the pistol / rifle sets have no fire clips): the gun jerks back and tips up, then settles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float KickBackCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0"))
	float KickPitchDeg = 7.0f;

	// ---- R1 cosmetics (referenced Fab assets, never edited) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlashFX;

	// Beam hit effect (R5, the Duskray's FreeStylizedLaserBeamVFX hit). Projectiles do NOT use it: a bullet's world impact comes from
	// DA_Impact_Bullet (a small spark / dust puff) and a creature's from C1's blood. Leave it empty on projectile guns.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	TSoftObjectPtr<UNiagaraSystem> ImpactFX;

	// Material of the stretched cylinder that is the tracer bolt (FreeStylizedLaserBeamVFX MI_Cylinder_xx).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	TSoftObjectPtr<UMaterialInterface> TracerMaterial;

	// Bolt length and thickness (cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues", meta = (ClampMin = "1"))
	float TracerLength = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues", meta = (ClampMin = "0.1"))
	float TracerThickness = 3.0f;

	// ---- R5 beam cosmetics (referenced Fab assets, never edited) ----
	// The laser system (FreeStylizedLaserBeamVFX NS_Laser_xx). Needs the user parameters LaserEnd (world position) and Color.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam")
	TSoftObjectPtr<UNiagaraSystem> BeamFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam")
	FLinearColor BeamColor = FLinearColor(0.8f, 0.3f, 1.0f, 1.0f);

	// Steam puff at the muzzle when the weapon overheats (one-shot, capped), and a smaller one when the lock ends and it has vented.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam")
	TSoftObjectPtr<UNiagaraSystem> OverheatFX;

	// Loads the mesh (static or skeletal). Null when none is set or it cannot load.
	UObject* LoadWeaponMesh() const;

	// True when the loaded mesh has a socket of this name.
	bool MeshHasSocket(FName Socket) const;

	// Grip data is usable: sockets exist on the mesh or a per-weapon offset stands in for them.
	bool HasUsableRightGrip() const;
	bool HasUsableSupportGrip() const;

	// Everything wrong with this asset (empty = valid). Used by Ranged.Data.RowsValidate.
	TArray<FString> GetValidationErrors() const;
};

// Row of DT_ElementResist (skeleton, R6 fills the numbers): damage multipliers per Damage.Type for one enemy.
// Row name = the enemy archetype tag (for example "Enemy.Ghoul").
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDElementResistRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resist", meta = (ClampMin = "0"))
	float Physical = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resist", meta = (ClampMin = "0"))
	float Frost = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resist", meta = (ClampMin = "0"))
	float Fire = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resist", meta = (ClampMin = "0"))
	float Poison = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resist", meta = (ClampMin = "0"))
	float Bleed = 1.0f;
};
