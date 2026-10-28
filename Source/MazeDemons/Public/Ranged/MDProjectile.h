// R1: the one projectile class of the game. Players' guns, the Bone Archer and the Cultist all fire AMDProjectile (V1 WP5 only wires
// the brain). Server-authoritative: the server spawns it, fast-forwards it by min(shooter RTT / 2, 100 ms) with sub-stepped sweeps
// and applies the hits through UMDCombatStatics::ApplyRangedHit. The shooter's machine spawns a cosmetic twin instantly and hides the
// replicated actor, so a client sees its own bolt without waiting for the round trip. Other machines fly the replicated actor along
// the same closed-form path (no movement replication), which also makes a late joiner see the bolts in flight (D5).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ranged/MDRangedTypes.h"
#include "MDProjectile.generated.h"

class UMDRangedWeaponData;
class UStaticMeshComponent;

// The replicated launch state: with it any machine can place the bolt at any time (pure maths, FMDRangedMath::ProjectilePosition).
USTRUCT()
struct MAZEDEMONS_API FMDProjectileLaunch
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 Velocity = FVector::ZeroVector;

	// cm/s^2 pulling down.
	UPROPERTY()
	float Gravity = 0.0f;

	// Server clock (AGameStateBase::GetServerWorldTimeSeconds) at which the bolt was at Origin. The server's fast-forward moves this
	// back, so the replicated bolt lines up with the shooter's twin.
	UPROPERTY()
	float StartServerTime = 0.0f;

	UPROPERTY()
	uint16 ShotId = 0;

	bool IsSet() const { return !Velocity.IsNearlyZero(); }
};

// Server-side spawn recipe. Plain data so the pipeline and the tests build it the same way.
struct FMDProjectileParams
{
	UMDRangedWeaponData* Data = nullptr;
	AActor* Shooter = nullptr; // becomes the Instigator; a player's pawn or an enemy character
	FVector Origin = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	// Damage before falloff and headshot (the caller applies perks and the gun's condition).
	float Damage = 0.0f;
	// Seconds to fast-forward right after spawning (FMDRangedMath::ComputeFastForward).
	float FastForward = 0.0f;
	uint16 ShotId = 0;
	// Overrides of the data asset's projectile numbers (<= 0 = use the data).
	float SpeedOverride = 0.0f;
};

UCLASS(NotBlueprintable)
class MAZEDEMONS_API AMDProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMDProjectile();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Server only. Spawns the real, replicated projectile and fast-forwards it. Null when the data is missing or Shooter is null.
	// Owner / Instigator come from Shooter (a pawn's controller owns it, so it is owner-relevant for its shooter).
	static AMDProjectile* SpawnReal(UWorld* World, const FMDProjectileParams& Params);

	// The shooter's machine: a local-only cosmetic bolt (never damages, never replicates). Registered by ShotId so the real
	// actor's impact can finish it. Null on a dedicated server or when the data is missing.
	static AMDProjectile* SpawnTwin(UWorld* World, const FMDProjectileParams& Params);

	// The class a data asset fires: Projectile.ProjectileClass when it is an AMDProjectile subclass, else AMDProjectile.
	static TSubclassOf<AMDProjectile> ResolveClass(const UMDRangedWeaponData* Data);

	// Hard ceiling of a projectile's flight (s): range / speed + MaxLifetimeMargin, never above MaxLifetimeCap. Real bolts and twins
	// end (silently on a miss) at this time; the actor lifespan is LifetimeBackstop longer, so nothing can outlive ~3 s.
	static constexpr float MaxLifetimeCap = 2.75f;
	static constexpr float MaxLifetimeMargin = 0.5f;
	static constexpr float LifetimeBackstop = 0.25f;
	static float ComputeMaxLifetime(float MaxRange, float Speed);
	float GetMaxLifetime() const { return MaxLifetime; }

	bool IsTwin() const { return bIsTwin; }
	uint16 GetShotId() const { return Launch.ShotId; }
	const FMDProjectileLaunch& GetLaunch() const { return Launch; }
	int32 GetPierceRemaining() const { return PierceRemaining; }
	int32 GetHitCount() const { return HitCount; }
	bool HasImpacted() const { return bImpacted; }
	// Seconds the server skipped at spawn (tests).
	float GetAppliedFastForward() const { return AppliedFastForward; }
	// Flight time simulated so far (server / twin).
	float GetSimTime() const { return SimTime; }
	FVector GetSimPosition() const { return PositionAt(SimTime); }

	// Server / test hook: advance the simulation by DeltaSeconds of flight (sub-stepped sweeps, hits applied). Tick calls it.
	void AdvanceSimulation(float DeltaSeconds);

	// The twin of ShotId on this machine, if it still flies.
	static AMDProjectile* FindTwin(const UWorld* World, uint16 ShotId);

	// Position of the bolt after T seconds of flight on its launch path.
	FVector PositionAt(float FlightSeconds) const;

protected:
	UFUNCTION()
	void OnRep_Launch();

	UFUNCTION()
	void OnRep_Impact();

private:
	void Configure(const FMDProjectileParams& Params, bool bTwin);
	void BuildVisual();
	void UpdateVisual(float FlightSeconds);
	void UpdateOwnerHidden();
	bool IsShooterLocal() const;
	double ServerNow() const;

	// Nearest blocking thing on A -> B (world geometry, or a pawn), ignoring the shooter and everything already hit.
	bool SweepStep(const FVector& A, const FVector& B, FHitResult& OutHit) const;

	// Server: resolves one hit. Returns true when the bolt ends here.
	bool HandleHit(const FHitResult& Hit);

	// Server: ends the flight at Point (replicates the impact, hides, short lifespan so clients see it). bOnCreature: the bolt ended in a
	// creature (C1 already plays blood for it), so only world hits get the projectile's own spark / dust.
	void FinishAt(const FVector& Point, const FVector& Normal, bool bOnCreature = false);
	void PlayImpactLocal(const FVector& Point, bool bOnCreature);

	// ---- Replicated ----
	UPROPERTY(ReplicatedUsing = OnRep_Launch)
	FMDProjectileLaunch Launch;

	UPROPERTY(Replicated)
	TObjectPtr<UMDRangedWeaponData> WeaponData;

	UPROPERTY(ReplicatedUsing = OnRep_Impact)
	bool bImpacted = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize ImpactPoint = FVector::ZeroVector;

	UPROPERTY(Replicated)
	bool bImpactOnCreature = false;

	// ---- Server / twin simulation state ----
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Bolt;

	TWeakObjectPtr<AActor> Shooter;
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;
	float BaseDamage = 0.0f;
	float Radius = 10.0f;
	float MaxRange = 4000.0f;
	float MaxLifetime = MaxLifetimeCap;
	int32 PierceRemaining = 0;
	int32 HitCount = 0;
	float SimTime = 0.0f;
	float AppliedFastForward = 0.0f;
	double SpawnWorldTime = 0.0;
	bool bIsTwin = false;
	bool bOwnerHidden = false;
	bool bImpactPlayed = false;
	bool bFinished = false;
};
