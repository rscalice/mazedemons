#include "Ranged/MDRangedStatics.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Perception/AISense_Hearing.h"
#include "Ranged/MDProjectile.h"
#include "Ranged/MDRangedWeaponData.h"

namespace MDRangedStaticsLocal
{
	TAutoConsoleVariable<int32> CVarSpawnCompensation(TEXT("md.Ranged.SpawnCompensation"), 1,
		TEXT("1 = the server fast-forwards fresh projectiles by min(shooter RTT / 2, 100 ms); 0 = off."));

	TAutoConsoleVariable<float> CVarFakeLag(TEXT("md.Ranged.FakeLag"), 0.0f,
		TEXT("Milliseconds added to the shooter's RTT in the spawn compensation (dev: test the 100 ms cap without real packet lag)."));
}

float UMDRangedStatics::ComputeSpawnFastForward(float RoundTripSeconds)
{
	using namespace MDRangedStaticsLocal;
	if (CVarSpawnCompensation.GetValueOnAnyThread() == 0)
	{
		return 0.0f;
	}
	return FMDRangedMath::ComputeFastForward(RoundTripSeconds + CVarFakeLag.GetValueOnAnyThread() * 0.001f);
}

void UMDRangedStatics::ReportGunshot(UWorld* World, const FVector& Location, AActor* Shooter)
{
	if (World && World->GetNetMode() != NM_Client)
	{
		UAISense_Hearing::ReportNoiseEvent(World, Location, 1.0f, Shooter, 0.0f, FName(TEXT("Gunshot")));
	}
}

AMDProjectile* UMDRangedStatics::FireFromData(AActor* Shooter, const UMDRangedWeaponData* Data, const FVector& Origin, const FVector& TargetPoint,
	float SpreadDeg, float DamageOverride, int32 Seed)
{
	if (!Shooter || !Data || !Shooter->HasAuthority() || !Shooter->GetWorld())
	{
		return nullptr;
	}
	const FVector Aim = (TargetPoint - Origin).GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		return nullptr;
	}
	FMDProjectileParams Params;
	Params.Data = const_cast<UMDRangedWeaponData*>(Data);
	Params.Shooter = Shooter;
	Params.Origin = Origin;
	Params.Dir = FMDRangedMath::SpreadDirection(Aim, SpreadDeg, Seed);
	Params.Damage = DamageOverride > 0.0f ? DamageOverride : Data->DamagePerShot;
	Params.FastForward = 0.0f; // AI shots are server-side: no shooter latency to hide
	return AMDProjectile::SpawnReal(Shooter->GetWorld(), Params);
}
