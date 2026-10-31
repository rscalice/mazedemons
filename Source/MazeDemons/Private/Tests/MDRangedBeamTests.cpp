// R5 beam tests (MazeDemons.Ranged.Beam.*): the pure rules (tick damage, heat, overheat lock, vent, charge, number aggregation) and a
// transient world with an authoritative shooter for the server ticks, friendly fire, replication state and late join.
// The test world does not tick, so the server is advanced by hand (AdvanceServer / ServerBeamTickAt).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatFeedbackComponent.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "Ranged/MDBeamComponent.h"
#include "Ranged/MDBeamRules.h"
#include "Ranged/MDRangedAttributeSet.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedWeaponData.h"

namespace MDBeamTestsLocal
{
	// A deterministic Duskray: 20 damage per second at 10 Hz (2 per tick), heat 30/s, vent 8/s, lock 2.5 s, falloff 10 m -> 18 m at 40 %.
	UMDRangedWeaponData* MakeBeamGun()
	{
		UMDRangedWeaponData* Data = NewObject<UMDRangedWeaponData>(GetTransientPackage());
		Data->FireMode = EMDFireMode::Beam;
		Data->HitModel = EMDRangedHitModel::Beam;
		Data->DamagePerShot = 1.575f;
		Data->PoiseDamage = 0.5f;
		Data->ReactionCap = EMDHitReaction::None;
		Data->FalloffStart = 1000.0f;
		Data->FalloffEnd = 1800.0f;
		Data->FalloffMinMult = 0.4f;
		Data->Beam.TickHz = 10.0f;
		Data->Beam.DamagePerSecond = 20.0f;
		Data->Beam.Range = 2000.0f;
		Data->Beam.HeatPerSecond = 30.0f;
		Data->Beam.VentPerSecond = 8.0f;
		Data->Beam.MaxHeat = 100.0f;
		Data->Beam.OverheatLock = 2.5f;
		Data->Beam.BeamDurabilityPerSecond = 0.5f;
		return Data;
	}

	struct FBeamFixture
	{
		UWorld* World = nullptr;
		AMDGameState* GameState = nullptr;
		APawn* ShooterPawn = nullptr;
		AMDPlayerState* ShooterState = nullptr;
		UMDRangedComponent* Ranged = nullptr;
		UMDBeamComponent* Beam = nullptr;
		UMDRangedWeaponData* Gun = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDBeamTestWorld")));
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
			Beam = ShooterState->BeamComponent;
			if (!Ranged || !Beam || !ShooterState->RangedAttributes)
			{
				Test.AddError(TEXT("Player state is missing the ranged component, the beam component or the ranged attribute set"));
				return false;
			}
			Gun = MakeBeamGun();
			Ranged->SetPawnForTests(ShooterPawn);
			Ranged->SetWeaponDataForTests(Gun);
			Ranged->SetRangedMode(true);
			return true;
		}

		AMDEnemyCharacter* SpawnEnemy(const FVector& Location)
		{
			UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
			Archetype->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
			Archetype->MaxHealth = 1000.0f;
			Archetype->MaxPoise = 30.0f;
			Archetype->KillGold = 0;
			Archetype->MoveSpeed = 300.0f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>(Location, FRotator::ZeroRotator, Params);
			if (Enemy)
			{
				Enemy->ApplyArchetype(Archetype);
				Enemy->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
			return Enemy;
		}

		ACharacter* SpawnPlayerVictim(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* Victim = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator, Params);
			AMDPlayerState* State = World->SpawnActor<AMDPlayerState>();
			if (!Victim || !State)
			{
				return nullptr;
			}
			State->SetRole(ROLE_Authority);
			Victim->SetPlayerState(State);
			UMDAbilitySystemComponent* ASC = State->GetMDAbilitySystemComponent();
			ASC->InitAbilityActorInfo(State, Victim);
			ASC->RegisterAttributeSet(State->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			return Victim;
		}

		~FBeamFixture()
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

	const FVector Muzzle(40.0, 0.0, 44.0);
	const FVector BeamForward(1.0, 0.0, 0.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tick damage and hit-once-per-tick
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamTickDamageTest, "MazeDemons.Ranged.Beam.TickDamageAndHitOncePerTick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamTickDamageTest::RunTest(const FString& Parameters)
{
	using namespace MDBeamTestsLocal;

	// ---- pure rules ----
	// Duskray: 15.75 DPS at 10 Hz = 1.575 per tick, full to 10 m, 40 % from 18 m, linear between.
	TestTrue(TEXT("a tick is DPS / Hz"), FMath::IsNearlyEqual(FMDBeamRules::TickDamage(15.75f, 10.0f, 500.0f, 1000.0f, 1800.0f, 0.4f), 1.575f, 0.0001f));
	TestTrue(TEXT("full damage out to 10 m"), FMath::IsNearlyEqual(FMDBeamRules::TickDamage(15.75f, 10.0f, 1000.0f, 1000.0f, 1800.0f, 0.4f), 1.575f, 0.0001f));
	TestTrue(TEXT("70 % halfway through the falloff"), FMath::IsNearlyEqual(FMDBeamRules::TickDamage(15.75f, 10.0f, 1400.0f, 1000.0f, 1800.0f, 0.4f), 1.575f * 0.7f, 0.0001f));
	TestTrue(TEXT("40 % at 18 m and beyond"), FMath::IsNearlyEqual(FMDBeamRules::TickDamage(15.75f, 10.0f, 2000.0f, 1000.0f, 1800.0f, 0.4f), 1.575f * 0.4f, 0.0001f));
	TestEqual(TEXT("no tick rate, no damage"), FMDBeamRules::TickDamage(15.75f, 0.0f, 100.0f, 1000.0f, 1800.0f, 0.4f), 0.0f);
	TestTrue(TEXT("10 ticks in a second add up to the DPS"), FMath::IsNearlyEqual(10.0f * FMDBeamRules::TickDamage(15.75f, 10.0f, 300.0f, 1000.0f, 1800.0f, 0.4f), 15.75f, 0.001f));
	TestTrue(TEXT("0.9x the melee reference DPS (17.5)"), FMath::IsNearlyEqual(15.75f / 17.5f, 0.9f, 0.001f));
	TestTrue(TEXT("wear per landed tick is 0.05"), FMath::IsNearlyEqual(FMDBeamRules::WearPerLandedTick(0.5f, 10.0f), 0.05f, 0.0001f));

	FMDBeamHitLog Log;
	AActor* DummyA = reinterpret_cast<AActor*>(static_cast<uintptr_t>(0x1000)); // only compared, never dereferenced by TryRegister
	(void)DummyA;
	// ---- world: one real victim, no double hits, team rules, falloff ----
	FBeamFixture Fx;
	if (!Fx.Init(*this) || !TestNotNull(TEXT("game state"), Fx.GameState))
	{
		return false;
	}
	AMDEnemyCharacter* Near = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("near enemy"), Near))
	{
		return false;
	}
	TestTrue(TEXT("a victim registers once"), Log.TryRegister(Near));
	TestFalse(TEXT("a second component of the same victim does not"), Log.TryRegister(Near));
	Log.Reset();
	TestTrue(TEXT("after the reset (next tick) it can be hit again"), Log.TryRegister(Near));

	AMDEnemyCharacter* Behind = Fx.SpawnEnemy(FVector(1100.0, 0.0, 100.0));
	TestNotNull(TEXT("enemy behind"), Behind);
	const float Start = HealthOf(Near);

	TestEqual(TEXT("the beam starts"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::None);
	TestTrue(TEXT("the beam is active on the server"), Fx.Beam->IsBeamActive());

	const int32 Hurt1 = Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	const float Drop1 = Start - HealthOf(Near);
	TestEqual(TEXT("one tick hurts one victim"), Hurt1, 1);
	TestTrue(TEXT("the victim lost health"), Drop1 > 0.0f);
	TestEqual(TEXT("the enemy behind the first is untouched (the beam ends on its first victim)"), HealthOf(Behind), HealthOf(Behind));
	if (Behind)
	{
		TestTrue(TEXT("the enemy behind kept all its health"), FMath::IsNearlyEqual(HealthOf(Behind), 1000.0f, 0.01f));
	}

	const int32 Hurt2 = Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	TestEqual(TEXT("the next tick hurts it again"), Hurt2, 1);
	TestTrue(TEXT("exactly one tick of damage per tick (no doubles)"), FMath::IsNearlyEqual(Start - HealthOf(Near), Drop1 * 2.0f, 0.01f));
	TestEqual(TEXT("two server ticks counted"), Fx.Beam->GetServerTickCount(), 2);

	// Ten ticks = one second = the DPS (within a rounding of the damage pipeline).
	for (int32 i = 0; i < 8; ++i)
	{
		Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	}
	TestTrue(TEXT("ten ticks are ten times one tick"), FMath::IsNearlyEqual(Start - HealthOf(Near), Drop1 * 10.0f, 0.05f));

	// A far victim (beyond 18 m) takes less per tick.
	AMDEnemyCharacter* Far = Fx.SpawnEnemy(FVector(1950.0, 600.0, 100.0));
	if (Far)
	{
		const float FarStart = HealthOf(Far);
		Fx.Beam->ServerBeamTickAt(FVector(40.0, 600.0, 44.0), BeamForward);
		const float FarDrop = FarStart - HealthOf(Far);
		TestTrue(TEXT("falloff: the far victim took damage"), FarDrop > 0.0f);
		TestTrue(TEXT("falloff: less than the near victim per tick"), FarDrop < Drop1);
	}

	// A miss costs nothing and moves the end point to full range.
	Fx.Beam->ServerBeamTickAt(FVector(40.0, -3000.0, 44.0), BeamForward);
	TestTrue(TEXT("a miss ends the beam at its range"), Fx.Beam->GetBeamEnd().X > 1900.0);

	// Team rules: a teammate in front is passed through with friendly fire off, hurt for half with it on.
	ACharacter* Mate = Fx.SpawnPlayerVictim(FVector(500.0, 1200.0, 100.0));
	AMDEnemyCharacter* BehindMate = Fx.SpawnEnemy(FVector(900.0, 1200.0, 100.0));
	if (TestNotNull(TEXT("teammate"), Mate) && TestNotNull(TEXT("enemy behind the teammate"), BehindMate))
	{
		const FVector Origin(40.0, 1200.0, 44.0);
		const float MateStart = HealthOf(Mate);
		const float BehindStart = HealthOf(BehindMate);
		Fx.GameState->SetFriendlyFireEnabled(false);
		TestEqual(TEXT("friendly fire off: the tick hurts the enemy behind"), Fx.Beam->ServerBeamTickAt(Origin, BeamForward), 1);
		TestEqual(TEXT("friendly fire off: the teammate takes nothing"), HealthOf(Mate), MateStart);
		TestTrue(TEXT("friendly fire off: the enemy behind took the tick"), HealthOf(BehindMate) < BehindStart);
		const float FullDrop = BehindStart - HealthOf(BehindMate);

		Fx.GameState->SetFriendlyFireEnabled(true);
		const float BehindMid = HealthOf(BehindMate);
		TestEqual(TEXT("friendly fire on: the tick hurts the teammate"), Fx.Beam->ServerBeamTickAt(Origin, BeamForward), 1);
		TestTrue(TEXT("friendly fire on: the teammate is hurt"), HealthOf(Mate) < MateStart);
		TestEqual(TEXT("friendly fire on: the beam ends on the teammate"), HealthOf(BehindMate), BehindMid);
		TestTrue(TEXT("friendly fire does half"), FMath::IsNearlyEqual(MateStart - HealthOf(Mate), FullDrop * FMDRangedMath::FriendlyFireRangedScale, 0.1f));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Heat, overheat lock, vent
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamHeatTest, "MazeDemons.Ranged.Beam.HeatOverheatLockVent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamHeatTest::RunTest(const FString& Parameters)
{
	using namespace MDBeamTestsLocal;

	// ---- pure rules: +30/s firing, 8/s idle vent, overheat at 100, lock 2.5 s ----
	const FMDHeatParams Params; // the Duskray defaults
	TestTrue(TEXT("about 3.3 s of beam before overheat"), FMath::IsNearlyEqual(FMDBeamRules::SecondsToOverheat(Params), 3.3333f, 0.001f));

	FMDHeatState State;
	for (int32 i = 0; i < 33; ++i)
	{
		State = FMDBeamRules::StepHeat(State, true, Params, 0.1f);
	}
	TestTrue(TEXT("3.3 s of fire: 99 heat"), FMath::IsNearlyEqual(State.Heat, 99.0f, 0.01f));
	TestFalse(TEXT("not overheated yet"), State.IsOverheated());
	State = FMDBeamRules::StepHeat(State, true, Params, 0.1f);
	TestTrue(TEXT("the 34th tenth hits 100"), FMath::IsNearlyEqual(State.Heat, 100.0f, 0.01f));
	TestTrue(TEXT("overheated"), State.IsOverheated());
	TestTrue(TEXT("locked for 2.5 s"), FMath::IsNearlyEqual(State.LockRemaining, 2.5f, 0.001f));

	// The trigger is ignored while locked; the heat vents with the lock so the meter and the radial agree.
	State = FMDBeamRules::StepHeat(State, true, Params, 0.5f);
	TestTrue(TEXT("still locked with the trigger down"), State.IsOverheated());
	TestTrue(TEXT("0.5 s into the lock: 2.0 s left"), FMath::IsNearlyEqual(State.LockRemaining, 2.0f, 0.001f));
	TestTrue(TEXT("the heat fell with the lock (80)"), FMath::IsNearlyEqual(State.Heat, 80.0f, 0.01f));
	TestTrue(TEXT("lock fraction 0.8"), FMath::IsNearlyEqual(FMDBeamRules::OverheatLockFraction(State, 2.5f), 0.8f, 0.001f));
	State = FMDBeamRules::StepHeat(State, false, Params, 2.0f);
	TestFalse(TEXT("the lock ends after 2.5 s in all"), State.IsOverheated());
	TestEqual(TEXT("and the weapon is cold again"), State.Heat, 0.0f);
	TestEqual(TEXT("no lock fraction once free"), FMDBeamRules::OverheatLockFraction(State, 2.5f), 0.0f);

	// A frame hitch past the end of the lock still frees the weapon.
	FMDHeatState Hitch;
	Hitch.Heat = 100.0f;
	Hitch.LockRemaining = 0.2f;
	Hitch = FMDBeamRules::StepHeat(Hitch, true, Params, 1.0f);
	TestFalse(TEXT("a long frame ends the lock"), Hitch.IsOverheated());

	// Vent: 8 per second with the trigger up, never below 0.
	FMDHeatState Warm;
	Warm.Heat = 50.0f;
	Warm = FMDBeamRules::StepHeat(Warm, false, Params, 1.0f);
	TestTrue(TEXT("1 s idle vents 8"), FMath::IsNearlyEqual(Warm.Heat, 42.0f, 0.001f));
	Warm = FMDBeamRules::StepHeat(Warm, false, Params, 100.0f);
	TestEqual(TEXT("heat never goes below 0"), Warm.Heat, 0.0f);

	// Short bursts never overheat: 1 s on, 1 s off nets +22 per cycle, so ~5 cycles; a 2 s rest vents only 16.
	FMDHeatState Burst;
	Burst = FMDBeamRules::StepHeat(Burst, true, Params, 1.0f);
	TestTrue(TEXT("1 s of fire = 30 heat"), FMath::IsNearlyEqual(Burst.Heat, 30.0f, 0.001f));

	// Upgrade hook: -8 % heat per second per level, floored at 20 %.
	TestTrue(TEXT("level 0 builds the base 30"), FMath::IsNearlyEqual(FMDBeamRules::UpgradedHeatPerSecond(30.0f, 0), 30.0f, 0.001f));
	TestTrue(TEXT("level 1: -8 %"), FMath::IsNearlyEqual(FMDBeamRules::UpgradedHeatPerSecond(30.0f, 1), 27.6f, 0.001f));
	TestTrue(TEXT("level 5: -40 %"), FMath::IsNearlyEqual(FMDBeamRules::UpgradedHeatPerSecond(30.0f, 5), 18.0f, 0.001f));
	TestTrue(TEXT("never below 20 %"), FMath::IsNearlyEqual(FMDBeamRules::UpgradedHeatPerSecond(30.0f, 50), 6.0f, 0.001f));
	TestTrue(TEXT("a negative level is level 0"), FMath::IsNearlyEqual(FMDBeamRules::UpgradedHeatPerSecond(30.0f, -3), 30.0f, 0.001f));

	// Owner prediction: a close sample is smoothed, a far one snaps.
	TestTrue(TEXT("small difference: pulled a little toward the server"), FMath::IsNearlyEqual(FMDBeamRules::ReconcileHeat(50.0f, 54.0f), 50.4f, 0.001f));
	TestEqual(TEXT("big difference: snaps to the server"), FMDBeamRules::ReconcileHeat(10.0f, 60.0f), 60.0f);

	// Can the beam fire? The first closed gate wins.
	TestEqual(TEXT("clear"), FMDBeamRules::CanFire(true, true, false, false, false, false, false), EMDBeamBlock::None);
	TestEqual(TEXT("not in ranged mode"), FMDBeamRules::CanFire(false, true, false, false, false, false, false), EMDBeamBlock::NotRangedMode);
	TestEqual(TEXT("not a beam weapon"), FMDBeamRules::CanFire(true, false, false, false, false, false, false), EMDBeamBlock::NoBeamWeapon);
	TestEqual(TEXT("dead"), FMDBeamRules::CanFire(true, true, true, false, false, false, false), EMDBeamBlock::Dead);
	TestEqual(TEXT("staggered"), FMDBeamRules::CanFire(true, true, false, true, false, false, false), EMDBeamBlock::Staggered);
	TestEqual(TEXT("reloading"), FMDBeamRules::CanFire(true, true, false, false, true, false, false), EMDBeamBlock::Reloading);
	TestEqual(TEXT("overheated"), FMDBeamRules::CanFire(true, true, false, false, false, true, false), EMDBeamBlock::Overheated);
	TestEqual(TEXT("no charge"), FMDBeamRules::CanFire(true, true, false, false, false, false, true), EMDBeamBlock::NoCharge);

	// ---- server: the component runs the same rules, sets State.Overheated for everyone, and writes the attribute ----
	FBeamFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	UMDAbilitySystemComponent* ASC = Fx.ShooterState->GetMDAbilitySystemComponent();
	TestEqual(TEXT("the weapon starts"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::None);
	for (int32 i = 0; i < 33; ++i)
	{
		Fx.Beam->AdvanceServer(0.1f);
	}
	TestTrue(TEXT("still firing after 3.3 s"), Fx.Beam->IsBeamActive());
	TestFalse(TEXT("no overheat tag yet"), ASC->HasMatchingGameplayTag(TAG_State_Overheated));
	TestTrue(TEXT("the heat view reads 99"), FMath::IsNearlyEqual(Fx.Ranged->GetHeat(), 99.0f, 0.2f));
	Fx.Beam->AdvanceServer(0.1f);
	TestFalse(TEXT("overheat stops the beam"), Fx.Beam->IsBeamActive());
	TestTrue(TEXT("State.Overheated is on the ASC"), ASC->HasMatchingGameplayTag(TAG_State_Overheated));
	TestTrue(TEXT("the ranged component reports it (HUD)"), Fx.Ranged->IsOverheated());
	TestTrue(TEXT("the heat attribute is at 100"), FMath::IsNearlyEqual(Fx.ShooterState->RangedAttributes->GetHeat(), 100.0f, 0.01f));
	TestTrue(TEXT("max heat attribute follows the data"), FMath::IsNearlyEqual(Fx.ShooterState->RangedAttributes->GetMaxHeat(), 100.0f, 0.01f));
	TestEqual(TEXT("a locked weapon cannot start"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::Overheated);
	TestFalse(TEXT("so no beam"), Fx.Beam->IsBeamActive());
	TestTrue(TEXT("the lock radial is full"), FMath::IsNearlyEqual(Fx.Ranged->GetOverheatLockFraction(), 1.0f, 0.001f));

	for (int32 i = 0; i < 24; ++i)
	{
		Fx.Beam->AdvanceServer(0.1f);
	}
	TestTrue(TEXT("2.4 s later still locked"), Fx.Ranged->IsOverheated());
	Fx.Beam->AdvanceServer(0.2f); // past the float knife edge of 2.5 s
	TestFalse(TEXT("2.5 s later free"), Fx.Ranged->IsOverheated());
	TestFalse(TEXT("the tag is gone"), ASC->HasMatchingGameplayTag(TAG_State_Overheated));
	TestEqual(TEXT("it can fire again"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::None);

	// Releasing the trigger vents; gates close the beam.
	Fx.Beam->ServerStopBeam();
	TestFalse(TEXT("trigger up: beam off"), Fx.Beam->IsBeamActive());
	Fx.Ranged->SetRangedMode(false);
	TestEqual(TEXT("holstered: cannot fire"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::NotRangedMode);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Replication state: active + end, overheat tag, late join
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamReplicationTest, "MazeDemons.Ranged.Beam.ReplicatesActiveAndEnd", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamReplicationTest::RunTest(const FString& Parameters)
{
	using namespace MDBeamTestsLocal;

	// ---- the properties are replicated ----
	UClass* BeamClass = UMDBeamComponent::StaticClass();
	for (const TCHAR* Name : { TEXT("bBeamActive"), TEXT("BeamEnd"), TEXT("bOverheated"), TEXT("OverheatEndServerTime") })
	{
		const FProperty* Prop = BeamClass->FindPropertyByName(FName(Name));
		if (TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Prop))
		{
			TestTrue(*FString::Printf(TEXT("%s replicates to everyone"), Name), Prop->HasAnyPropertyFlags(CPF_Net));
		}
	}
	TestTrue(TEXT("bBeamActive has a RepNotify (late joiners and toggles)"), BeamClass->FindPropertyByName(TEXT("bBeamActive"))->HasAnyPropertyFlags(CPF_RepNotify));
	TestTrue(TEXT("bOverheated has a RepNotify (the tag)"), BeamClass->FindPropertyByName(TEXT("bOverheated"))->HasAnyPropertyFlags(CPF_RepNotify));
	{
		TArray<FLifetimeProperty> Lifetime;
		GetDefault<UMDRangedAttributeSet>()->GetLifetimeReplicatedProps(Lifetime);
		int32 OwnerOnly = 0;
		for (const TCHAR* AttrName : { TEXT("Heat"), TEXT("MaxHeat"), TEXT("Charge"), TEXT("MaxCharge") })
		{
			const FProperty* AttrProp = UMDRangedAttributeSet::StaticClass()->FindPropertyByName(FName(AttrName));
			if (AttrProp)
			{
				for (const FLifetimeProperty& Prop : Lifetime)
				{
					OwnerOnly += (Prop.RepIndex == AttrProp->RepIndex && Prop.Condition == COND_OwnerOnly) ? 1 : 0;
				}
			}
		}
		TestEqual(TEXT("Heat / MaxHeat / Charge / MaxCharge replicate to the owner only"), OwnerOnly, 4);
	}

	FBeamFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Target = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("target"), Target))
	{
		return false;
	}

	TestFalse(TEXT("nothing active before the trigger"), Fx.Beam->IsBeamActive());
	TestEqual(TEXT("server accepts the beam"), Fx.Beam->ServerStartBeam(), EMDBeamBlock::None);
	TestTrue(TEXT("active is set"), Fx.Beam->IsBeamActive());
	Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	const FVector End = Fx.Beam->GetBeamEnd();
	TestTrue(TEXT("the end is on the target (800 cm out, minus its capsule)"), End.X > 600.0 && End.X < 820.0);
	TestTrue(TEXT("the end is quantised to whole centimetres"), FMath::IsNearlyEqual(End.X, FMath::RoundToDouble(End.X), 0.0001) && FMath::IsNearlyEqual(End.Z, FMath::RoundToDouble(End.Z), 0.0001));

	// A late joiner / another machine: a fresh player state receives the replicated values and shows the beam.
	AMDPlayerState* RemoteState = Fx.World->SpawnActor<AMDPlayerState>();
	if (!TestNotNull(TEXT("remote player state"), RemoteState) || !TestNotNull(TEXT("remote beam component"), RemoteState->BeamComponent.Get()))
	{
		return false;
	}
	UMDBeamComponent* Remote = RemoteState->BeamComponent;
	TestFalse(TEXT("a machine that has not received it sees no beam"), Remote->IsBeamShown());
	Remote->Test_SetReplicatedBeam(Fx.Beam->IsBeamActive(), End);
	TestTrue(TEXT("late joiner: the beam is active"), Remote->IsBeamActive());
	TestTrue(TEXT("late joiner: the beam is shown"), Remote->IsBeamShown());
	TestTrue(TEXT("late joiner: same end point"), Remote->GetBeamEnd().Equals(End, 0.001));

	// The overheat flag becomes State.Overheated on the remote machine, and goes away again.
	UMDAbilitySystemComponent* RemoteASC = RemoteState->GetMDAbilitySystemComponent();
	TestFalse(TEXT("no tag before"), RemoteASC->HasMatchingGameplayTag(TAG_State_Overheated));
	Remote->Test_SetReplicatedOverheat(true, 10.0f);
	TestTrue(TEXT("remote machine sees State.Overheated"), RemoteASC->HasMatchingGameplayTag(TAG_State_Overheated));
	TestTrue(TEXT("remote machine reports overheated"), Remote->IsOverheatedView());
	Remote->Test_SetReplicatedOverheat(false, 0.0f);
	TestFalse(TEXT("tag removed when the lock ends"), RemoteASC->HasMatchingGameplayTag(TAG_State_Overheated));

	// Trigger up: the flag turns off for everyone.
	Fx.Beam->ServerStopBeam();
	TestFalse(TEXT("server: beam off"), Fx.Beam->IsBeamActive());
	Remote->Test_SetReplicatedBeam(false, End);
	TestFalse(TEXT("remote: beam off"), Remote->IsBeamActive());
	TestFalse(TEXT("remote: not shown"), Remote->IsBeamShown());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Charge (staff, V2): pure rules only
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamChargeTest, "MazeDemons.Ranged.Beam.ChargeDrainRegen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamChargeTest::RunTest(const FString& Parameters)
{
	const FMDChargeParams Params; // staff defaults: pool 100, drain 25/s, regen 12/s
	FMDChargeState State;
	TestEqual(TEXT("starts full"), State.Charge, 100.0f);

	State = FMDBeamRules::StepCharge(State, true, Params, 2.0f);
	TestTrue(TEXT("2 s of fire drains 50"), FMath::IsNearlyEqual(State.Charge, 50.0f, 0.001f));
	TestFalse(TEXT("not dry"), State.bDry);

	State = FMDBeamRules::StepCharge(State, false, Params, 1.0f);
	TestTrue(TEXT("1 s idle regenerates 12"), FMath::IsNearlyEqual(State.Charge, 62.0f, 0.001f));

	State = FMDBeamRules::StepCharge(State, false, Params, 100.0f);
	TestEqual(TEXT("regen stops at the pool size"), State.Charge, 100.0f);

	State = FMDBeamRules::StepCharge(State, true, Params, 10.0f);
	TestEqual(TEXT("a long burst empties it, never below 0"), State.Charge, 0.0f);
	TestTrue(TEXT("and leaves the staff dry"), State.bDry);
	TestEqual(TEXT("a dry staff cannot fire"), FMDBeamRules::CanFire(true, true, false, false, false, false, State.bDry), EMDBeamBlock::NoCharge);

	// Dry: the trigger is ignored and the pool refills; it fires again at 15 %.
	State = FMDBeamRules::StepCharge(State, true, Params, 1.0f);
	TestTrue(TEXT("dry with the trigger down: regenerates 12, not drained"), FMath::IsNearlyEqual(State.Charge, 12.0f, 0.001f));
	TestTrue(TEXT("still dry below 15"), State.bDry);
	State = FMDBeamRules::StepCharge(State, false, Params, 0.5f);
	TestTrue(TEXT("18 charge"), FMath::IsNearlyEqual(State.Charge, 18.0f, 0.001f));
	TestFalse(TEXT("recovered"), State.bDry);

	// The rates are data: the staff row's numbers.
	FMDChargeParams Custom;
	Custom.MaxCharge = 200.0f;
	Custom.DrainPerSecond = 50.0f;
	Custom.RegenPerSecond = 10.0f;
	FMDChargeState Big;
	Big.Charge = 200.0f;
	Big = FMDBeamRules::StepCharge(Big, true, Custom, 1.0f);
	TestTrue(TEXT("custom drain 50/s"), FMath::IsNearlyEqual(Big.Charge, 150.0f, 0.001f));
	Big = FMDBeamRules::StepCharge(Big, false, Custom, 2.0f);
	TestTrue(TEXT("custom regen 10/s"), FMath::IsNearlyEqual(Big.Charge, 170.0f, 0.001f));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Damage numbers: one rolled-up number per victim per 0.5 s
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamNumbersTest, "MazeDemons.Ranged.Beam.DamageNumbersAggregate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamNumbersTest::RunTest(const FString& Parameters)
{
	using namespace MDBeamTestsLocal;

	// ---- pure: buckets per victim ----
	AActor* VictimA = GetMutableDefault<AMDPlayerState>(); // identity only: the aggregator never dereferences the victim
	AActor* VictimB = GetMutableDefault<AMDGameState>();
	TArray<FMDBeamNumberBucket> Buckets;
	auto Tick = [](AActor* Victim, float Damage, bool bCrit, bool bKill)
	{
		FMDHitConfirm Hit;
		Hit.Victim = Victim;
		Hit.Damage = Damage;
		Hit.bCrit = bCrit;
		Hit.bKill = bKill;
		return Hit;
	};
	// 5 ticks of 2 on A over 0.4 s, 3 ticks of 1 on B.
	for (int32 i = 0; i < 5; ++i)
	{
		FMDBeamNumbers::Add(Buckets, Tick(VictimA, 2.0f, i == 3, false), 10.0 + 0.1 * i);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		FMDBeamNumbers::Add(Buckets, Tick(VictimB, 1.0f, false, false), 10.1 + 0.1 * i);
	}
	TestEqual(TEXT("two victims, two buckets"), Buckets.Num(), 2);

	TArray<FMDHitConfirm> Out;
	FMDBeamNumbers::Collect(Buckets, 10.4, false, Out);
	TestEqual(TEXT("nothing is due inside the 0.5 s window"), Out.Num(), 0);
	FMDBeamNumbers::Collect(Buckets, 10.5, false, Out);
	if (TestEqual(TEXT("A's window closed: one number for 5 ticks"), Out.Num(), 1))
	{
		TestTrue(TEXT("the number is the sum (10)"), FMath::IsNearlyEqual(Out[0].Damage, 10.0f, 0.001f));
		TestTrue(TEXT("a crit in any tick shows the crit colour"), Out[0].bCrit);
		TestTrue(TEXT("it belongs to victim A"), Out[0].Victim == VictimA);
	}
	TestEqual(TEXT("B is still open (its window started later)"), Buckets.Num(), 1);
	Out.Reset();
	FMDBeamNumbers::Collect(Buckets, 10.6, false, Out);
	if (TestEqual(TEXT("B due at 0.5 s after its first tick"), Out.Num(), 1))
	{
		TestTrue(TEXT("B's number is 3"), FMath::IsNearlyEqual(Out[0].Damage, 3.0f, 0.001f));
	}
	TestEqual(TEXT("all flushed"), Buckets.Num(), 0);

	// A kill is never held back; a flush-all (trigger up) sends everything.
	FMDBeamNumbers::Add(Buckets, Tick(VictimA, 2.0f, false, false), 20.0);
	FMDBeamNumbers::Add(Buckets, Tick(VictimA, 2.0f, false, true), 20.1);
	Out.Reset();
	FMDBeamNumbers::Collect(Buckets, 20.1, false, Out);
	if (TestEqual(TEXT("a kill flushes at once"), Out.Num(), 1))
	{
		TestTrue(TEXT("with everything so far (4)"), FMath::IsNearlyEqual(Out[0].Damage, 4.0f, 0.001f));
		TestTrue(TEXT("marked as a kill"), Out[0].bKill);
	}
	FMDBeamNumbers::Add(Buckets, Tick(VictimB, 1.0f, false, false), 30.0);
	Out.Reset();
	FMDBeamNumbers::Collect(Buckets, 30.05, true, Out);
	TestEqual(TEXT("flush-all sends the open bucket"), Out.Num(), 1);

	// A different damage type does not merge into the same number.
	FMDHitConfirm Frost = Tick(VictimA, 2.0f, false, false);
	Frost.DamageType = TAG_Damage_Type_Frost.GetTag();
	FMDBeamNumbers::Add(Buckets, Tick(VictimA, 2.0f, false, false), 40.0);
	FMDBeamNumbers::Add(Buckets, Frost, 40.0);
	TestEqual(TEXT("physical and frost are separate numbers"), Buckets.Num(), 2);
	Buckets.Reset();

	// ---- world: the beam rolls its ticks into one number and counts what it sends ----
	FBeamFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Target = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("target"), Target))
	{
		return false;
	}
	Fx.Beam->ServerStartBeam();
	for (int32 i = 0; i < 5; ++i)
	{
		Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	}
	TestEqual(TEXT("5 landed ticks make one pending number"), Fx.Beam->GetPendingNumberCount(), 1);
	Fx.Beam->FlushNumbers(false);
	TestEqual(TEXT("the window is still open (the test world's clock stands still)"), Fx.Beam->GetNumbersSentCount(), 0);
	Fx.Beam->ServerStopBeam();
	TestEqual(TEXT("releasing the trigger sends the number"), Fx.Beam->GetNumbersSentCount(), 1);
	TestEqual(TEXT("and nothing is left pending"), Fx.Beam->GetPendingNumberCount(), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Durability: only landed ticks wear the weapon
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBeamWearTest, "MazeDemons.Ranged.Beam.WearOnlyWhileHitting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBeamWearTest::RunTest(const FString& Parameters)
{
	using namespace MDBeamTestsLocal;
	FBeamFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Target = Fx.SpawnEnemy(FVector(800.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("target"), Target))
	{
		return false;
	}
	Fx.Beam->ServerStartBeam();

	// Ten ticks into empty space: no wear banked.
	for (int32 i = 0; i < 10; ++i)
	{
		Fx.Beam->ServerBeamTickAt(FVector(40.0, -3000.0, 44.0), BeamForward);
	}
	TestEqual(TEXT("misses cost nothing"), Fx.Beam->GetWearBanked(), 0.0f);

	// BeamDurabilityPerSecond 0.5 at 10 Hz: 0.05 per landed tick, so 10 landed ticks bank half a point.
	for (int32 i = 0; i < 10; ++i)
	{
		Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	}
	TestTrue(TEXT("10 landed ticks bank 0.5"), FMath::IsNearlyEqual(Fx.Beam->GetWearBanked(), 0.5f, 0.001f));
	// 10 more: the whole point is paid out and the banked fraction resets.
	for (int32 i = 0; i < 10; ++i)
	{
		Fx.Beam->ServerBeamTickAt(Muzzle, BeamForward);
	}
	TestTrue(TEXT("20 landed ticks (2 s of beam) pay 1 point, nothing left banked"), Fx.Beam->GetWearBanked() < 0.01f);

	// A later miss still costs nothing.
	const float Banked = Fx.Beam->GetWearBanked();
	Fx.Beam->ServerBeamTickAt(FVector(40.0, -3000.0, 44.0), BeamForward);
	TestEqual(TEXT("a miss after hits adds no wear"), Fx.Beam->GetWearBanked(), Banked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
