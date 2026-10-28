// R1: world-facing helpers of the projectile fire pipeline (pure rules live in FMDRangedMath).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Ranged/MDRangedMath.h"
#include "MDRangedStatics.generated.h"

class AMDProjectile;
class UMDRangedWeaponData;
class UWorld;

UCLASS()
class MAZEDEMONS_API UMDRangedStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// The server's verdict on one shot request: magazine, cadence (<= RPS x 1.15), origin within 150 cm of the muzzle, direction
	// within Spread x 2 of the server's aim estimate, state gates. Rejected shots spawn nothing and resync the magazine.
	static EMDShotReject ValidateShot(const FMDShotValidationInput& Input) { return FMDRangedMath::ValidateShot(Input); }

	// Seconds the server fast-forwards a fresh projectile: min(RTT / 2, 100 ms), 0 when md.Ranged.SpawnCompensation is 0.
	// md.Ranged.FakeLag adds milliseconds to RTT for a quick lag test on a listen server host.
	static float ComputeSpawnFastForward(float RoundTripSeconds);

	// Server: tells the AI hearing sense about a shot (loudness 1, tag "Gunshot"), for WP3's perception.
	static void ReportGunshot(UWorld* World, const FVector& Location, AActor* Shooter);

	// Server: the one way anything fires the shared projectile from a weapon data asset. The Bone Archer's brain calls it with its
	// archetype RangedData; the player's fire pipeline uses the same spawn underneath. Damage is the data's DamagePerShot unless
	// DamageOverride > 0. Direction = Origin -> TargetPoint with a random cone of SpreadDeg. Returns the real (replicated) projectile.
	static AMDProjectile* FireFromData(AActor* Shooter, const UMDRangedWeaponData* Data, const FVector& Origin, const FVector& TargetPoint,
		float SpreadDeg = 0.0f, float DamageOverride = 0.0f, int32 Seed = 0);
};
