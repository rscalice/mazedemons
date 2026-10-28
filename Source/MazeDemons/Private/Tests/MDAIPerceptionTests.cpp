
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDAIQueries.h"
#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDPerceptionRules.h"
#include "AI/MDRallyPoint.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"
#include "MDQuestTypes.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

// V1 WP3 tests (MazeDemons.AI.Perception.*, .Queries.*, .Perf.*, .Dormancy.*, .Nav.*). The rules (MDPerceptionRules.h) and the
// point queries (MDAIQueries.h) are plain functions and run with no world or navmesh (a mock FMDQueryEnvironment); the
// component tests use a transient world with real enemies and drive the brains with their own clocks.

namespace MDAIPerceptionTestsLocal
{
	// A console variable (int) set for the length of a test.
	struct FScopedIntCVar
	{
		IConsoleVariable* Var = nullptr;
		int32 Old = 0;

		FScopedIntCVar(const TCHAR* Name, int32 Value)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Var)
			{
				Old = Var->GetInt();
				Var->Set(Value, ECVF_SetByCode);
			}
		}
		~FScopedIntCVar()
		{
			if (Var)
			{
				Var->Set(Old, ECVF_SetByCode);
			}
		}
	};

	USceneComponent* MakeRoot(AActor* Actor)
	{
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		return Root;
	}

	AActor* SpawnMarker(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (Actor)
		{
			MakeRoot(Actor);
			Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Actor;
	}

	UMDEnemyArchetype* MakeGhoul()
	{
		UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
		Archetype->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
		Archetype->MaxHealth = 60.0f;
		Archetype->MaxPoise = 30.0f;
		Archetype->KillGold = 5;
		Archetype->MoveSpeed = 520.0f;
		FMDEnemyAttack& Swipe = Archetype->Attacks.AddDefaulted_GetRef();
		Swipe.Name = TEXT("Swipe");
		Swipe.Range = 220.0f;
		Swipe.TellTime = 0.8f;
		return Archetype;
	}

	// Transient world: one player-team pawn (AMDPlayerState) and N authoritative enemies with brains.
	struct FWorldFx
	{
		UWorld* World = nullptr;
		APawn* Player = nullptr;
		AMDPlayerState* PlayerState = nullptr;
		TArray<AMDEnemyCharacter*> Enemies;
		UMDEnemyArchetype* Archetype = nullptr;
		bool bFlying = false;

		bool Init(FAutomationTestBase& Test, int32 NumEnemies, bool bInFlying = false)
		{
			bFlying = bInFlying;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIPerceptionTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Player = World->SpawnActor<APawn>();
			PlayerState = World->SpawnActor<AMDPlayerState>();
			if (!Player || !PlayerState)
			{
				Test.AddError(TEXT("Could not spawn the player pawn or its player state"));
				return false;
			}
			Player->SetRootComponent(MakeRoot(Player)); // a bare APawn has no root: it could not be moved
			PlayerState->SetRole(ROLE_Authority);
			Player->SetPlayerState(PlayerState);
			UMDAbilitySystemComponent* PlayerASC = PlayerState->GetMDAbilitySystemComponent();
			if (!PlayerASC)
			{
				Test.AddError(TEXT("Player state has no ASC"));
				return false;
			}
			PlayerASC->InitAbilityActorInfo(PlayerState, Player);
			PlayerASC->RegisterAttributeSet(PlayerState->Attributes);
			PlayerASC->InitializeStats(100.0f, 10.0f, 0.0f);

			Archetype = MakeGhoul();
			for (int32 i = 0; i < NumEnemies; ++i)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>(AMDEnemyCharacter::StaticClass(), FTransform::Identity, SpawnParams);
				if (!Enemy)
				{
					Test.AddError(TEXT("Could not spawn an enemy"));
					return false;
				}
				Enemy->ApplyArchetype(Archetype);
				if (bFlying && Enemy->GetCharacterMovement())
				{
					Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying); // no floor in the test world
				}
				Enemies.Add(Enemy);
			}
			return true;
		}

		UMDEnemyBrainComponent* Brain(int32 Index) const
		{
			const AMDEnemyController* Controller = Enemies.IsValidIndex(Index) ? Cast<AMDEnemyController>(Enemies[Index]->GetController()) : nullptr;
			return Controller ? Controller->GetBrain() : nullptr;
		}

		void Place(AActor* Actor, const FVector& Location) const
		{
			if (Actor)
			{
				Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		TArray<AActor*> EnemyActors(int32 First, int32 Count) const
		{
			TArray<AActor*> Out;
			for (int32 i = First; i < First + Count && i < Enemies.Num(); ++i)
			{
				Out.Add(Enemies[i]);
			}
			return Out;
		}

		void TickFrames(int32 NumFrames, float Dt)
		{
			for (int32 i = 0; i < NumFrames; ++i)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
		}

		~FWorldFx()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				if (World->IsRooted())
				{
					World->RemoveFromRoot();
				}
				World = nullptr;
			}
		}
	};

	bool KnowsPlayer(const UMDEnemyBrainComponent* Brain, const AActor* Player, FVector& OutLocation, bool& bOutSensedNow)
	{
		return Brain && Brain->GetKnownLocation(Player, OutLocation, bOutSensedNow);
	}

	float AngleBehind(const FVector& Target, const FVector& Facing, const FVector& Point)
	{
		return MDAIQueries::AngleFromFacing(Target, Facing, Point);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Rules (no world)
// ---------------------------------------------------------------------------------------------------------------------

// Noise profiles, the hearing range per tier, what is shared, what is live, and the share timing formula.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPerceptionRulesTest, "MazeDemons.AI.Perception.HearingAndNoiseRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPerceptionRulesTest::RunTest(const FString& Parameters)
{
	const MDNoise::FProfile Attack = MDNoise::ProfileFor(FName(MDNoise::Attack));
	const MDNoise::FProfile Sprint = MDNoise::ProfileFor(FName(MDNoise::Sprint));
	const MDNoise::FProfile Gunshot = MDNoise::ProfileFor(FName(MDNoise::Gunshot));
	const MDNoise::FProfile Other = MDNoise::ProfileFor(FName(TEXT("SomethingElse")));
	TestTrue(TEXT("a gunshot is louder than a melee step"), Gunshot.Loudness > Attack.Loudness);
	TestTrue(TEXT("a melee step is louder than sprint footfalls"), Attack.Loudness > Sprint.Loudness);
	TestEqual(TEXT("an unknown tag is a plain noise"), Other.Loudness, 1.0f);
	TestTrue(TEXT("a gunshot carries furthest"), Gunshot.MaxRange > Attack.MaxRange && Attack.MaxRange > Sprint.MaxRange);
	TestTrue(TEXT("only the gunshot is loud"), MDNoise::IsLoud(FName(MDNoise::Gunshot)) && !MDNoise::IsLoud(FName(MDNoise::Attack)) && !MDNoise::IsLoud(FName(MDNoise::Sprint)));

	TestEqual(TEXT("T0 hearing range 12 m"), MDPerception::HearingRange(EMDIntelligenceTier::T0), 1200.0f);
	TestEqual(TEXT("T4 hearing range 24 m"), MDPerception::HearingRange(EMDIntelligenceTier::T4), 2400.0f);
	float Previous = 0.0f;
	for (int32 T = 0; T <= 4; ++T)
	{
		const float Range = MDPerception::HearingRange(static_cast<EMDIntelligenceTier>(T));
		TestTrue(*FString::Printf(TEXT("hearing range grows with the tier (T%d)"), T), Range > Previous);
		Previous = Range;
	}

	TestTrue(TEXT("sight is shared"), MDPerception::ShouldShare(EMDStimulusKind::Sight, NAME_None));
	TestTrue(TEXT("a gunshot is shared"), MDPerception::ShouldShare(EMDStimulusKind::Hearing, FName(MDNoise::Gunshot)));
	TestFalse(TEXT("a melee step is not shared"), MDPerception::ShouldShare(EMDStimulusKind::Hearing, FName(MDNoise::Attack)));
	TestFalse(TEXT("a sprint is not shared"), MDPerception::ShouldShare(EMDStimulusKind::Hearing, FName(MDNoise::Sprint)));
	TestFalse(TEXT("damage is not shared"), MDPerception::ShouldShare(EMDStimulusKind::Damage, NAME_None));
	TestFalse(TEXT("a shared sighting is not re-shared"), MDPerception::ShouldShare(EMDStimulusKind::Shared, NAME_None));

	TestTrue(TEXT("sight is live"), MDPerception::IsLive(EMDStimulusKind::Sight));
	TestFalse(TEXT("hearing is last-known only"), MDPerception::IsLive(EMDStimulusKind::Hearing));
	TestFalse(TEXT("damage is last-known only"), MDPerception::IsLive(EMDStimulusKind::Damage));
	TestFalse(TEXT("shared is last-known only"), MDPerception::IsLive(EMDStimulusKind::Shared));

	TestEqual(TEXT("share arrives after the share delay"), MDPerception::SharedArrivalTime(10.0, 2.0f, 0.9f), 12.0);
	TestEqual(TEXT("the reaction floor holds for shared knowledge"), MDPerception::SharedArrivalTime(10.0, 0.25f, 0.3f), 10.3);
	TestTrue(TEXT("T0 (negative delay) never shares"), MDPerception::SharedArrivalTime(10.0, -1.0f, 1.3f) < 0.0);
	TestTrue(TEXT("never shares gives a negative queue time"), MDPerception::SharedQueueTime(10.0, -1.0f, 1.3f) < 0.0);
	TestEqual(TEXT("queue time + reaction = arrival"), MDPerception::SharedQueueTime(10.0, 2.0f, 0.9f) + 0.9, 12.0, 1e-6);

	TestEqual(TEXT("lose sight 3 s"), MDPerception::LoseSightTime, 3.0);

	// The tier table in the spec: sight radius / FOV T0 15 m / 110 ... T4 30 m / 160.
	const FMDTierParams T0 = FMDTierParams::ForTier(EMDIntelligenceTier::T0);
	const FMDTierParams T4 = FMDTierParams::ForTier(EMDIntelligenceTier::T4);
	TestEqual(TEXT("T0 sight radius"), T0.SightRadius, 1500.0f);
	TestEqual(TEXT("T0 sight FOV"), T0.SightFOV, 110.0f);
	TestEqual(TEXT("T4 sight radius"), T4.SightRadius, 3000.0f);
	TestEqual(TEXT("T4 sight FOV"), T4.SightFOV, 160.0f);
	TestTrue(TEXT("T0 never shares"), T0.ShareDelay < 0.0f);
	TestEqual(TEXT("T4 share delay 0.25 s"), T4.ShareDelay, 0.25f);
	TestEqual(TEXT("T1 share delay 2 s"), FMDTierParams::ForTier(EMDIntelligenceTier::T1).ShareDelay, 2.0f);
	return true;
}

// Dormancy, net frequency, repath and the per-frame path budget rules.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAINavRulesTest, "MazeDemons.AI.Nav.RepathAndPathBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAINavRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no player known: never dormant"), MDDormancy::ShouldSleep(-1.0f, -1.0));
	TestFalse(TEXT("inside 40 m: awake"), MDDormancy::ShouldSleep(3900.0f, -1.0));
	TestTrue(TEXT("beyond 40 m: asleep"), MDDormancy::ShouldSleep(4100.0f, -1.0));
	TestFalse(TEXT("beyond 40 m but just hurt: awake"), MDDormancy::ShouldSleep(4100.0f, 2.0));
	TestTrue(TEXT("beyond 40 m, hurt long ago: asleep"), MDDormancy::ShouldSleep(4100.0f, 30.0));
	TestEqual(TEXT("full net rate inside 25 m"), MDDormancy::NetUpdateFrequencyFor(2000.0f, 60.0f), 60.0f);
	TestEqual(TEXT("reduced net rate beyond 25 m"), MDDormancy::NetUpdateFrequencyFor(3000.0f, 60.0f), MDDormancy::ReducedNetFrequency);
	TestEqual(TEXT("full net rate with no player known"), MDDormancy::NetUpdateFrequencyFor(-1.0f, 60.0f), 60.0f);

	const FVector Goal(1000.0, 0.0, 0.0);
	TestTrue(TEXT("no move running: request one"), MDNav::ShouldRepath(false, Goal, Goal));
	TestFalse(TEXT("goal moved 1 m: keep the path"), MDNav::ShouldRepath(true, Goal, Goal + FVector(100.0, 0.0, 0.0)));
	TestTrue(TEXT("goal moved 2 m: repath"), MDNav::ShouldRepath(true, Goal, Goal + FVector(200.0, 0.0, 0.0)));
	TestFalse(TEXT("height alone never repaths"), MDNav::ShouldRepath(true, Goal, Goal + FVector(0.0, 0.0, 1000.0)));
	TestEqual(TEXT("repath threshold 1.5 m"), MDNav::RepathDistance, 150.0f);

	MDNav::FPathBudget Budget;
	int32 Granted = 0;
	for (int32 i = 0; i < 25; ++i)
	{
		Granted += Budget.TryConsume(100) ? 1 : 0;
	}
	TestEqual(TEXT("10 path requests per frame"), Granted, 10);
	TestEqual(TEXT("the rest are denied"), Budget.GetDenied(), 15);
	TestTrue(TEXT("the next frame has a fresh budget"), Budget.TryConsume(101));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Perception: reaction floor and shared last-known position
// ---------------------------------------------------------------------------------------------------------------------

// A sees the player; the pack mates learn the LAST-KNOWN position after ShareDelay (not before the reaction floor), even after
// the player moves. T0 never shares. A gunshot is shared, a melee step is not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPerceptionShareDelayTest, "MazeDemons.AI.Perception.ReactionAndShareDelay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPerceptionShareDelayTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;

	// One pack of three at the given tier; the first enemy reports `Tag` (sight when NAME_None) at world time 0.
	auto RunScenario = [this](int32 TierValue, FName Tag, auto&& Check)
	{
		FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), TierValue);
		FWorldFx F;
		if (!F.Init(*this, 3))
		{
			return;
		}
		UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
		if (!TestNotNull(TEXT("pack director"), Director))
		{
			return;
		}
		Director->RegisterSpawnGroup(F.EnemyActors(0, 3), 0, NAME_None);
		F.Place(F.Player, FVector(0.0, 0.0, 0.0));
		F.Place(F.Enemies[0], FVector(900.0, 0.0, 0.0));
		F.Place(F.Enemies[1], FVector(1500.0, 300.0, 0.0));
		F.Place(F.Enemies[2], FVector(1500.0, -300.0, 0.0));
		UMDEnemyBrainComponent* A = F.Brain(0);
		if (!TestNotNull(TEXT("brain A"), A))
		{
			return;
		}
		const FVector SeenAt(0.0, 0.0, 0.0);
		if (Tag.IsNone())
		{
			A->ReportStimulus(F.Player, true, SeenAt);
		}
		else
		{
			A->ReportHeard(F.Player, SeenAt, Tag);
		}
		Check(F, A, SeenAt);
	};

	// ---- T2: reaction floor for the seer, ShareDelay for the pack ----
	{
		const FMDTierParams P = FMDTierParams::ForTier(EMDIntelligenceTier::T2);
		const double Arrival = MDPerception::SharedArrivalTime(0.0, P.ShareDelay, P.ReactionTime);
		RunScenario(2, NAME_None, [&](FWorldFx& F, UMDEnemyBrainComponent* A, const FVector& SeenAt)
		{
			FVector Loc;
			bool bNow = false;
			A->RunDecision(P.ReactionTime - 0.05);
			TestFalse(TEXT("T2: the seer does not know the player before its reaction time"), KnowsPlayer(A, F.Player, Loc, bNow));
			A->RunDecision(P.ReactionTime + 0.02);
			TestTrue(TEXT("T2: the seer knows the player after its reaction time"), KnowsPlayer(A, F.Player, Loc, bNow));
			TestTrue(TEXT("T2: the seer sees the player live"), bNow);

			for (int32 Mate = 1; Mate <= 2; ++Mate)
			{
				UMDEnemyBrainComponent* B = F.Brain(Mate);
				if (!TestNotNull(TEXT("brain B"), B))
				{
					return;
				}
				B->RunDecision(Arrival - 0.05);
				TestFalse(TEXT("T2: a pack mate does not know before ShareDelay"), KnowsPlayer(B, F.Player, Loc, bNow));
			}
			// The player runs away before the share lands: mates still get the position it was seen at.
			F.Place(F.Player, FVector(-5000.0, 0.0, 0.0));
			for (int32 Mate = 1; Mate <= 2; ++Mate)
			{
				UMDEnemyBrainComponent* B = F.Brain(Mate);
				B->RunDecision(Arrival + 0.02);
				FVector MateLoc;
				bool bMateNow = true;
				if (TestTrue(TEXT("T2: a pack mate knows the player after ShareDelay"), KnowsPlayer(B, F.Player, MateLoc, bMateNow)))
				{
					TestFalse(TEXT("T2: the shared position is last-known, not live"), bMateNow);
					TestTrue(TEXT("T2: it is the position the seer saw, not the live one"), MateLoc.Equals(SeenAt, 1.0));
				}
			}
		});
	}

	// ---- T0 never shares ----
	RunScenario(0, NAME_None, [&](FWorldFx& F, UMDEnemyBrainComponent* A, const FVector&)
	{
		FVector Loc;
		bool bNow = false;
		A->RunDecision(FMDTierParams::ForTier(EMDIntelligenceTier::T0).ReactionTime + 0.02);
		TestTrue(TEXT("T0: the seer knows the player"), KnowsPlayer(A, F.Player, Loc, bNow));
		for (int32 Mate = 1; Mate <= 2; ++Mate)
		{
			F.Brain(Mate)->RunDecision(2.5);
			TestFalse(TEXT("T0: nothing is shared with the pack"), KnowsPlayer(F.Brain(Mate), F.Player, Loc, bNow));
		}
	});

	// ---- T4: the 0.25 s share delay is held back to the reaction floor ----
	{
		const FMDTierParams P = FMDTierParams::ForTier(EMDIntelligenceTier::T4);
		const double Arrival = MDPerception::SharedArrivalTime(0.0, P.ShareDelay, P.ReactionTime);
		TestTrue(TEXT("T4: the share arrives no earlier than the reaction time"), Arrival >= P.ReactionTime - 1e-6);
		RunScenario(4, NAME_None, [&](FWorldFx& F, UMDEnemyBrainComponent*, const FVector&)
		{
			FVector Loc;
			bool bNow = false;
			UMDEnemyBrainComponent* B = F.Brain(1);
			B->RunDecision(Arrival - 0.03);
			TestFalse(TEXT("T4: a mate does not know before the reaction floor"), KnowsPlayer(B, F.Player, Loc, bNow));
			B->RunDecision(Arrival + 0.03);
			TestTrue(TEXT("T4: a mate knows right after the reaction floor"), KnowsPlayer(B, F.Player, Loc, bNow));
		});
	}

	// ---- A gunshot is heard and shared as a last-known position; a melee step is heard but not shared ----
	{
		const FMDTierParams P = FMDTierParams::ForTier(EMDIntelligenceTier::T2);
		const double Arrival = MDPerception::SharedArrivalTime(0.0, P.ShareDelay, P.ReactionTime);
		RunScenario(2, FName(MDNoise::Gunshot), [&](FWorldFx& F, UMDEnemyBrainComponent* A, const FVector& At)
		{
			FVector Loc;
			bool bNow = true;
			A->RunDecision(P.ReactionTime + 0.02);
			TestTrue(TEXT("gunshot: the hearer knows where it came from"), KnowsPlayer(A, F.Player, Loc, bNow));
			TestFalse(TEXT("gunshot: hearing gives a last-known position, not a live one"), bNow);
			TestTrue(TEXT("gunshot: the heard tag is kept"), A->GetLastHeardTag() == FName(MDNoise::Gunshot));
			UMDEnemyBrainComponent* B = F.Brain(1);
			B->RunDecision(Arrival + 0.02);
			bNow = true;
			TestTrue(TEXT("gunshot: the pack is alerted"), KnowsPlayer(B, F.Player, Loc, bNow));
			TestTrue(TEXT("gunshot: the mate learns the gunshot position"), Loc.Equals(At, 1.0));
		});
		RunScenario(2, FName(MDNoise::Attack), [&](FWorldFx& F, UMDEnemyBrainComponent* A, const FVector&)
		{
			FVector Loc;
			bool bNow = true;
			A->RunDecision(P.ReactionTime + 0.02);
			TestTrue(TEXT("attack noise: the hearer turns to it"), KnowsPlayer(A, F.Player, Loc, bNow));
			TestTrue(TEXT("attack noise: the heard tag is kept"), A->GetLastHeardTag() == FName(MDNoise::Attack));
			UMDEnemyBrainComponent* B = F.Brain(1);
			B->RunDecision(Arrival + 0.5);
			TestFalse(TEXT("attack noise: a melee step does not alert the pack"), KnowsPlayer(B, F.Player, Loc, bNow));
		});
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Controller: senses per tier, crowd path following
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPerceptionControllerSensesTest, "MazeDemons.AI.Perception.ControllerSensesAndCrowd", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPerceptionControllerSensesTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;
	FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), 4);
	FWorldFx F;
	if (!F.Init(*this, 1))
	{
		return false;
	}
	AMDEnemyController* Controller = Cast<AMDEnemyController>(F.Enemies[0]->GetController());
	if (!TestNotNull(TEXT("enemy controller"), Controller))
	{
		return false;
	}
	TestNotNull(TEXT("the controller follows paths with the crowd component"), Cast<UCrowdFollowingComponent>(Controller->GetPathFollowingComponent()));

	UAIPerceptionComponent* Perception = Controller->GetAIPerceptionComponent();
	if (!TestNotNull(TEXT("perception component"), Perception))
	{
		return false;
	}
	const FMDTierParams T4 = FMDTierParams::ForTier(EMDIntelligenceTier::T4);
	const UAISenseConfig_Sight* Sight = Cast<UAISenseConfig_Sight>(Perception->GetSenseConfig(UAISense::GetSenseID<UAISense_Sight>()));
	if (TestNotNull(TEXT("sight config"), Sight))
	{
		TestEqual(TEXT("T4 sight radius 30 m"), Sight->SightRadius, T4.SightRadius);
		TestEqual(TEXT("sight FOV is the half angle"), Sight->PeripheralVisionAngleDegrees, T4.SightFOV * 0.5f);
		TestTrue(TEXT("lose-sight radius is beyond the sight radius"), Sight->LoseSightRadius > Sight->SightRadius);
		TestEqual(TEXT("sight forgets after 3 s"), Sight->GetMaxAge(), 3.0f);
	}
	const UAISenseConfig_Hearing* Hearing = nullptr;
	for (auto It = Perception->GetSensesConfigIterator(); It; ++It)
	{
		if (const UAISenseConfig_Hearing* Candidate = Cast<UAISenseConfig_Hearing>(*It))
		{
			Hearing = Candidate;
		}
	}
	if (TestNotNull(TEXT("hearing config"), Hearing))
	{
		TestEqual(TEXT("T4 hearing range"), Hearing->HearingRange, MDPerception::HearingRange(EMDIntelligenceTier::T4));
		TestEqual(TEXT("hearing forgets after 3 s"), Hearing->GetMaxAge(), 3.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries (mock environment, no level)
// ---------------------------------------------------------------------------------------------------------------------

// Flank points sit in the band behind the target, on the navmesh, and prefer a spot the target cannot see.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIQueriesFlankTest, "MazeDemons.AI.Queries.FlankPointBehindTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIQueriesFlankTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;
	FMDFlankParams P;
	P.TargetLocation = FVector(0.0, 0.0, 0.0);
	P.TargetFacing = FVector(1.0, 0.0, 0.0);
	P.FromLocation = FVector(600.0, -300.0, 0.0);
	P.Radius = 350.0f;

	TArray<FVector> Candidates;
	MDAIQueries::FlankCandidates(P, Candidates);
	TestTrue(TEXT("candidates exist"), Candidates.Num() > 0);
	for (const FVector& C : Candidates)
	{
		TestTrue(TEXT("every candidate is in the band behind the target"), AngleBehind(P.TargetLocation, P.TargetFacing, C) >= P.MinAngle - 0.5f);
	}

	// Open ground: the point is behind the target, on the ring, and (ties) on the side the enemy comes from.
	FMDQueryEnvironment Open;
	FVector Point;
	if (TestTrue(TEXT("open ground: a flank point is found"), MDAIQueries::FindFlankPoint(P, Open, Point)))
	{
		TestTrue(TEXT("open ground: behind the target (>= 120 degrees off its facing)"), AngleBehind(P.TargetLocation, P.TargetFacing, Point) >= 119.5f);
		TestTrue(TEXT("open ground: on the ring"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(Point, P.TargetLocation)), P.Radius, 5.0f));

	}

	// A wall hides the target's left-behind (Y > 0): that side wins although the enemy is on the other side.
	FMDQueryEnvironment Walled;
	Walled.HasLineOfSight = [](const FVector& From, const FVector& To) { return !(From.Y > 10.0 || To.Y > 10.0); };
	if (TestTrue(TEXT("walled: a flank point is found"), MDAIQueries::FindFlankPoint(P, Walled, Point)))
	{
		TestTrue(TEXT("walled: the point the target cannot see wins"), Point.Y > 10.0);
		TestTrue(TEXT("walled: still behind the target"), AngleBehind(P.TargetLocation, P.TargetFacing, Point) >= 119.5f);
	}

	// Navmesh: only the Y < 0 half exists, and projection drops the height.
	FMDQueryEnvironment Nav;
	Nav.ProjectToNav = [](const FVector& In, FVector& Out)
	{
		Out = FVector(In.X, In.Y, 0.0);
		return In.Y < 0.0;
	};
	if (TestTrue(TEXT("navmesh: a flank point is found"), MDAIQueries::FindFlankPoint(P, Nav, Point)))
	{
		TestTrue(TEXT("navmesh: the point is on the navmesh"), Point.Y < 0.0);
	}
	Nav.ProjectToNav = [](const FVector&, FVector&) { return false; };
	TestFalse(TEXT("no navmesh anywhere: no point"), MDAIQueries::FindFlankPoint(P, Nav, Point));

	// A target facing the other way flips the answer.
	P.TargetFacing = FVector(-1.0, 0.0, 0.0);
	if (TestTrue(TEXT("flipped facing: a flank point is found"), MDAIQueries::FindFlankPoint(P, Open, Point)))
	{
		TestTrue(TEXT("flipped facing: now at +X"), Point.X > 0.0);
		TestTrue(TEXT("flipped facing: behind it"), AngleBehind(P.TargetLocation, P.TargetFacing, Point) >= 119.5f);
	}

	// The melee ring (WP2's slots) is reused: 6 points around the target.
	TArray<FVector> Ring;
	MDAIQueries::RingSlots(FVector::ZeroVector, 0.0f, 250.0f, Ring);
	TestEqual(TEXT("six ring slots"), Ring.Num(), 6);
	for (const FVector& R : Ring)
	{
		TestTrue(TEXT("ring slot on the ring"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(R, FVector::ZeroVector)), 250.0f, 2.0f));
	}
	return true;
}

// Kite points: PreferredRange +- 1.5 m from the target, line of sight to it, 3 m from every player in melee.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIQueriesKiteTest, "MazeDemons.AI.Queries.KitePointRangeAndLOS", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIQueriesKiteTest::RunTest(const FString& Parameters)
{
	FMDKiteParams P;
	P.TargetLocation = FVector::ZeroVector;
	P.FromLocation = FVector(2000.0, 0.0, 0.0);
	P.PreferredRange = 900.0f;

	FMDQueryEnvironment Open;
	FVector Point;
	if (TestTrue(TEXT("open ground: a kite point is found"), MDAIQueries::FindKitePoint(P, Open, Point)))
	{
		const float D = static_cast<float>(FVector::Dist2D(Point, P.TargetLocation));
		TestTrue(TEXT("within PreferredRange +- 1.5 m"), D >= 900.0f - 150.0f - 1.0f && D <= 900.0f + 150.0f + 1.0f);
		TestTrue(TEXT("the point nearest the enemy wins (least running)"), Point.X > 500.0);
	}

	// Line of sight: everything with X > 500 is behind a wall, so the point must come from the clear side.
	FMDQueryEnvironment Walled;
	Walled.HasLineOfSight = [](const FVector& From, const FVector& To) { return !(From.X > 500.0 || To.X > 500.0); };
	if (TestTrue(TEXT("walled: a kite point is found"), MDAIQueries::FindKitePoint(P, Walled, Point)))
	{
		TestTrue(TEXT("walled: the point has line of sight to the target"), Point.X <= 500.0);
		const float D = static_cast<float>(FVector::Dist2D(Point, P.TargetLocation));
		TestTrue(TEXT("walled: still at the preferred range"), FMath::Abs(D - 900.0f) <= 151.0f);
	}

	// No line of sight anywhere: no kite point.
	FMDQueryEnvironment Blind;
	Blind.HasLineOfSight = [](const FVector&, const FVector&) { return false; };
	TestFalse(TEXT("blind: no kite point"), MDAIQueries::FindKitePoint(P, Blind, Point));

	// A player in melee keeps the kite point 3 m away.
	P.MeleePlayers.Add(FVector(800.0, 300.0, 0.0));
	P.MeleePlayers.Add(FVector(0.0, 800.0, 0.0));
	if (TestTrue(TEXT("melee players: a kite point is found"), MDAIQueries::FindKitePoint(P, Open, Point)))
	{
		for (const FVector& M : P.MeleePlayers)
		{
			TestTrue(TEXT("at least 3 m from a player in melee"), FVector::Dist(Point, M) >= P.MinMeleeDistance - 1.0f);
		}
	}

	// Candidates stay inside the tolerance band.
	TArray<FVector> Candidates;
	MDAIQueries::KiteCandidates(P, Candidates);
	TestTrue(TEXT("kite candidates exist"), Candidates.Num() > 0);
	for (const FVector& C : Candidates)
	{
		const float D = static_cast<float>(FVector::Dist2D(C, P.TargetLocation));
		TestTrue(TEXT("every kite candidate is within the range band"), D >= 900.0f - 151.0f && D <= 900.0f + 151.0f);
	}
	return true;
}

// The reveal gate: no query ever returns a position in a room the shared fog has not revealed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIQueriesRevealGateTest, "MazeDemons.AI.Queries.NeverUnrevealedRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIQueriesRevealGateTest::RunTest(const FString& Parameters)
{
	FMDFogGrid Uninit;
	TestTrue(TEXT("a grid with no layout reveals everything (no fog)"), MDAIQueries::IsPointRevealed(Uninit, FVector(1234.0, 99.0, 0.0)));

	FMDFogGrid Grid;
	Grid.Init(FBox2D(FVector2D(-2000.0, -2000.0), FVector2D(2000.0, 2000.0)), 200.0f);
	Grid.RevealRadius(FVector2D(0.0, 0.0), 600.0f);
	TestTrue(TEXT("the revealed room is revealed"), MDAIQueries::IsPointRevealed(Grid, FVector(100.0, 100.0, 0.0)));
	TestFalse(TEXT("the far room is not"), MDAIQueries::IsPointRevealed(Grid, FVector(1500.0, 1500.0, 0.0)));
	TestTrue(TEXT("outside the grid counts as revealed (the gate only blocks known hidden rooms)"), MDAIQueries::IsPointRevealed(Grid, FVector(9000.0, 0.0, 0.0)));

	FMDQueryEnvironment Env;
	Env.IsRevealed = [&Grid](const FVector& P) { return MDAIQueries::IsPointRevealed(Grid, P); };

	// Flank: target at the edge of the revealed area, facing outwards: the flank band is inside, never beyond.
	FMDFlankParams Flank;
	Flank.TargetLocation = FVector(500.0, 0.0, 0.0);
	Flank.TargetFacing = FVector(-1.0, 0.0, 0.0); // behind it is +X: unrevealed
	Flank.FromLocation = FVector(0.0, 0.0, 0.0);
	Flank.Radius = 350.0f;
	FVector Point;
	const bool bFlank = MDAIQueries::FindFlankPoint(Flank, Env, Point);
	if (bFlank)
	{
		TestTrue(TEXT("flank point is in a revealed room"), MDAIQueries::IsPointRevealed(Grid, Point));
	}
	// Whole target area hidden: no flank point at all.
	FMDFogGrid Hidden;
	Hidden.Init(FBox2D(FVector2D(-2000.0, -2000.0), FVector2D(2000.0, 2000.0)), 200.0f);
	FMDQueryEnvironment HiddenEnv;
	HiddenEnv.IsRevealed = [&Hidden](const FVector& P) { return MDAIQueries::IsPointRevealed(Hidden, P); };
	Flank.TargetLocation = FVector(1000.0, 1000.0, 0.0);
	TestFalse(TEXT("nothing revealed: no flank point"), MDAIQueries::FindFlankPoint(Flank, HiddenEnv, Point));

	// Kite: a target near the border; every candidate returned is revealed.
	FMDKiteParams Kite;
	Kite.TargetLocation = FVector(0.0, 0.0, 0.0);
	Kite.FromLocation = FVector(1500.0, 0.0, 0.0); // the closest ring point is outside the revealed disc
	Kite.PreferredRange = 500.0f;
	if (TestTrue(TEXT("kite: a point in the revealed room is found"), MDAIQueries::FindKitePoint(Kite, Env, Point)))
	{
		TestTrue(TEXT("kite point is in a revealed room"), MDAIQueries::IsPointRevealed(Grid, Point));
	}
	Kite.PreferredRange = 1500.0f; // the whole ring is out of the revealed area
	TestFalse(TEXT("kite: a ring entirely in unrevealed rooms gives no point"), MDAIQueries::FindKitePoint(Kite, Env, Point));
	TestFalse(TEXT("kite: nothing revealed gives no point"), MDAIQueries::FindKitePoint(Kite, HiddenEnv, Point));

	// Ring slots: the ones in unrevealed rooms are skipped.
	TArray<FVector> RingAll;
	TArray<FVector> RingValid;
	MDAIQueries::RingSlots(FVector(700.0, 0.0, 0.0), 0.0f, 350.0f, RingAll);
	MDAIQueries::ValidRingSlots(FVector(700.0, 0.0, 0.0), 0.0f, 350.0f, Env, RingValid);
	TestEqual(TEXT("six slots without the gate"), RingAll.Num(), 6);
	TestTrue(TEXT("some slots are gated out"), RingValid.Num() < RingAll.Num());
	TestTrue(TEXT("some slots remain"), RingValid.Num() > 0);
	for (const FVector& R : RingValid)
	{
		TestTrue(TEXT("ring slot is in a revealed room"), MDAIQueries::IsPointRevealed(Grid, R));
	}

	// Rally: an unrevealed rally point is skipped for a revealed one, even when it is further away.
	const TArray<FVector> Rallies = { FVector(1500.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0) };
	const TArray<FVector> Entries = { FVector(300.0, 100.0, 0.0) };
	if (TestTrue(TEXT("rally: a revealed rally point is found"), MDAIQueries::FindRallyPoint(Rallies, Entries, FVector(1400.0, 0.0, 0.0), Env, Point)))
	{
		TestTrue(TEXT("rally: the revealed rally point wins over the nearer hidden one"), Point.Equals(FVector(200.0, 0.0, 0.0), 1.0));
	}
	const TArray<FVector> HiddenRallies = { FVector(1500.0, 0.0, 0.0), FVector(1800.0, 0.0, 0.0) };
	if (TestTrue(TEXT("rally: with every rally point hidden, a revealed room entry is the fallback"), MDAIQueries::FindRallyPoint(HiddenRallies, Entries, FVector(1400.0, 0.0, 0.0), Env, Point)))
	{
		TestTrue(TEXT("rally: the fallback is the room entry"), Point.Equals(FVector(300.0, 100.0, 0.0), 1.0));
	}
	const TArray<FVector> HiddenEntries = { FVector(1700.0, 0.0, 0.0) };
	TestFalse(TEXT("rally: nothing revealed, nothing found"), MDAIQueries::FindRallyPoint(HiddenRallies, HiddenEntries, FVector::ZeroVector, Env, Point));
	TestFalse(TEXT("rally: no rally points and no entries"), MDAIQueries::FindRallyPoint(TArray<FVector>(), TArray<FVector>(), FVector::ZeroVector, Env, Point));
	return true;
}

// The world wrappers: rally actors (AMDRallyPoint) win over room entries (tagged actors), the reveal gate reaches the subsystem,
// and the ambush query is one EQS template run only at spawn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIQueriesWorldTest, "MazeDemons.AI.Queries.WorldWrappersAndAmbush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIQueriesWorldTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;
	FWorldFx F;
	if (!F.Init(*this, 1))
	{
		return false;
	}
	UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
	if (!TestNotNull(TEXT("AI subsystem"), AI))
	{
		return false;
	}

	FVector Point;
	TestFalse(TEXT("no rally point and no room entry: nothing"), MDAIQueries::FindRallyPointFor(F.World, FVector::ZeroVector, Point));

	AActor* Entry = SpawnMarker(F.World, FVector(800.0, 0.0, 0.0));
	if (TestNotNull(TEXT("room entry actor"), Entry))
	{
		Entry->Tags.Add(AMDRallyPoint::RoomEntryTag);
		if (TestTrue(TEXT("a room entry is the fallback"), MDAIQueries::FindRallyPointFor(F.World, FVector::ZeroVector, Point)))
		{
			TestTrue(TEXT("the fallback is the room entry"), FVector::Dist2D(Point, FVector(800.0, 0.0, 0.0)) < 150.0);
		}
	}
	AMDRallyPoint* Rally = F.World->SpawnActor<AMDRallyPoint>(FVector(3000.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("rally point"), Rally))
	{
		if (TestTrue(TEXT("a rally point is found"), MDAIQueries::FindRallyPointFor(F.World, FVector::ZeroVector, Point)))
		{
			TestTrue(TEXT("the rally point wins over the nearer room entry"), FVector::Dist2D(Point, FVector(3000.0, 0.0, 0.0)) < 150.0);
		}
	}

	// The reveal gate reaches the AI subsystem through the override (the real source is the shared fog component).
	TestTrue(TEXT("no fog: every room counts as revealed"), AI->IsRoomRevealed(FVector(5000.0, 0.0, 0.0)));
	FMDFogGrid Grid;
	Grid.Init(FBox2D(FVector2D(-2000.0, -2000.0), FVector2D(2000.0, 2000.0)), 200.0f);
	Grid.RevealRadius(FVector2D(0.0, 0.0), 400.0f);
	AI->SetRevealGridOverride(&Grid);
	TestTrue(TEXT("the revealed room is revealed"), AI->IsRoomRevealed(FVector(0.0, 0.0, 0.0)));
	TestFalse(TEXT("an unrevealed room is not"), AI->IsRoomRevealed(FVector(1500.0, 1500.0, 0.0)));
	if (AMDRallyPoint* HiddenRally = F.World->SpawnActor<AMDRallyPoint>(FVector(1500.0, 1500.0, 0.0), FRotator::ZeroRotator))
	{
		FVector RallyNear;
		if (MDAIQueries::FindRallyPointFor(F.World, FVector(1500.0, 1400.0, 0.0), RallyNear))
		{
			TestTrue(TEXT("the world rally query never returns a point in a hidden room"), AI->IsRoomRevealed(RallyNear));
		}
		HiddenRally->Destroy();
	}
	AI->SetRevealGridOverride(nullptr);

	// The ambush query: one EQS template (grid + distance filter), built once.
	UEnvQuery* Template = UMDAIAmbushQuery::GetTemplate();
	if (TestNotNull(TEXT("ambush EQS template"), Template))
	{
		TestEqual(TEXT("the ambush template is one option (one generator)"), Template->GetOptionsMutable().Num(), 1);
		TestTrue(TEXT("the template is built once"), UMDAIAmbushQuery::GetTemplate() == Template);
	}
	TArray<FVector> Positions;
	F.Place(F.Enemies[0], FVector(1000.0, 0.0, 0.0));
	UMDAIAmbushQuery::Run(F.Enemies[0], Positions); // no navmesh in the test world: it may find nothing, but must not fail
	for (const FVector& Pos : Positions)
	{
		TestTrue(TEXT("an ambush position is at least 8 m from the player"), FVector::Dist2D(Pos, F.Player->GetActorLocation()) >= UMDAIAmbushQuery::MinPlayerDistance - 1.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dormancy (world)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIDormancyWorldTest, "MazeDemons.AI.Dormancy.SleepWakeAndNetRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIDormancyWorldTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;
	FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), 2);
	FWorldFx F;
	if (!F.Init(*this, 3, true))
	{
		return false;
	}
	UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
	if (!TestNotNull(TEXT("AI subsystem"), AI))
	{
		return false;
	}
	AI->TrackExtraPlayer(F.Player);
	F.Place(F.Player, FVector::ZeroVector);
	F.Place(F.Enemies[0], FVector(5000.0, 0.0, 0.0)); // 50 m: asleep
	F.Place(F.Enemies[1], FVector(1000.0, 0.0, 0.0)); // 10 m: awake, full rate
	F.Place(F.Enemies[2], FVector(3000.0, 0.0, 0.0)); // 30 m: awake, reduced rate
	UMDEnemyBrainComponent* Far = F.Brain(0);
	UMDEnemyBrainComponent* Near = F.Brain(1);
	UMDEnemyBrainComponent* Mid = F.Brain(2);
	if (!TestTrue(TEXT("brains exist"), Far && Near && Mid))
	{
		return false;
	}

	TestEqual(TEXT("one enemy goes to sleep"), AI->UpdateDormancy(10.0), 1);
	TestTrue(TEXT("beyond 40 m: dormant"), Far->IsDormant());
	TestFalse(TEXT("10 m: awake"), Near->IsDormant());
	TestFalse(TEXT("30 m: awake"), Mid->IsDormant());
	TestEqual(TEXT("NumDormant"), AI->NumDormant(), 1);

	const float DefaultHz = Near->GetDefaultNetFrequency();
	TestEqual(TEXT("near: full net update rate"), F.Enemies[1]->GetNetUpdateFrequency(), DefaultHz);
	TestEqual(TEXT("30 m: reduced net update rate"), F.Enemies[2]->GetNetUpdateFrequency(), FMath::Min(DefaultHz, MDDormancy::ReducedNetFrequency));

	// A dormant enemy senses nothing (sight, hearing, shared) ...
	FVector Loc;
	bool bNow = false;
	Far->ReportStimulus(F.Player, true, F.Player->GetActorLocation());
	Far->ReportHeard(F.Player, F.Player->GetActorLocation(), FName(MDNoise::Gunshot));
	Far->RunDecision(20.0);
	TestFalse(TEXT("a dormant enemy senses nothing"), KnowsPlayer(Far, F.Player, Loc, bNow));
	// ... and the scheduler skips it while the awake ones keep thinking.
	const int32 FarDecisions = Far->GetDecisionIndex();
	const int32 NearDecisions = Near->GetDecisionIndex();
	F.TickFrames(90, 1.0f / 60.0f);
	TestEqual(TEXT("the scheduler does not think for a dormant enemy"), Far->GetDecisionIndex(), FarDecisions);
	TestTrue(TEXT("the scheduler keeps thinking for awake enemies"), Near->GetDecisionIndex() > NearDecisions);
	TestTrue(TEXT("the far enemy is still dormant after ticking"), Far->IsDormant());

	// Damage wakes it.
	if (TestNotNull(TEXT("enemy ASC"), F.Enemies[0]->AbilitySystem.Get()))
	{
		F.Enemies[0]->AbilitySystem->ApplyDamage(5.0f, F.Player);
		TestFalse(TEXT("damage wakes a dormant enemy"), Far->IsDormant());
		const double Hurt = F.World->GetTimeSeconds();
		AI->UpdateDormancy(Hurt + 2.0);
		TestFalse(TEXT("a recently hurt enemy stays awake with no player near"), Far->IsDormant());
		AI->UpdateDormancy(Hurt + 20.0);
		TestTrue(TEXT("it goes back to sleep once the damage is old"), Far->IsDormant());
	}

	// A player coming into range wakes it.
	F.Place(F.Player, FVector(4200.0, 0.0, 0.0));
	AI->UpdateDormancy(F.World->GetTimeSeconds() + 40.0);
	TestFalse(TEXT("a player within 40 m wakes it"), Far->IsDormant());

	// md.AI.Dormancy 0 turns the system off.
	{
		FScopedIntCVar NoDormancy(TEXT("md.AI.Dormancy"), 0);
		F.Place(F.Player, FVector(-9000.0, 0.0, 0.0));
		TestEqual(TEXT("dormancy off: nobody sleeps"), AI->UpdateDormancy(F.World->GetTimeSeconds() + 60.0), 0);
	}

	// The path request budget: at most 10 per frame.
	++GFrameCounter;
	int32 Granted = 0;
	for (int32 i = 0; i < 30; ++i)
	{
		Granted += AI->TryConsumePathRequest() ? 1 : 0;
	}
	TestEqual(TEXT("10 path requests per frame"), Granted, MDNav::PathRequestsPerFrame);
	++GFrameCounter;
	TestTrue(TEXT("a new frame, a new budget"), AI->TryConsumePathRequest());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Perf: 40 enemies
// ---------------------------------------------------------------------------------------------------------------------

// 40 real enemies (character + controller + brain) awake around a player for 10 s of ticks: the brain scheduler stays within
// 0.6 ms per frame and the pack director within 0.3 ms, on average (2.9). Stimuli are fed every half second so the brains,
// the sharing fan-out and the director all work. The transient world has no navmesh and no StateTree executors (BeginPlay is
// not dispatched), so path following and tasks are outside the numbers, which cover the two subsystem ticks the budgets name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPerfFortyEnemiesTest, "MazeDemons.AI.Perf.FortyEnemies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPerfFortyEnemiesTest::RunTest(const FString& Parameters)
{
	using namespace MDAIPerceptionTestsLocal;
	FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), 2);
	constexpr int32 NumEnemies = 40;
	FWorldFx F;
	if (!F.Init(*this, NumEnemies, true))
	{
		return false;
	}
	UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
	if (!TestNotNull(TEXT("AI subsystem"), AI) || !TestNotNull(TEXT("pack director"), Director))
	{
		return false;
	}
	AI->TrackExtraPlayer(F.Player);
	F.Place(F.Player, FVector::ZeroVector);
	for (int32 i = 0; i < NumEnemies; ++i)
	{
		const float Angle = FMath::DegreesToRadians(static_cast<float>(i) * (360.0f / NumEnemies));
		const float Radius = 1500.0f + static_cast<float>(i % 5) * 200.0f;
		F.Place(F.Enemies[i], FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0));
	}
	for (int32 Pack = 0; Pack < 8; ++Pack)
	{
		Director->RegisterSpawnGroup(F.EnemyActors(Pack * 5, 5), 0, FName(*FString::Printf(TEXT("Room%d"), Pack)));
	}
	TestEqual(TEXT("40 brains registered"), AI->NumBrains(), NumEnemies);

	constexpr float Dt = 1.0f / 60.0f;
	constexpr int32 TotalFrames = 600; // 10 s
	constexpr int32 WarmupFrames = 60;
	for (int32 Frame = 0; Frame < TotalFrames + WarmupFrames; ++Frame)
	{
		if (Frame == WarmupFrames)
		{
			AI->ResetProfile();
			Director->ResetProfile();
		}
		if (Frame % 30 == 0)
		{
			// A few enemies see the player every half second; the sighting is shared through the packs.
			for (int32 i = Frame / 30 % 8; i < NumEnemies; i += 8)
			{
				if (UMDEnemyBrainComponent* Brain = F.Brain(i))
				{
					Brain->ReportStimulus(F.Player, true, F.Player->GetActorLocation());
				}
			}
			if (Frame % 60 == 0 && F.Brain(3))
			{
				F.Brain(3)->ReportHeard(F.Player, F.Player->GetActorLocation(), FName(MDNoise::Gunshot));
			}
		}
		++GFrameCounter;
		F.World->Tick(LEVELTICK_All, Dt);
	}

	const double BrainMs = AI->GetProfile().AverageMs();
	const double DirectorMs = Director->GetProfile().AverageMs();
	AddInfo(FString::Printf(TEXT("40 enemies, %d frames: brain average %.4f ms (peak %.3f), director average %.4f ms (peak %.3f), %d dormant"),
		AI->GetProfile().Frames, BrainMs, AI->GetProfile().PeakMs(), DirectorMs, Director->GetProfile().PeakMs(), AI->NumDormant()));
	TestTrue(TEXT("the profiles saw the frames"), AI->GetProfile().Frames >= TotalFrames - 5 && Director->GetProfile().Frames >= TotalFrames - 5);
	TestEqual(TEXT("everything stayed awake (all within 40 m)"), AI->NumDormant(), 0);
	TestTrue(*FString::Printf(TEXT("brain <= 0.6 ms/frame on average (was %.4f)"), BrainMs), BrainMs <= 0.6);
	TestTrue(*FString::Printf(TEXT("director <= 0.3 ms/frame on average (was %.4f)"), DirectorMs), DirectorMs <= 0.3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
