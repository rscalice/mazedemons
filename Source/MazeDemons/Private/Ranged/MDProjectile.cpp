#include "Ranged/MDProjectile.h"

#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "MDCombatStatics.h"
#include "MDDamageNumbers.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Ranged/MDRangedMath.h"
#include "Ranged/MDRangedWeaponData.h"
#include "UObject/ConstructorHelpers.h"

namespace MDProjectileLocal
{
	// Twins by (world, shot id): the shooter's machine finds its twin when the real projectile's impact arrives.
	TMap<TPair<const UWorld*, uint16>, TWeakObjectPtr<AMDProjectile>>& Twins()
	{
		static TMap<TPair<const UWorld*, uint16>, TWeakObjectPtr<AMDProjectile>> Map;
		return Map;
	}

	// A sweep step never covers more than this (cm): keeps the gravity arc and the hit order accurate at 200 m/s.
	constexpr float MaxStepCm = 100.0f;
	constexpr float MaxFlightSeconds = 8.0f;
}

AMDProjectile::AMDProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);
	NetUpdateFrequency = 20.0f;
	NetPriority = 3.0f;
	InitialLifeSpan = MDProjectileLocal::MaxFlightSeconds + 2.0f;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Bolt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bolt"));
	Bolt->SetupAttachment(Root);
	Bolt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bolt->SetGenerateOverlapEvents(false);
	Bolt->SetCastShadow(false);
	Bolt->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Bolt->SetStaticMesh(Cylinder.Object);
	}
	Bolt->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); // the cylinder's long axis (Z) lies along the flight direction (X)
}

void AMDProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDProjectile, Launch);
	DOREPLIFETIME(AMDProjectile, WeaponData);
	DOREPLIFETIME(AMDProjectile, bImpacted);
	DOREPLIFETIME(AMDProjectile, ImpactPoint);
}

TSubclassOf<AMDProjectile> AMDProjectile::ResolveClass(const UMDRangedWeaponData* Data)
{
	if (Data && !Data->Projectile.ProjectileClass.IsNull())
	{
		if (UClass* Loaded = Data->Projectile.ProjectileClass.LoadSynchronous())
		{
			if (Loaded->IsChildOf(AMDProjectile::StaticClass()))
			{
				return Loaded;
			}
		}
	}
	return AMDProjectile::StaticClass();
}

double AMDProjectile::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void AMDProjectile::Configure(const FMDProjectileParams& Params, bool bTwin)
{
	bIsTwin = bTwin;
	WeaponData = Params.Data;
	Shooter = Params.Shooter;
	BaseDamage = Params.Damage;
	Radius = Params.Data->Projectile.Radius;
	MaxRange = Params.Data->Projectile.MaxRange;
	PierceRemaining = Params.Data->Projectile.Pierce;

	const float Speed = Params.SpeedOverride > 0.0f ? Params.SpeedOverride : Params.Data->Projectile.Speed;
	Launch.Origin = Params.Origin;
	Launch.Velocity = Params.Dir.GetSafeNormal() * Speed;
	Launch.Gravity = Params.Data->Projectile.Gravity;
	Launch.ShotId = Params.ShotId;
	Launch.StartServerTime = static_cast<float>(ServerNow()) - (bTwin ? 0.0f : Params.FastForward);
	AppliedFastForward = bTwin ? 0.0f : Params.FastForward;

	if (AActor* ShooterActor = Params.Shooter)
	{
		IgnoredActors.Add(ShooterActor);
		TArray<AActor*> Attached;
		ShooterActor->GetAttachedActors(Attached, true);
		for (AActor* Child : Attached)
		{
			IgnoredActors.Add(Child);
		}
	}
}

AMDProjectile* AMDProjectile::SpawnReal(UWorld* World, const FMDProjectileParams& Params)
{
	if (!World || !Params.Data || !Params.Shooter || Params.Dir.IsNearlyZero())
	{
		return nullptr;
	}
	APawn* ShooterPawn = Cast<APawn>(Params.Shooter);
	AActor* OwnerActor = ShooterPawn && ShooterPawn->GetController() ? static_cast<AActor*>(ShooterPawn->GetController()) : Params.Shooter;
	AMDProjectile* Projectile = World->SpawnActorDeferred<AMDProjectile>(ResolveClass(Params.Data), FTransform(Params.Dir.Rotation(), Params.Origin),
		OwnerActor, ShooterPawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return nullptr;
	}
	Projectile->Configure(Params, false);
	Projectile->FinishSpawning(FTransform(Params.Dir.Rotation(), Params.Origin)); // BeginPlay fast-forwards on the server
	// A world that has not begun play (unit tests) never calls BeginPlay: do the fast-forward here, once.
	if (IsValid(Projectile) && Projectile->SimTime <= 0.0f && Projectile->AppliedFastForward > 0.0f)
	{
		Projectile->AdvanceSimulation(Projectile->AppliedFastForward);
	}
	return Projectile;
}

AMDProjectile* AMDProjectile::SpawnTwin(UWorld* World, const FMDProjectileParams& Params)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Params.Data || Params.Dir.IsNearlyZero())
	{
		return nullptr;
	}
	AMDProjectile* Twin = World->SpawnActorDeferred<AMDProjectile>(ResolveClass(Params.Data), FTransform(Params.Dir.Rotation(), Params.Origin),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Twin)
	{
		return nullptr;
	}
	Twin->SetReplicates(false); // local only
	Twin->Configure(Params, true);
	Twin->FinishSpawning(FTransform(Params.Dir.Rotation(), Params.Origin));
	MDProjectileLocal::Twins().Add(TPair<const UWorld*, uint16>(World, Params.ShotId), Twin);
	return Twin;
}

AMDProjectile* AMDProjectile::FindTwin(const UWorld* World, uint16 ShotId)
{
	const TWeakObjectPtr<AMDProjectile>* Found = MDProjectileLocal::Twins().Find(TPair<const UWorld*, uint16>(World, ShotId));
	return Found ? Found->Get() : nullptr;
}

FVector AMDProjectile::PositionAt(float FlightSeconds) const
{
	return FMDRangedMath::ProjectilePosition(Launch.Origin, Launch.Velocity, Launch.Gravity, FlightSeconds);
}

void AMDProjectile::BeginPlay()
{
	Super::BeginPlay();
	SpawnWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	BuildVisual();

	if (bIsTwin || HasAuthority())
	{
		SetActorLocation(Launch.Origin);
		if (!bIsTwin && AppliedFastForward > 0.0f)
		{
			AdvanceSimulation(AppliedFastForward); // line up with the shooter's twin: min(RTT / 2, 100 ms)
		}
		UpdateVisual(SimTime);
	}
	else if (Launch.IsSet())
	{
		UpdateOwnerHidden();
	}
}

void AMDProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsTwin && GetWorld())
	{
		MDProjectileLocal::Twins().Remove(TPair<const UWorld*, uint16>(GetWorld(), Launch.ShotId));
	}
	Super::EndPlay(EndPlayReason);
}

void AMDProjectile::BuildVisual()
{
	if (!Bolt)
	{
		return;
	}
	const UMDRangedWeaponData* Data = WeaponData;
	const float Length = Data ? Data->TracerLength : 60.0f;
	const float Thickness = Data ? Data->TracerThickness : 3.0f;
	// Basic cylinder: 100 cm tall, 100 cm wide. Head at the actor, tail trailing behind.
	Bolt->SetRelativeScale3D(FVector(Thickness / 100.0f, Thickness / 100.0f, Length / 100.0f));
	Bolt->SetRelativeLocation(FVector(-Length * 0.5f, 0.0f, 0.0f));
	if (Data && !Data->TracerMaterial.IsNull())
	{
		if (UMaterialInterface* Material = Data->TracerMaterial.LoadSynchronous())
		{
			Bolt->SetMaterial(0, Material);
		}
	}
}

bool AMDProjectile::IsShooterLocal() const
{
	const APawn* ShooterPawn = GetInstigator();
	return ShooterPawn && ShooterPawn->IsLocallyControlled();
}

void AMDProjectile::UpdateOwnerHidden()
{
	// The shooter's own twin shows its bolt; the replicated actor stays hidden for its owner (no double bolt).
	bOwnerHidden = !bIsTwin && !HasAuthority() && IsShooterLocal();
}

void AMDProjectile::UpdateVisual(float FlightSeconds)
{
	if (!Bolt)
	{
		return;
	}
	const FVector Position = PositionAt(FlightSeconds);
	const FVector Velocity = FMDRangedMath::ProjectileVelocity(Launch.Velocity, Launch.Gravity, FlightSeconds);
	SetActorLocationAndRotation(Position, Velocity.Rotation());
	if (!bOwnerHidden && !bFinished)
	{
		Bolt->SetVisibility(true);
	}
}

void AMDProjectile::OnRep_Launch()
{
	BuildVisual();
	UpdateOwnerHidden();
}

void AMDProjectile::OnRep_Impact()
{
	if (!bImpacted)
	{
		return;
	}
	bFinished = true;
	if (Bolt)
	{
		Bolt->SetVisibility(false);
	}
	if (IsShooterLocal())
	{
		// The shooter's twin owns the visual; make it end where the server says (it may already be gone).
		if (AMDProjectile* Twin = FindTwin(GetWorld(), Launch.ShotId))
		{
			Twin->FinishAt(ImpactPoint, FVector::ZeroVector);
		}
		return;
	}
	PlayImpactLocal(ImpactPoint);
}

void AMDProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	if (bIsTwin || HasAuthority())
	{
		const float Target = static_cast<float>(GetWorld()->GetTimeSeconds() - SpawnWorldTime) + AppliedFastForward;
		AdvanceSimulation(Target - SimTime);
		if (!bFinished)
		{
			UpdateVisual(SimTime);
		}
		return;
	}
	// A remote machine flies the replicated launch along the same closed-form path.
	if (Launch.IsSet())
	{
		if (!bOwnerHidden)
		{
			UpdateOwnerHidden();
		}
		UpdateVisual(FMath::Max(static_cast<float>(ServerNow()) - Launch.StartServerTime, 0.0f));
	}
}

bool AMDProjectile::SweepStep(const FVector& A, const FVector& B, FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDProjectileSweep), false, this);
	for (const TWeakObjectPtr<AActor>& Ignored : IgnoredActors)
	{
		if (Ignored.IsValid())
		{
			Query.AddIgnoredActor(Ignored.Get());
		}
	}
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);

	// Geometry (and mesh bodies) by the Visibility channel, characters by object type: a character capsule need not block Visibility,
	// and trigger volumes must not stop a bullet, which an object-type query on world types would do.
	FHitResult WorldHit;
	const bool bWorld = World->SweepSingleByChannel(WorldHit, A, B, FQuat::Identity, ECC_Visibility, Shape, Query);
	FHitResult PawnHit;
	const bool bPawn = World->SweepSingleByObjectType(PawnHit, A, B, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), Shape, Query);
	if (bWorld && (!bPawn || WorldHit.Time <= PawnHit.Time))
	{
		OutHit = WorldHit;
		return true;
	}
	if (bPawn)
	{
		OutHit = PawnHit;
		return true;
	}
	return false;
}

void AMDProjectile::AdvanceSimulation(float DeltaSeconds)
{
	if (bFinished || DeltaSeconds <= 0.0f || !Launch.IsSet())
	{
		return;
	}
	const float Speed = static_cast<float>(Launch.Velocity.Size());
	const float StepSeconds = FMath::Max(MDProjectileLocal::MaxStepCm / FMath::Max(Speed, 1.0f), 0.002f);
	float Remaining = DeltaSeconds;
	while (Remaining > KINDA_SMALL_NUMBER && !bFinished)
	{
		const float Step = FMath::Min(Remaining, StepSeconds);
		const FVector A = PositionAt(SimTime);
		const FVector B = PositionAt(SimTime + Step);
		SimTime += Step;
		Remaining -= Step;

		FHitResult Hit;
		if (SweepStep(A, B, Hit))
		{
			// Restart the step's remainder from the hit point after a pierce or a pass-through: ignore list grew, so the next
			// sweep of the same segment skips the victim.
			if (bIsTwin)
			{
				FinishAt(Hit.ImpactPoint, Hit.ImpactNormal);
				return;
			}
			if (HandleHit(Hit))
			{
				return;
			}
			// Flew on: sweep the same segment again for the next thing behind the victim (the ignore list grew, so this ends).
			SimTime -= Step;
			Remaining += Step;
			continue;
		}
		if (FVector::Dist(B, Launch.Origin) >= MaxRange || SimTime >= MDProjectileLocal::MaxFlightSeconds)
		{
			bFinished = true;
			if (bIsTwin || HasAuthority())
			{
				Destroy(); // a miss: silent
			}
			return;
		}
	}
}

bool AMDProjectile::HandleHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	const UMDRangedWeaponData* Data = WeaponData;
	if (!Data)
	{
		FinishAt(Hit.ImpactPoint, Hit.ImpactNormal);
		return true;
	}
	if (!HitActor || !MDDamageNumbers::IsEligibleVictim(HitActor))
	{
		FinishAt(Hit.ImpactPoint, Hit.ImpactNormal); // world geometry (or anything without an ability system) stops the bolt
		return true;
	}

	IgnoredActors.Add(HitActor); // never hit the same actor twice with one bolt, landed or not

	const float Distance = static_cast<float>(FVector::Dist(Launch.Origin, Hit.ImpactPoint));
	float Damage = BaseDamage * FMDRangedMath::FalloffMultiplier(Distance, Data->FalloffStart, Data->FalloffEnd, Data->FalloffMinMult);
	const bool bHead = FMDRangedMath::IsHeadshot(Hit.BoneName, Hit.ImpactPoint.Z, HitActor->GetActorLocation().Z, HitActor->GetSimpleCollisionHalfHeight());
	if (bHead)
	{
		Damage *= Data->HeadshotMult;
	}

	FMDRangedHitParams Params;
	Params.Damage = Damage;
	Params.PoiseDamage = Data->PoiseDamage;
	Params.ReactionCap = Data->ReactionCap;
	Params.DamageType = Data->DefaultDamageType;
	Params.ImpactTag = Data->ImpactCue;
	Params.bHeadshot = bHead;
	Params.Kind = EMDHitKind::Projectile;
	Params.ShotOrigin = Launch.Origin;
	if (!UMDCombatStatics::ApplyRangedHit(Shooter.Get(), HitActor, Hit, Params))
	{
		return false; // team rules / dodge i-frames / corpse: the bolt flies on without using a pierce
	}
	++HitCount;
	if (PierceRemaining > 0)
	{
		--PierceRemaining;
		return false;
	}
	FinishAt(Hit.ImpactPoint, Hit.ImpactNormal);
	return true;
}

void AMDProjectile::FinishAt(const FVector& Point, const FVector& Normal)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	SetActorLocation(Point);
	if (Bolt)
	{
		Bolt->SetVisibility(false);
	}
	SetActorTickEnabled(false);

	if (bIsTwin)
	{
		PlayImpactLocal(Point);
		Destroy();
		return;
	}
	bImpacted = true;
	ImpactPoint = Point;
	PlayImpactLocal(Point); // the host (and a test) sees it too
	ForceNetUpdate();
	SetLifeSpan(0.5f); // clients get the impact before the actor goes
}

void AMDProjectile::PlayImpactLocal(const FVector& Point)
{
	if (bImpactPlayed)
	{
		return;
	}
	bImpactPlayed = true;
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !WeaponData || WeaponData->ImpactFX.IsNull())
	{
		return;
	}
	if (UNiagaraSystem* FX = WeaponData->ImpactFX.LoadSynchronous())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX, Point, (-Launch.Velocity).Rotation());
	}
}
