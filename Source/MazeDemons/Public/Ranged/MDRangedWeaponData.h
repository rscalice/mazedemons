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
class UObject;
class USkeletalMesh;
class UStaticMesh;

USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDRangedProjectileParams
{
	GENERATED_BODY()

	// AMDProjectile subclass (R1). Empty until R1 lands.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	TSoftClassPtr<AActor> ProjectileClass;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = "0"))
	float VentSeconds = 0.0f;

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
