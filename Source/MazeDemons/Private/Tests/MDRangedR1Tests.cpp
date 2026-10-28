
// R1 ranged fire pipeline tests (MazeDemons.Ranged.Net.*, .Projectile.*, .Math.*, .Hit.*, .Durability.*).
// The maths and validation are plain functions; the world tests use a transient world with an authoritative shooter, enemies and
// (for friendly fire) a second player. Projectiles are advanced by hand (AdvanceSimulation) because the test world does not tick.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatRules.h"
#include "MDCombatStatics.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"
#include "Ranged/MDProjectile.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedMath.h"
#include "Ranged/MDRangedStatics.h"
#include "Ranged/MDRangedWeaponData.h"

namespace MDRangedR1TestsLocal
{
	// A deterministic pistol: no spread, 10 damage, 20000 cm/s, 10 cm radius, reaction cap Flinch.
	UMDRangedWeaponData* MakeGun(int32 Pierce = 0, EMDHitReaction Cap = EMDHitReaction::Flinch)
	{
		UMDRangedWeaponData* Data = NewObject<UMDRangedWeaponData>(GetTransientPackage());
		Data->FireMode = EMDFireMode::Semi;
		Data->HitModel = EMDRangedHitModel::Projectile;
		Data->RoundsPerSecond = 3.0f;
		Data->DamagePerShot = 10.0f;
		Data->PoiseDamage = 8.0f;
		Data->HeadshotMult = 1.5f;
		Data->SpreadHip = 0.0f;
		Data->SpreadAimed = 0.0f;
		Data->SpreadBloom = 0.0f;
		Data->MagSize = 8;
		Data->ReactionCap = Cap;
		Data->FalloffStart = 1200.0f;
		Data->FalloffEnd = 2000.0f;
		Data->FalloffMinMult = 0.6f;
		Data->Projectile.Speed = 20000.0f;
		Data->Projectile.Radius = 10.0f;
		Data->Projectile.MaxRange = 4000.0f;
		Data->Projectile.Pierce = Pierce;
		return Data;
	}

	UMDEnemyArchetype* MakeArcher(UMDRangedWeaponData* Gun)
	{
		UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
		Archetype->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
		Archetype->MaxHealth = 1000.0f;
		Archetype->MaxPoise = 30.0f;
		Archetype->KillGold = 0;
		Archetype->MoveSpeed = 300.0f;
		Archetype->RangedData = Gun;
		return Archetype;
	}

	struct FR1Fixture
	{
		UWorld* World = nullptr;
		AMDGameState* GameState = nullptr;
		APawn* ShooterPawn = nullptr;
		AMDPlayerState* ShooterState = nullptr;
		UMDRangedComponent* Ranged = nullptr;
		UMDRangedWeaponData* Gun = nullptr;

		bool Init(FAutomationTestBase& Test, UMDRangedWeaponData* InGun = nullptr)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRangedR1TestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			GameState = World->SpawnActor<AMDGameState>();
			if (GameState)
			{
				World->SetGameState(GameState);
			}
			ShooterPawn = World->SpawnActor<APawn>();
			ShooterState = World->SpawnActor<AMDPlayerState>();
			if (!ShooterPawn || !ShooterState)
			{
				Test.AddError(TEXT("Could not spawn the shooter pawn or its player state"));
				return false;
			}
			ShooterState->SetRole(ROLE_Authority);
			ShooterPawn->SetPlayerState(ShooterState);
			UMDAbilitySystemComponent* ASC = ShooterState->GetMDAbilitySystemComponent();
			if (!ASC)
			{
				Test.AddError(TEXT("Shooter player state has no ASC"));
				return false;
			}
			ASC->InitAbilityActorInfo(ShooterState, ShooterPawn);
			ASC->RegisterAttributeSet(ShooterState->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			Ranged = ShooterState->RangedComponent;
			if (!Ranged)
			{
				Test.AddError(TEXT("Player state has no ranged component"));
				return false;
			}
			Gun = InGun ? InGun : MakeGun();
			Ranged->SetPawnForTests(ShooterPawn);
			Ranged->SetWeaponDataForTests(Gun);
			Ranged->SetRangedMode(true);
			Ranged->SetMagRounds(MDItemType::RangedSlot, Gun->MagSize);
			return true;
		}

		AMDEnemyCharacter* SpawnEnemy(const FVector& Location, UMDEnemyArchetype* Archetype = nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>(Location, FRotator::ZeroRotator, Params);
			if (Enemy)
			{
				Enemy->ApplyArchetype(Archetype ? Archetype : MakeArcher(nullptr));
				Enemy->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
			return Enemy;
		}

		// A second player: a character (has a capsule) with a player state and 100 health.
		ACharacter* SpawnPlayerVictim(const FVector& Location, AMDPlayerState*& OutState)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* Victim = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator, Params);
			OutState = World->SpawnActor<AMDPlayerState>();
			if (!Victim || !OutState)
			{
				return nullptr;
			}
			OutState->SetRole(ROLE_Authority);
			Victim->SetPlayerState(OutState);
			UMDAbilitySystemComponent* ASC = OutState->GetMDAbilitySystemComponent();
			ASC->InitAbilityActorInfo(OutState, Victim);
			ASC->RegisterAttributeSet(OutState->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			return Victim;
		}

		FMDProjectileParams MakeParams(const FVector& Origin, const FVector& Dir, uint16 ShotId = 1) const
		{
			FMDProjectileParams P;
			P.Data = Gun;
			P.Shooter = ShooterPawn;
			P.Origin = Origin;
			P.Dir = Dir;
			P.Damage = Gun->DamagePerShot;
			P.ShotId = ShotId;
			return P;
		}

		int32 CountProjectiles() const
		{
			int32 Count = 0;
			for (TActorIterator<AMDProjectile> It(World); It; ++It)
			{
				++Count;
			}
			return Count;
		}

		~FR1Fixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (World->IsRooted())
				{
					World->RemoveFromRoot();
				}
				World = nullptr;
			}
		}
	};

	float HealthOf(AActor* Actor)
	{
		const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Actor);
		return ASC ? ASC->GetHealth() : -1.0f;
	}

	// A shot request that passes validation for the fixture's shooter: from the server's muzzle estimate along the server's aim.
	FMDShotRequest MakeValidRequest(const FR1Fixture& Fx, uint16 Id, float ClientTime)
	{
		FMDShotRequest R;
		R.ShotId = Id;
		R.ClientTime = ClientTime;
		FVector Muzzle = FVector::ZeroVector;
		Fx.Ranged->GetMuzzleWorld(Muzzle);
		R.Origin = Muzzle;
		R.Dir = Fx.ShooterPawn->GetBaseAimRotation().Vector();
		return R;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Net
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1ValidateShotTest, "MazeDemons.Ranged.Net.ValidateShotRejectsCadenceOriginDir", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1ValidateShotTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;

	// ---- pure rules ----
	FMDShotValidationInput In;
	TestEqual(TEXT("a clean shot passes"), UMDRangedStatics::ValidateShot(In), EMDShotReject::None);

	FMDShotValidationInput Empty = In;
	Empty.MagRounds = 0;
	TestEqual(TEXT("empty magazine"), UMDRangedStatics::ValidateShot(Empty), EMDShotReject::NoAmmo);

	FMDShotValidationInput Fast = In; // 3 shots/s: the cap is 3 x 1.15 = 3.45/s -> 0.29 s apart
	Fast.SecondsSinceLastShot = 0.20f;
	TestEqual(TEXT("faster than RPS x 1.15"), UMDRangedStatics::ValidateShot(Fast), EMDShotReject::Cadence);
	Fast.SecondsSinceLastShot = 0.30f;
	TestEqual(TEXT("just inside the cadence tolerance"), UMDRangedStatics::ValidateShot(Fast), EMDShotReject::None);

	FMDShotValidationInput Far = In;
	Far.Origin = FVector(151.0, 0.0, 0.0);
	TestEqual(TEXT("origin 151 cm from the muzzle"), UMDRangedStatics::ValidateShot(Far), EMDShotReject::Origin);
	Far.Origin = FVector(149.0, 0.0, 0.0);
	TestEqual(TEXT("origin 149 cm from the muzzle"), UMDRangedStatics::ValidateShot(Far), EMDShotReject::None);

	FMDShotValidationInput Off = In; // spread 3: the limit is 6 degrees (plus slack 0 here)
	Off.Dir = FVector(FMath::Cos(FMath::DegreesToRadians(10.0)), FMath::Sin(FMath::DegreesToRadians(10.0)), 0.0);
	TestEqual(TEXT("direction 10 degrees off with 3 degrees of spread"), UMDRangedStatics::ValidateShot(Off), EMDShotReject::Direction);
	Off.Dir = FVector(FMath::Cos(FMath::DegreesToRadians(5.0)), FMath::Sin(FMath::DegreesToRadians(5.0)), 0.0);
	TestEqual(TEXT("direction 5 degrees off is inside Spread x 2"), UMDRangedStatics::ValidateShot(Off), EMDShotReject::None);

	FMDShotValidationInput Future = In;
	Future.ClientTimeAheadOfServer = 1.0f;
	TestEqual(TEXT("claims to be from the future"), UMDRangedStatics::ValidateShot(Future), EMDShotReject::Time);

	FMDShotValidationInput Dead = In;
	Dead.bDead = true;
	TestEqual(TEXT("dead"), UMDRangedStatics::ValidateShot(Dead), EMDShotReject::State);
	Dead = In;
	Dead.bRangedMode = false;
	TestEqual(TEXT("not in ranged mode"), UMDRangedStatics::ValidateShot(Dead), EMDShotReject::State);
	Dead = In;
	Dead.bStaggered = true;
	TestEqual(TEXT("staggered"), UMDRangedStatics::ValidateShot(Dead), EMDShotReject::State);

	// ---- through the server entry point ----
	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const uint8 Slot = MDItemType::RangedSlot;
	TestEqual(TEXT("A: first shot accepted"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 1, -2.0f), 0.0f), EMDShotReject::None);
	TestEqual(TEXT("magazine 8 -> 7"), Fx.Ranged->GetMagRounds(Slot), 7);
	TestEqual(TEXT("B: 0.1 s later is too fast"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 2, -1.9f), 0.0f), EMDShotReject::Cadence);
	TestEqual(TEXT("a rejected shot costs no round"), Fx.Ranged->GetMagRounds(Slot), 7);
	TestEqual(TEXT("C: one second after A is fine"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 3, -1.0f), 0.0f), EMDShotReject::None);

	FMDShotRequest BadOrigin = MakeValidRequest(Fx, 4, 0.0f);
	BadOrigin.Origin = FVector(BadOrigin.Origin) + FVector(0.0, 0.0, 1000.0);
	TestEqual(TEXT("D: origin 10 m above the muzzle"), Fx.Ranged->ProcessShot(BadOrigin, 0.0f), EMDShotReject::Origin);

	FMDShotRequest BadDir = MakeValidRequest(Fx, 5, 0.0f);
	BadDir.Dir = -FVector(BadDir.Dir);
	TestEqual(TEXT("E: firing backwards"), Fx.Ranged->ProcessShot(BadDir, 0.0f), EMDShotReject::Direction);

	FMDShotRequest Future2 = MakeValidRequest(Fx, 6, 5.0f);
	TestEqual(TEXT("F: a timestamp from the future"), Fx.Ranged->ProcessShot(Future2, 0.0f), EMDShotReject::Time);

	TestEqual(TEXT("two accepted shots"), Fx.Ranged->GetAcceptedShotCount(), 2);
	TestEqual(TEXT("two projectiles in the world"), Fx.CountProjectiles(), 2);
	TestEqual(TEXT("every request is acknowledged"), (int32)Fx.Ranged->GetShotAck(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1FastForwardTest, "MazeDemons.Ranged.Net.SpawnFastForwardCapped100ms", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1FastForwardTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	TestEqual(TEXT("50 ms round trip -> 25 ms"), FMDRangedMath::ComputeFastForward(0.05f), 0.025f);
	TestEqual(TEXT("200 ms round trip -> 100 ms"), FMDRangedMath::ComputeFastForward(0.2f), 0.1f);
	TestEqual(TEXT("800 ms round trip stays at 100 ms"), FMDRangedMath::ComputeFastForward(0.8f), 0.1f);
	TestEqual(TEXT("negative round trip -> 0"), FMDRangedMath::ComputeFastForward(-1.0f), 0.0f);
	TestEqual(TEXT("statics wrapper caps too"), UMDRangedStatics::ComputeSpawnFastForward(0.5f), 0.1f);

	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	// A 600 ms round trip: the projectile still skips only 100 ms (20000 cm/s -> 2000 cm).
	TestEqual(TEXT("accepted"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 1, -2.0f), 0.6f), EMDShotReject::None);
	AMDProjectile* Real = nullptr;
	for (TActorIterator<AMDProjectile> It(Fx.World); It; ++It)
	{
		Real = *It;
	}
	if (!TestNotNull(TEXT("the server spawned the projectile"), Real))
	{
		return false;
	}
	TestFalse(TEXT("it is the real one, not a twin"), Real->IsTwin());
	TestTrue(TEXT("fast-forwarded by 100 ms"), FMath::IsNearlyEqual(Real->GetAppliedFastForward(), 0.1f, 0.001f));
	const FVector Moved = Real->GetSimPosition() - FVector(Real->GetLaunch().Origin);
	TestTrue(TEXT("it moved about 2000 cm, not 12000"), FMath::IsNearlyEqual((float)Moved.Size(), 2000.0f, 50.0f));

	// A fresh shot at 40 ms round trip skips 20 ms.
	TestEqual(TEXT("second accepted"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 2, -1.0f), 0.04f), EMDShotReject::None);
	AMDProjectile* Second = nullptr;
	for (TActorIterator<AMDProjectile> It(Fx.World); It; ++It)
	{
		if (It->GetShotId() == 2)
		{
			Second = *It;
		}
	}
	if (TestNotNull(TEXT("second projectile"), Second))
	{
		TestTrue(TEXT("20 ms"), FMath::IsNearlyEqual(Second->GetAppliedFastForward(), 0.02f, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1TwinTest, "MazeDemons.Ranged.Net.TwinNoDoubleHit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1TwinTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}
	const float Start = HealthOf(Enemy);
	const FVector Origin(40.0, 0.0, 44.0);

	AMDProjectile* Twin = AMDProjectile::SpawnTwin(Fx.World, Fx.MakeParams(Origin, FVector::ForwardVector, 7));
	if (!TestNotNull(TEXT("the twin spawned"), Twin))
	{
		return false;
	}
	TestTrue(TEXT("it is a twin"), Twin->IsTwin());
	TestTrue(TEXT("the twin is registered by shot id"), AMDProjectile::FindTwin(Fx.World, 7) == Twin);
	Twin->AdvanceSimulation(0.1f);
	TestEqual(TEXT("the twin never damages"), HealthOf(Enemy), Start);
	TestEqual(TEXT("the twin counted no hits"), Twin->GetHitCount(), 0);

	AMDProjectile* Real = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(Origin, FVector::ForwardVector, 7));
	if (!TestNotNull(TEXT("the real projectile spawned"), Real))
	{
		return false;
	}
	Real->AdvanceSimulation(0.1f);
	TestEqual(TEXT("the real projectile hit once"), Real->GetHitCount(), 1);
	TestTrue(TEXT("exactly one hit of damage"), FMath::IsNearlyEqual(Start - HealthOf(Enemy), Fx.Gun->DamagePerShot, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1MagCorrectionTest, "MazeDemons.Ranged.Net.MagCorrectionOnReject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1MagCorrectionTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const uint8 Slot = MDItemType::RangedSlot;
	Fx.Ranged->SetMagRounds(Slot, 5);
	TestEqual(TEXT("view magazine = replicated rounds with nothing in flight"), Fx.Ranged->GetViewMagRounds(), 5);

	FMDShotRequest Bad = MakeValidRequest(Fx, 9, -1.0f);
	Bad.Dir = -FVector(Bad.Dir);
	TestEqual(TEXT("rejected"), Fx.Ranged->ProcessShot(Bad, 0.0f), EMDShotReject::Direction);
	TestEqual(TEXT("no projectile"), Fx.CountProjectiles(), 0);
	TestEqual(TEXT("the magazine is untouched"), Fx.Ranged->GetMagRounds(Slot), 5);
	TestEqual(TEXT("the owner was told once"), Fx.Ranged->GetMagCorrectionCount(), 1);
	TestEqual(TEXT("with the real magazine"), Fx.Ranged->GetLastCorrectedRounds(), 5);
	TestEqual(TEXT("and the request is acknowledged"), (int32)Fx.Ranged->GetShotAck(), 9);

	// Empty magazine: NoAmmo, also corrected.
	Fx.Ranged->SetMagRounds(Slot, 0);
	TestEqual(TEXT("empty magazine rejects"), Fx.Ranged->ProcessShot(MakeValidRequest(Fx, 10, -0.5f), 0.0f), EMDShotReject::NoAmmo);
	TestEqual(TEXT("second correction"), Fx.Ranged->GetMagCorrectionCount(), 2);
	TestEqual(TEXT("with zero rounds"), Fx.Ranged->GetLastCorrectedRounds(), 0);

	// The wrap-safe ack ordering the owner uses to drop its predicted shots.
	TestTrue(TEXT("id 5 is acked by 9"), FMDRangedMath::IsShotAcked(5, 9));
	TestTrue(TEXT("id 9 is acked by 9"), FMDRangedMath::IsShotAcked(9, 9));
	TestFalse(TEXT("id 10 is not acked by 9"), FMDRangedMath::IsShotAcked(10, 9));
	TestTrue(TEXT("wrap: 65535 is acked by 2"), FMDRangedMath::IsShotAcked(65535, 2));
	TestFalse(TEXT("wrap: 3 is not acked by 65535"), FMDRangedMath::IsShotAcked(3, 65535));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1FriendlyFireTest, "MazeDemons.Ranged.Net.FriendlyFireToggle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1FriendlyFireTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	FR1Fixture Fx;
	if (!Fx.Init(*this) || !TestNotNull(TEXT("game state"), Fx.GameState))
	{
		return false;
	}
	AMDPlayerState* VictimState = nullptr;
	ACharacter* Victim = Fx.SpawnPlayerVictim(FVector(800.0, 0.0, 100.0), VictimState);
	if (!TestNotNull(TEXT("victim player"), Victim))
	{
		return false;
	}
	const FVector Origin(40.0, 0.0, 44.0);
	const float Start = HealthOf(Victim);

	Fx.GameState->SetFriendlyFireEnabled(false);
	AMDProjectile* Off = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(Origin, FVector::ForwardVector, 1));
	Off->AdvanceSimulation(0.1f);
	TestEqual(TEXT("friendly fire off: the teammate takes nothing"), HealthOf(Victim), Start);
	TestEqual(TEXT("and the bolt did not count a hit"), Off->GetHitCount(), 0);

	Fx.GameState->SetFriendlyFireEnabled(true);
	AMDProjectile* On = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(Origin, FVector::ForwardVector, 2));
	On->AdvanceSimulation(0.1f);
	TestTrue(TEXT("friendly fire on: the teammate is hurt"), HealthOf(Victim) < Start);
	TestEqual(TEXT("one hit counted"), On->GetHitCount(), 1);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Projectile
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1SharedClassTest, "MazeDemons.Ranged.Projectile.SharedClassPlayerAndArcher", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1SharedClassTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	UMDRangedWeaponData* ArcherBow = MakeGun();
	ArcherBow->DamagePerShot = 12.0f;
	ArcherBow->Projectile.Speed = 6000.0f;
	UMDEnemyArchetype* Archetype = MakeArcher(ArcherBow);
	TestTrue(TEXT("the archetype carries RangedData"), Archetype->RangedData == ArcherBow);

	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Archer = Fx.SpawnEnemy(FVector(0.0, 1500.0, 100.0), Archetype);
	AMDPlayerState* TargetState = nullptr;
	ACharacter* Target = Fx.SpawnPlayerVictim(FVector(0.0, 0.0, 100.0), TargetState);
	if (!TestNotNull(TEXT("archer"), Archer) || !TestNotNull(TEXT("target player"), Target))
	{
		return false;
	}
	const float Start = HealthOf(Target);

	// The player's gun and the archer's bow resolve to the same projectile class.
	TestTrue(TEXT("same class for both"), AMDProjectile::ResolveClass(Fx.Gun) == AMDProjectile::ResolveClass(Archetype->RangedData));
	TestTrue(TEXT("and it is AMDProjectile"), AMDProjectile::ResolveClass(Archetype->RangedData) == AMDProjectile::StaticClass());

	// The Bone Archer's brain will call exactly this.
	AMDProjectile* Bolt = UMDRangedStatics::FireFromData(Archer, Archetype->RangedData, Archer->GetActorLocation() + FVector(0.0, -60.0, 0.0), Target->GetActorLocation());
	if (!TestNotNull(TEXT("the archer fired"), Bolt))
	{
		return false;
	}
	TestTrue(TEXT("the archer's bolt is an AMDProjectile"), Bolt->GetClass() == AMDProjectile::StaticClass());
	Bolt->AdvanceSimulation(0.5f); // 6000 cm/s over ~14 m
	TestTrue(TEXT("the player took the arrow"), HealthOf(Target) < Start);
	TestTrue(TEXT("for about the archer's damage"), FMath::IsNearlyEqual(Start - HealthOf(Target), 12.0f, 3.0f));

	// The same shared class, fired by the player.
	AMDProjectile* PlayerBolt = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(FVector(40.0, 0.0, 44.0), FVector::ForwardVector, 3));
	TestTrue(TEXT("the player's bolt is the same class"), PlayerBolt && PlayerBolt->GetClass() == Bolt->GetClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1PierceTest, "MazeDemons.Ranged.Projectile.PierceCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1PierceTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	const FVector Origin(40.0, 0.0, 44.0);
	for (const int32 Pierce : { 0, 2 })
	{
		FR1Fixture Fx;
		if (!Fx.Init(*this, MakeGun(Pierce)))
		{
			return false;
		}
		TArray<AMDEnemyCharacter*> Line;
		for (int32 i = 0; i < 4; ++i)
		{
			Line.Add(Fx.SpawnEnemy(FVector(500.0 + 250.0 * i, 0.0, 100.0)));
		}
		AMDProjectile* Bolt = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(Origin, FVector::ForwardVector, 1));
		if (!TestNotNull(TEXT("bolt"), Bolt))
		{
			return false;
		}
		TestEqual(TEXT("pierce count read from the data"), Bolt->GetPierceRemaining(), Pierce);
		Bolt->AdvanceSimulation(0.3f);
		const int32 Expected = Pierce + 1;
		TestEqual(*FString::Printf(TEXT("pierce %d hits %d enemies"), Pierce, Expected), Bolt->GetHitCount(), Expected);
		for (int32 i = 0; i < 4; ++i)
		{
			const bool bHurt = HealthOf(Line[i]) < 1000.0f;
			TestEqual(*FString::Printf(TEXT("pierce %d: enemy %d hurt = %d"), Pierce, i, (int32)(i < Expected)), bHurt, i < Expected);
		}
		TestTrue(TEXT("the bolt ended after its last pierce"), Bolt->HasImpacted());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1LateJoinTest, "MazeDemons.Ranged.Projectile.ReplicatesToLateJoiner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1LateJoinTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	// Property level: a late joiner gets Launch / WeaponData / bImpacted / ImpactPoint in the initial bunch and replays the closed-form path.
	for (const TCHAR* Name : { TEXT("Launch"), TEXT("WeaponData"), TEXT("bImpacted"), TEXT("ImpactPoint") })
	{
		const FProperty* Prop = AMDProjectile::StaticClass()->FindPropertyByName(Name);
		if (TestNotNull(*FString::Printf(TEXT("property %s exists"), Name), Prop))
		{
			TestTrue(*FString::Printf(TEXT("%s is replicated"), Name), Prop->HasAnyPropertyFlags(CPF_Net));
		}
	}
	const FProperty* LaunchProp = AMDProjectile::StaticClass()->FindPropertyByName(TEXT("Launch"));
	if (LaunchProp)
	{
		TestTrue(TEXT("Launch notifies the client"), LaunchProp->HasAnyPropertyFlags(CPF_RepNotify));
	}

	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDProjectile* Real = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(FVector(40.0, 0.0, 44.0), FVector::ForwardVector, 4));
	if (!TestNotNull(TEXT("bolt"), Real))
	{
		return false;
	}
	TestTrue(TEXT("the projectile replicates"), Real->GetIsReplicated());
	const FMDProjectileLaunch Replicated = Real->GetLaunch(); // what a late joiner receives
	TestTrue(TEXT("launch state is set"), Replicated.IsSet());
	TestEqual(TEXT("shot id travels with it"), (int32)Replicated.ShotId, 4);
	// A joiner T seconds after the launch computes the position without any movement replication.
	const float T = 0.05f;
	const FVector Joiner = FMDRangedMath::ProjectilePosition(Replicated.Origin, Replicated.Velocity, Replicated.Gravity, T);
	TestTrue(TEXT("the joiner's path equals the server's"), Joiner.Equals(Real->PositionAt(T), 0.5));
	TestTrue(TEXT("the bolt moves forward along the shot"), Joiner.X > Replicated.Origin.X + 500.0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1SpreadTest, "MazeDemons.Ranged.Math.SpreadBloomDecay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1SpreadTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("still: base"), FMDRangedMath::ComputeSpread(3.0f, false, 1.4f, 0.0f), 3.0f);
	TestEqual(TEXT("moving: base x multiplier"), FMDRangedMath::ComputeSpread(3.0f, true, 1.4f, 0.0f), 4.2f);
	TestEqual(TEXT("bloom adds on top"), FMDRangedMath::ComputeSpread(3.0f, false, 1.4f, 1.5f), 4.5f);
	TestEqual(TEXT("a multiplier below 1 never tightens"), FMDRangedMath::ComputeSpread(3.0f, true, 0.5f, 0.0f), 3.0f);

	float Bloom = 0.0f;
	for (int32 i = 0; i < 3; ++i)
	{
		Bloom = FMDRangedMath::AddBloom(Bloom, 0.6f, 3.0f);
	}
	TestTrue(TEXT("three shots add 1.8"), FMath::IsNearlyEqual(Bloom, 1.8f, 0.001f));
	for (int32 i = 0; i < 20; ++i)
	{
		Bloom = FMDRangedMath::AddBloom(Bloom, 0.6f, 3.0f);
	}
	TestEqual(TEXT("bloom clamps at the cap"), Bloom, 3.0f);

	TestTrue(TEXT("0.5 s at 3 deg/s removes 1.5"), FMath::IsNearlyEqual(FMDRangedMath::DecayBloom(3.0f, 3.0f, 0.5f), 1.5f, 0.001f));
	TestEqual(TEXT("bloom never goes negative"), FMDRangedMath::DecayBloom(1.0f, 3.0f, 5.0f), 0.0f);
	TestEqual(TEXT("no time, no decay"), FMDRangedMath::DecayBloom(1.0f, 3.0f, 0.0f), 1.0f);

	// An auto gun at 8 shots/s: bloom grows 0.6 per 0.125 s and decays 3 per second -> net 0.225 per shot until the cap.
	float Auto = 0.0f;
	for (int32 i = 0; i < 40; ++i)
	{
		Auto = FMDRangedMath::AddBloom(Auto, 0.6f, 3.0f);
		Auto = FMDRangedMath::DecayBloom(Auto, 3.0f, 0.125f);
	}
	TestTrue(TEXT("sustained fire settles below the cap and above zero"), Auto > 1.0f && Auto <= 3.0f);

	// Spread directions stay inside the cone and repeat for the same seed.
	const FVector Aim = FVector::ForwardVector;
	bool bAllInside = true;
	bool bAnyOff = false;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		const FVector A = FMDRangedMath::SpreadDirection(Aim, 4.0f, Seed);
		const FVector B = FMDRangedMath::SpreadDirection(Aim, 4.0f, Seed);
		bAllInside &= FMDRangedMath::AngleBetweenDeg(A, Aim) <= 4.001f && A.Equals(B, 0.0001);
		bAnyOff |= FMDRangedMath::AngleBetweenDeg(A, Aim) > 0.5f;
	}
	TestTrue(TEXT("200 seeds stay inside 4 degrees and repeat"), bAllInside);
	TestTrue(TEXT("and they do spread out"), bAnyOff);
	TestTrue(TEXT("zero spread is exactly the aim"), FMDRangedMath::SpreadDirection(Aim, 0.0f, 5).Equals(Aim, 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1FalloffTest, "MazeDemons.Ranged.Math.FalloffCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1FalloffTest::RunTest(const FString& Parameters)
{
	// Pistol numbers: full damage to 12 m, 60% from 20 m, linear between.
	TestEqual(TEXT("point blank"), FMDRangedMath::FalloffMultiplier(0.0f, 1200.0f, 2000.0f, 0.6f), 1.0f);
	TestEqual(TEXT("exactly at the start"), FMDRangedMath::FalloffMultiplier(1200.0f, 1200.0f, 2000.0f, 0.6f), 1.0f);
	TestTrue(TEXT("halfway: 80%"), FMath::IsNearlyEqual(FMDRangedMath::FalloffMultiplier(1600.0f, 1200.0f, 2000.0f, 0.6f), 0.8f, 0.001f));
	TestEqual(TEXT("at the end"), FMDRangedMath::FalloffMultiplier(2000.0f, 1200.0f, 2000.0f, 0.6f), 0.6f);
	TestEqual(TEXT("beyond the end stays at the floor"), FMDRangedMath::FalloffMultiplier(9000.0f, 1200.0f, 2000.0f, 0.6f), 0.6f);
	TestEqual(TEXT("a degenerate range is the floor past the start"), FMDRangedMath::FalloffMultiplier(500.0f, 100.0f, 100.0f, 0.5f), 0.5f);
	float Last = 1.0f;
	bool bMonotonic = true;
	for (float D = 0.0f; D <= 3000.0f; D += 50.0f)
	{
		const float M = FMDRangedMath::FalloffMultiplier(D, 1200.0f, 2000.0f, 0.6f);
		bMonotonic &= M <= Last + KINDA_SMALL_NUMBER;
		Last = M;
	}
	TestTrue(TEXT("never increases with distance"), bMonotonic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1RecoilTest, "MazeDemons.Ranged.Math.RecoilRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1RecoilTest::RunTest(const FString& Parameters)
{
	const FVector2D K1 = FMDRangedMath::RecoilKick(1.2f, 0.4f, 11);
	const FVector2D K2 = FMDRangedMath::RecoilKick(1.2f, 0.4f, 11);
	TestEqual(TEXT("pitch is the data value"), (float)K1.X, 1.2f);
	TestTrue(TEXT("yaw is within the data range"), FMath::Abs(K1.Y) <= 0.4f + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("same seed, same kick"), K1.Equals(K2, 0.0001));
	bool bBothSigns = false;
	float Min = 0.0f, Max = 0.0f;
	for (int32 Seed = 0; Seed < 100; ++Seed)
	{
		const float Y = (float)FMDRangedMath::RecoilKick(1.2f, 0.4f, Seed).Y;
		Min = FMath::Min(Min, Y);
		Max = FMath::Max(Max, Y);
	}
	bBothSigns = Min < -0.05f && Max > 0.05f;
	TestTrue(TEXT("yaw goes both ways"), bBothSigns);

	// Recovery: exponential, about 5% left after RecoverySeconds, gone for good afterwards.
	float Left = 5.0f;
	for (int32 i = 0; i < 25; ++i) // 0.25 s in 10 ms steps
	{
		Left = FMDRangedMath::RecoilRemaining(Left, 0.25f, 0.01f);
	}
	TestTrue(TEXT("5% left after the recovery time"), FMath::IsNearlyEqual(Left, 5.0f * 0.0498f, 0.02f));
	float Prev = 5.0f;
	bool bShrinks = true;
	for (int32 i = 0; i < 40; ++i)
	{
		const float Next = FMDRangedMath::RecoilRemaining(Prev, 0.25f, 0.016f);
		bShrinks &= Next < Prev;
		Prev = Next;
	}
	TestTrue(TEXT("shrinks every frame"), bShrinks);
	TestEqual(TEXT("zero recovery time snaps back"), FMDRangedMath::RecoilRemaining(2.0f, 0.0f, 0.016f), 0.0f);
	TestEqual(TEXT("tiny remainders snap to zero"), FMDRangedMath::RecoilRemaining(0.0005f, 0.25f, 0.016f), 0.0f);
	TestTrue(TEXT("negative yaw recovers toward zero"), FMDRangedMath::RecoilRemaining(-1.0f, 0.25f, 0.05f) > -1.0f);
	// Frame-rate independence: one 0.1 s step equals ten 0.01 s steps.
	float Ten = 3.0f;
	for (int32 i = 0; i < 10; ++i)
	{
		Ten = FMDRangedMath::RecoilRemaining(Ten, 0.25f, 0.01f);
	}
	TestTrue(TEXT("frame-rate independent"), FMath::IsNearlyEqual(Ten, FMDRangedMath::RecoilRemaining(3.0f, 0.25f, 0.1f), 0.001f));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hit
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1ReactionCapTest, "MazeDemons.Ranged.Hit.ReactionCapPerWeapon", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1ReactionCapTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;

	// ---- the rule ----
	FMDReactionInput Break;
	Break.Poise = 3.0f;
	Break.MaxPoise = 30.0f;
	Break.PoiseDamage = 8.0f;
	Break.StepReaction = EMDHitReaction::Flinch;
	const FMDReactionResult Uncapped = UMDCombatRules::ResolveReaction(Break);
	TestTrue(TEXT("uncapped, a poise break staggers"), Uncapped.Reaction == EMDHitReaction::Stagger && Uncapped.bStartStun);

	FMDReactionInput Pistol = Break;
	Pistol.MaxReaction = EMDHitReaction::Flinch;
	const FMDReactionResult Capped = UMDCombatRules::ResolveReaction(Pistol);
	TestEqual(TEXT("a Flinch-capped gun only flinches"), Capped.Reaction, EMDHitReaction::Flinch);
	TestFalse(TEXT("and never starts the stun"), Capped.bStartStun);
	TestEqual(TEXT("poise is still gone"), Capped.PoiseAfter, 0.0f);

	FMDReactionInput Bolter = Break;
	Bolter.MaxReaction = EMDHitReaction::Stagger;
	TestEqual(TEXT("a Stagger-capped gun staggers"), UMDCombatRules::ResolveReaction(Bolter).Reaction, EMDHitReaction::Stagger);

	FMDReactionInput Heavy = Break;
	Heavy.StepReaction = EMDHitReaction::Knockdown;
	Heavy.MaxReaction = EMDHitReaction::Flinch;
	TestEqual(TEXT("the cap also clamps the step reaction"), UMDCombatRules::ResolveReaction(Heavy).Reaction, EMDHitReaction::Flinch);
	TestEqual(TEXT("ClampReaction helper"), FMDRangedMath::ClampReaction(EMDHitReaction::Knockdown, EMDHitReaction::Stagger), EMDHitReaction::Stagger);
	TestEqual(TEXT("ClampReaction leaves lower ones"), FMDRangedMath::ClampReaction(EMDHitReaction::Flinch, EMDHitReaction::Stagger), EMDHitReaction::Flinch);

	// ---- through the projectile and the poise component ----
	for (const EMDHitReaction Cap : { EMDHitReaction::Flinch, EMDHitReaction::Stagger })
	{
		UMDRangedWeaponData* Gun = MakeGun(0, Cap);
		Gun->PoiseDamage = 500.0f; // breaks any poise bar
		FR1Fixture Fx;
		if (!Fx.Init(*this, Gun))
		{
			return false;
		}
		AMDEnemyCharacter* Enemy = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
		if (!TestNotNull(TEXT("enemy"), Enemy))
		{
			return false;
		}
		AMDProjectile* Bolt = AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(FVector(40.0, 0.0, 44.0), FVector::ForwardVector, 1));
		Bolt->AdvanceSimulation(0.1f);
		TestEqual(TEXT("the hit landed"), Bolt->GetHitCount(), 1);
		TestEqual(*FString::Printf(TEXT("cap %d: stunned = %d"), (int32)Cap, (int32)(Cap == EMDHitReaction::Stagger)),
			UMDPoiseComponent::IsActorStunned(Enemy), Cap == EMDHitReaction::Stagger);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1HeadshotTest, "MazeDemons.Ranged.Hit.Headshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1HeadshotTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	// ---- the rule ----
	TestTrue(TEXT("a head bone"), FMDRangedMath::IsHeadshot(FName(TEXT("head")), 0.0, 0.0, 0.0));
	TestTrue(TEXT("bone names match case-insensitively"), FMDRangedMath::IsHeadshot(FName(TEXT("Neck_Head_01")), 0.0, 0.0, 0.0));
	TestFalse(TEXT("a spine bone"), FMDRangedMath::IsHeadshot(FName(TEXT("spine_02")), 0.0, 100.0, 88.0));
	TestTrue(TEXT("top of the capsule without a bone"), FMDRangedMath::IsHeadshot(NAME_None, 175.0, 100.0, 88.0));
	TestFalse(TEXT("chest height without a bone"), FMDRangedMath::IsHeadshot(NAME_None, 120.0, 100.0, 88.0));
	TestFalse(TEXT("no capsule, no headshot"), FMDRangedMath::IsHeadshot(NAME_None, 500.0, 100.0, 0.0));

	// ---- through the projectile: head x1.5 ----
	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Body = Fx.SpawnEnemy(FVector(800.0, -300.0, 100.0));
	AMDEnemyCharacter* Head = Fx.SpawnEnemy(FVector(800.0, 300.0, 100.0));
	if (!TestNotNull(TEXT("enemies"), Body) || !TestNotNull(TEXT("enemies"), Head))
	{
		return false;
	}
	const float HalfHeight = Head->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float StartBody = HealthOf(Body);
	const float StartHead = HealthOf(Head);

	const FVector BodyOrigin(40.0, -300.0, 100.0);
	const FVector HeadOrigin(40.0, 300.0, 100.0 + HalfHeight - 12.0);
	AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(BodyOrigin, FVector::ForwardVector, 1))->AdvanceSimulation(0.1f);
	AMDProjectile::SpawnReal(Fx.World, Fx.MakeParams(HeadOrigin, FVector::ForwardVector, 2))->AdvanceSimulation(0.1f);
	const float BodyDamage = StartBody - HealthOf(Body);
	const float HeadDamage = StartHead - HealthOf(Head);
	TestTrue(TEXT("the body shot landed"), BodyDamage > 0.0f);
	TestTrue(TEXT("the head shot landed"), HeadDamage > 0.0f);
	TestTrue(TEXT("the head shot does 1.5x"), FMath::IsNearlyEqual(HeadDamage, BodyDamage * 1.5f, 0.1f));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Durability
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedR1WearTest, "MazeDemons.Ranged.Durability.WearPerLandedHit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRangedR1WearTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedR1TestsLocal;
	// ---- the accumulator ----
	float Acc = 0.0f;
	TestEqual(TEXT("first landed hit: 0 whole points"), FMDRangedMath::AccumulateWear(Acc, 0.5f), 0);
	TestEqual(TEXT("second landed hit: 1 point"), FMDRangedMath::AccumulateWear(Acc, 0.5f), 1);
	TestEqual(TEXT("then it starts over"), FMDRangedMath::AccumulateWear(Acc, 0.5f), 0);
	TestEqual(TEXT("a heavier gun pays out sooner"), FMDRangedMath::AccumulateWear(Acc, 1.5f), 2);

	// ---- the real pipeline: HollowPistol in slot 9, two landed hits wear one point, a miss wears nothing ----
	FR1Fixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	Fx.Ranged->SetWeaponDataForTests(nullptr); // use the inventory's real gun below
	UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	UMDInventoryComponent* Inv = NewObject<UMDInventoryComponent>(Fx.ShooterState);
	Inv->ItemTable = Items;
	Inv->RegisterComponent();
	if (!TestTrue(TEXT("the pistol was added"), Inv->AddItem(FName(TEXT("HollowPistol")), 1)))
	{
		return false;
	}
	Inv->RequestEquip(0);
	UMDRangedWeaponData* Real = Inv->GetEquippedRangedData();
	if (!TestNotNull(TEXT("the pistol is equipped in the ranged slot"), Real))
	{
		return false;
	}
	int32 Cur = 0, Max = 0;
	Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestTrue(TEXT("the gun has durability"), Max > 0);
	const int32 Before = Cur;
	TestTrue(TEXT("WearPerHit is 0.5 on the real pistol"), FMath::IsNearlyEqual(Real->WearPerHit, 0.5f, 0.001f));

	AMDEnemyCharacter* Enemy = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}
	const FVector Origin(40.0, 0.0, 44.0);
	const FVector Target(800.0, 0.0, 44.0);

	// A shot into empty space lands nothing.
	UMDRangedStatics::FireFromData(Fx.ShooterPawn, Real, Origin, FVector(40.0, 3000.0, 44.0))->AdvanceSimulation(0.5f);
	Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestEqual(TEXT("a miss wears nothing"), Cur, Before);

	UMDRangedStatics::FireFromData(Fx.ShooterPawn, Real, Origin, Target)->AdvanceSimulation(0.2f);
	Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestEqual(TEXT("one landed hit: still the same whole number"), Cur, Before);

	UMDRangedStatics::FireFromData(Fx.ShooterPawn, Real, Origin, Target)->AdvanceSimulation(0.2f);
	Inv->GetSlotDurability(MDItemType::RangedSlot, Cur, Max);
	TestEqual(TEXT("two landed hits: one point"), Cur, Before - 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
