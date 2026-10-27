#include "Ranged/MDRangedWeaponData.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_State_RangedMode, "State.RangedMode");

UObject* UMDRangedWeaponData::LoadWeaponMesh() const
{
	if (!StaticMesh.IsNull())
	{
		return StaticMesh.LoadSynchronous();
	}
	if (!SkeletalMesh.IsNull())
	{
		return SkeletalMesh.LoadSynchronous();
	}
	return nullptr;
}

bool UMDRangedWeaponData::MeshHasSocket(FName Socket) const
{
	if (Socket.IsNone())
	{
		return false;
	}
	UObject* Mesh = LoadWeaponMesh();
	if (const UStaticMesh* SM = Cast<UStaticMesh>(Mesh))
	{
		return SM->FindSocket(Socket) != nullptr;
	}
	if (const USkeletalMesh* SK = Cast<USkeletalMesh>(Mesh))
	{
		return SK->FindSocket(Socket) != nullptr;
	}
	return false;
}

bool UMDRangedWeaponData::HasUsableRightGrip() const
{
	return bHasRightGripOffset || MeshHasSocket(GripRSocket);
}

bool UMDRangedWeaponData::HasUsableSupportGrip() const
{
	return !FMDRangedRules::GripNeedsSupportHand(GripType) || bHasLeftGripOffset || MeshHasSocket(GripLSocket);
}

TArray<FString> UMDRangedWeaponData::GetValidationErrors() const
{
	TArray<FString> Errors;
	if (!FMDRangedRules::IsRangedStance(Stance))
	{
		Errors.Add(TEXT("Stance must be Pistol, Rifle or Staff"));
	}
	if (HeadshotMult < 1.0f)
	{
		Errors.Add(TEXT("HeadshotMult must be >= 1"));
	}
	if (StaticMesh.IsNull() == SkeletalMesh.IsNull())
	{
		Errors.Add(TEXT("Set exactly one of StaticMesh / SkeletalMesh"));
	}
	else if (!LoadWeaponMesh())
	{
		Errors.Add(TEXT("The weapon mesh does not load"));
	}

	const bool bBeamFire = FireMode == EMDFireMode::Beam;
	if (bBeamFire != (HitModel == EMDRangedHitModel::Beam))
	{
		Errors.Add(TEXT("FireMode Beam and HitModel Beam must be used together"));
	}
	if (HitModel == EMDRangedHitModel::Projectile)
	{
		if (RoundsPerSecond <= 0.0f) { Errors.Add(TEXT("RoundsPerSecond must be > 0")); }
		if (DamagePerShot <= 0.0f) { Errors.Add(TEXT("DamagePerShot must be > 0")); }
		if (MagSize <= 0) { Errors.Add(TEXT("MagSize must be > 0")); }
		if (AmmoRow.IsNone()) { Errors.Add(TEXT("AmmoRow must name an ammo item")); }
		if (Projectile.Speed <= 0.0f) { Errors.Add(TEXT("Projectile.Speed must be > 0")); }
	}
	else
	{
		if (Beam.DamagePerSecond <= 0.0f) { Errors.Add(TEXT("Beam.DamagePerSecond must be > 0")); }
		if (Beam.Range <= 0.0f) { Errors.Add(TEXT("Beam.Range must be > 0")); }
		if (Beam.TickHz < 1.0f) { Errors.Add(TEXT("Beam.TickHz must be >= 1")); }
	}
	return Errors;
}
