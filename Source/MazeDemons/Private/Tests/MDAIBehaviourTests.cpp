#include "CoreMinimal.h"
#include "AI/MDAIQueries.h"
#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyBehaviourComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDPack.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDPackRules.h"
#include "AI/MDTestArchetypes.h"
#include "AI/MDEnemyArchetype.h"
#include "HAL/IConsoleManager.h"
#include "MDAbilitySystemComponent.h"
#include "MDPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// V1 WP5 advanced-behaviour tests (MazeDemons.AI.Behaviour.*). The rules are plain functions (MDBehaviourRules.h, MDPackRules.h)
// and run with no world; BaitNeverHoldsToken also uses a transient world with real enemies and the pack director.

namespace MDAIBehaviourTestsLocal
{
	struct FScopedForceTier
	{
		IConsoleVariable* Var = nullptr;
		int32 Old = -1;

		explicit FScopedForceTier(int32 Tier)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(TEXT("md.AI.ForceTier"));
			if (Var)
			{
				Old = Var->GetInt();
				Var->Set(Tier, ECVF_SetByCode);
			}
		}
		~FScopedForceTier()
		{
			if (Var)
			{
				Var->Set(Old, ECVF_SetByCode);
			}
		}
	};

	struct FBehaviourWorld
	{
		UWorld* World = nullptr;
		APawn* Player = nullptr;
		AMDPlayerState* PlayerState = nullptr;
		TArray<AMDEnemyCharacter*> Enemies;

		bool Init(FAutomationTestBase& Test, int32 NumEnemies)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIBehaviourTestWorld")));
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
			PlayerState->SetRole(ROLE_Authority);
			Player->SetPlayerState(PlayerState);
			if (UMDAbilitySystemComponent* ASC = PlayerState->GetMDAbilitySystemComponent())
			{
				ASC->InitAbilityActorInfo(PlayerState, Player);
				ASC->RegisterAttributeSet(PlayerState->Attributes);
				ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			}
			for (int32 i = 0; i < NumEnemies; ++i)
			{
				AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>();
				if (!Enemy)
				{
					Test.AddError(TEXT("Could not spawn an enemy"));
					return false;
				}
				Enemies.Add(Enemy);
			}
			return true;
		}

		TArray<AActor*> EnemyActors() const
		{
			TArray<AActor*> Out;
			for (AMDEnemyCharacter* Enemy : Enemies)
			{
				Out.Add(Enemy);
			}
			return Out;
		}

		void ReportAll(UMDPackDirectorSubsystem& Director, uint32 AllowedMask) const
		{
			for (AMDEnemyCharacter* Enemy : Enemies)
			{
				FMDMemberReport Report;
				Report.Target = Player;
				Report.Known.Add(Player);
				Report.AllowedMask = AllowedMask;
				Director.ReportMember(Enemy, Report);
			}
		}

		~FBehaviourWorld()
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

	int32 CountRole(const TArray<EMDRole>& Roles, EMDRole Role)
	{
		int32 N = 0;
		for (const EMDRole R : Roles)
		{
			N += R == Role ? 1 : 0;
		}
		return N;
	}

	TArray<FMDRoleCandidate> MakeCandidates(int32 Num, uint32 Mask, int32 CasterIndex = INDEX_NONE)
	{
		TArray<FMDRoleCandidate> Out;
		for (int32 i = 0; i < Num; ++i)
		{
			FMDRoleCandidate C;
			C.Id = i + 1;
			C.AllowedMask = Mask;
			C.bIsCaster = i == CasterIndex;
			Out.Add(C);
		}
		return Out;
	}

	constexpr uint32 MeleeMask = MDPack::RoleBit(EMDRole::Striker) | MDPack::RoleBit(EMDRole::Flanker) | MDPack::RoleBit(EMDRole::Bait);
	constexpr uint32 FlankMask = MDPack::RoleBit(EMDRole::Striker) | MDPack::RoleBit(EMDRole::Flanker);

	MDBehaviour::FMDBlinkParams MakeBlink()
	{
		MDBehaviour::FMDBlinkParams P;
		P.TargetLocation = FVector::ZeroVector;
		P.TargetFacing = FVector::ForwardVector;
		P.FromLocation = FVector(1200.0, 0.0, 0.0);
		return P;
	}
}

// The bait never holds an attack token: not by rule, not through the director, whatever the pool has free.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourBaitNeverHoldsTokenTest, "MazeDemons.AI.Behaviour.BaitNeverHoldsToken", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourBaitNeverHoldsTokenTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Bait cannot hold an attack token"), MDBehaviour::CanHoldAttackToken(EMDRole::Bait));
	TestFalse(TEXT("Guard cannot hold an attack token"), MDBehaviour::CanHoldAttackToken(EMDRole::Guard));
	TestTrue(TEXT("Striker can"), MDBehaviour::CanHoldAttackToken(EMDRole::Striker));
	TestTrue(TEXT("Flanker can"), MDBehaviour::CanHoldAttackToken(EMDRole::Flanker));

	MDAIBehaviourTestsLocal::FScopedForceTier Tier(2);
	MDAIBehaviourTestsLocal::FBehaviourWorld F;
	if (!F.Init(*this, 3))
	{
		return false;
	}
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
	if (!TestNotNull(TEXT("director"), Director))
	{
		return false;
	}
	Director->RegisterSpawnGroup(F.EnemyActors(), 0, NAME_None);
	for (int32 i = 0; i < F.Enemies.Num(); ++i)
	{
		F.Enemies[i]->SetActorLocation(FVector(300.0 + 80.0 * i, 40.0 * i, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	}
	F.ReportAll(*Director, MDAIBehaviourTestsLocal::MeleeMask);
	Director->StepPacks(1.0);

	AMDEnemyCharacter* Bait = nullptr;
	for (AMDEnemyCharacter* Enemy : F.Enemies)
	{
		FMDEnemyAssignment A;
		if (Director->GetAssignment(Enemy, A) && A.Role == EMDRole::Bait)
		{
			Bait = Enemy;
		}
	}
	if (!TestNotNull(TEXT("a pack of 3 at T2 has a Bait"), Bait))
	{
		return false;
	}
	TestFalse(TEXT("the director refuses the bait a token on a free pool"), Director->RequestToken(Bait, F.Player));
	TestFalse(TEXT("so it holds none"), Director->HoldsToken(Bait));
	for (AMDEnemyCharacter* Enemy : F.Enemies)
	{
		if (Enemy != Bait)
		{
			TestTrue(TEXT("a non-bait member gets one"), Director->RequestToken(Enemy, F.Player));
			break;
		}
	}
	return true;
}

// The bait stays within 8 m of the pack, steps back half a step when approached, and never baits across unrevealed rooms.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourBaitLeashTest, "MazeDemons.AI.Behaviour.BaitLeash8m", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourBaitLeashTest::RunTest(const FString& Parameters)
{
	const FVector Pack = FVector::ZeroVector;
	TestTrue(TEXT("7.9 m is inside the leash"), MDBehaviour::IsWithinBaitLeash(FVector(790.0, 0.0, 0.0), Pack));
	TestTrue(TEXT("8.0 m is on the leash"), MDBehaviour::IsWithinBaitLeash(FVector(800.0, 0.0, 0.0), Pack));
	TestFalse(TEXT("8.1 m is outside"), MDBehaviour::IsWithinBaitLeash(FVector(810.0, 0.0, 0.0), Pack));

	// Approached at 4 m inside the leash: a half-step (90 cm) straight away from the player.
	{
		FVector Step;
		const FVector Bait(300.0, 0.0, 0.0);
		const FVector Player(700.0, 0.0, 0.0);
		TestTrue(TEXT("approached bait steps"), MDBehaviour::ComputeBaitStep(Bait, Player, Pack, Step));
		TestTrue(TEXT("away from the player"), Step.X < Bait.X);
		TestTrue(TEXT("a half-step, not a run"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(Step, Bait)), MDBehaviour::BaitHalfStep, 1.0f));
		TestTrue(TEXT("and still on the leash"), MDBehaviour::IsWithinBaitLeash(Step, Pack));
	}
	// A bait at the leash edge with the player pushing outward never ends outside it.
	{
		FVector Step;
		const FVector Bait(790.0, 0.0, 0.0);
		const FVector Player(1200.0, 0.0, 0.0); // 4 m on the far side: stepping back means going toward the pack
		if (MDBehaviour::ComputeBaitStep(Bait, Player, Pack, Step))
		{
			TestTrue(TEXT("the step stays inside the leash"), MDBehaviour::IsWithinBaitLeash(Step, Pack));
		}
		const FVector Player2(300.0, 0.0, 0.0); // pushes it outward past 8 m
		if (MDBehaviour::ComputeBaitStep(Bait, Player2, Pack, Step))
		{
			TestTrue(TEXT("pushed outward, the step is clamped to the leash"), MDBehaviour::IsWithinBaitLeash(Step, Pack));
		}
	}
	// Strayed beyond the leash: walks back toward the pack.
	{
		FVector Step;
		const FVector Bait(1200.0, 0.0, 0.0);
		TestTrue(TEXT("a strayed bait is brought back"), MDBehaviour::ComputeBaitStep(Bait, FVector(3000.0, 0.0, 0.0), Pack, Step));
		TestTrue(TEXT("toward the pack"), Step.X < Bait.X);
	}
	// A far player gives a bait inside the leash nothing to do.
	{
		FVector Step;
		TestFalse(TEXT("no step when the player is far"), MDBehaviour::ComputeBaitStep(FVector(200.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0), Pack, Step));
	}
	// Never baits across unrevealed rooms.
	{
		FMDQueryEnvironment Open;
		TestTrue(TEXT("no fog: allowed"), MDBehaviour::CanBaitAcross(Open, FVector(100.0, 0.0, 0.0), FVector(900.0, 0.0, 0.0)));
		FMDQueryEnvironment Fog;
		Fog.IsRevealed = [](const FVector& P) { return P.X < 500.0; }; // the player's room (x = 900) is unrevealed
		TestFalse(TEXT("player in an unrevealed room: no bait"), MDBehaviour::CanBaitAcross(Fog, FVector(100.0, 0.0, 0.0), FVector(900.0, 0.0, 0.0)));
		TestFalse(TEXT("bait in an unrevealed room: no bait"), MDBehaviour::CanBaitAcross(Fog, FVector(900.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0)));
		TestTrue(TEXT("both revealed: bait"), MDBehaviour::CanBaitAcross(Fog, FVector(100.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0)));
	}
	// The fake flinch is T3+.
	TestFalse(TEXT("no fake flinch at T2"), MDBehaviour::FakeFlinchUnlocked(EMDIntelligenceTier::T2));
	TestTrue(TEXT("fake flinch at T3"), MDBehaviour::FakeFlinchUnlocked(EMDIntelligenceTier::T3));
	TestTrue(TEXT("Bait unlocks at T2"), FMDTierParams::ForTier(EMDIntelligenceTier::T2).IsIntentUnlocked(EMDIntent::Bait));
	TestFalse(TEXT("Bait is locked at T1"), FMDTierParams::ForTier(EMDIntelligenceTier::T1).IsIntentUnlocked(EMDIntent::Bait));
	return true;
}

// Feint is T3+ only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourFeintTierGateTest, "MazeDemons.AI.Behaviour.FeintTierGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourFeintTierGateTest::RunTest(const FString& Parameters)
{
	for (int32 T = 0; T <= 4; ++T)
	{
		const EMDIntelligenceTier Tier = static_cast<EMDIntelligenceTier>(T);
		const bool bExpected = T >= 3;
		TestEqual(*FString::Printf(TEXT("T%d Feint unlocked"), T), FMDTierParams::ForTier(Tier).IsIntentUnlocked(EMDIntent::Feint), bExpected);
		MDBehaviour::FMDFeintOpportunity Opp;
		TestEqual(*FString::Printf(TEXT("T%d feints on a 0 roll"), T), Opp.ShouldFeintNow(Tier, true, 0.0f), bExpected);
		TestEqual(*FString::Printf(TEXT("T%d chance"), T), MDBehaviour::FeintChance(Tier) > 0.0f, bExpected);
	}
	MDBehaviour::FMDFeintOpportunity NoToken;
	TestFalse(TEXT("a feint needs the token"), NoToken.ShouldFeintNow(EMDIntelligenceTier::T4, false, 0.0f));
	MDBehaviour::FMDFeintOpportunity HighRoll;
	TestFalse(TEXT("a high roll does not feint"), HighRoll.ShouldFeintNow(EMDIntelligenceTier::T3, true, 0.99f));
	return true;
}

// One feint per attack opportunity; the real attack comes 0.3 - 0.6 s after the jerk.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourFeintOncePerOpportunityTest, "MazeDemons.AI.Behaviour.FeintOncePerOpportunity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourFeintOncePerOpportunityTest::RunTest(const FString& Parameters)
{
	MDBehaviour::FMDFeintOpportunity Opp;
	TestTrue(TEXT("first look: feints"), Opp.ShouldFeintNow(EMDIntelligenceTier::T4, true, 0.0f));
	TestTrue(TEXT("until it is marked"), Opp.ShouldFeintNow(EMDIntelligenceTier::T4, true, 0.9f));
	Opp.MarkFeinted();
	for (int32 i = 0; i < 5; ++i)
	{
		TestFalse(TEXT("never a second feint in the same opportunity"), Opp.ShouldFeintNow(EMDIntelligenceTier::T4, true, 0.0f));
	}
	Opp.Reset();
	TestTrue(TEXT("a new opportunity may feint again"), Opp.ShouldFeintNow(EMDIntelligenceTier::T4, true, 0.0f));

	TestEqual(TEXT("fastest real attack follows after 0.3 s"), MDBehaviour::FeintDelay(0.0f), 0.3f);
	TestEqual(TEXT("slowest after 0.6 s"), MDBehaviour::FeintDelay(1.0f), 0.6f);
	for (int32 i = 0; i <= 10; ++i)
	{
		const float D = MDBehaviour::FeintDelay(static_cast<float>(i) / 10.0f);
		TestTrue(TEXT("delay within 0.3 - 0.6 s"), D >= 0.3f - KINDA_SMALL_NUMBER && D <= 0.6f + KINDA_SMALL_NUMBER);
	}
	TestTrue(TEXT("the cancel comes inside the wind-up, before the trace window"), MDBehaviour::FeintCancelAfter < 0.8f);
	TestTrue(TEXT("the jerk has its own sound"), UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue::FeintJerk).ToString().Contains(TEXT("MonsterRoarsAndGrowls")));
	return true;
}

// The blink arrival is nav-valid, at least 2 m from the target and in a revealed area; the 6 s cooldown.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourBlinkArrivalValidTest, "MazeDemons.AI.Behaviour.BlinkArrivalValid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourBlinkArrivalValidTest::RunTest(const FString& Parameters)
{
	const MDBehaviour::FMDBlinkParams Params = MDAIBehaviourTestsLocal::MakeBlink();
	FVector Point;

	FMDQueryEnvironment Open;
	TestTrue(TEXT("open ground: an arrival exists"), MDBehaviour::FindBlinkArrival(Params, Open, Point));
	TestTrue(TEXT("at least 2 m from the target"), FVector::Dist2D(Point, Params.TargetLocation) >= MDBehaviour::BlinkMinTargetDistance - 1.0f);
	TestTrue(TEXT("behind or beside the target"), MDAIQueries::AngleFromFacing(Params.TargetLocation, Params.TargetFacing, Point) >= 90.0f - 1.0f);

	// Nothing on the navmesh: no blink.
	FMDQueryEnvironment NoNav;
	NoNav.ProjectToNav = [](const FVector&, FVector&) { return false; };
	TestFalse(TEXT("no navmesh point: no blink"), MDBehaviour::FindBlinkArrival(Params, NoNav, Point));

	// Everything unrevealed: no blink into the fog.
	FMDQueryEnvironment Fog;
	Fog.IsRevealed = [](const FVector&) { return false; };
	TestFalse(TEXT("unrevealed area: no blink"), MDBehaviour::FindBlinkArrival(Params, Fog, Point));

	// A navmesh that snaps every point onto the target: arrival is too close.
	FMDQueryEnvironment Snap;
	Snap.ProjectToNav = [](const FVector&, FVector& Out) { Out = FVector(50.0, 0.0, 0.0); return true; };
	TestFalse(TEXT("snapped within 2 m of the target: no blink"), MDBehaviour::FindBlinkArrival(Params, Snap, Point));

	FVector Projected;
	TestFalse(TEXT("a candidate 1.5 m from the target is invalid"), MDBehaviour::IsBlinkArrivalValid(Params, Open, FVector(-150.0, 0.0, 0.0), Projected));
	TestTrue(TEXT("a candidate 3 m behind is valid"), MDBehaviour::IsBlinkArrivalValid(Params, Open, FVector(-300.0, 0.0, 0.0), Projected));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourBlinkCooldownTest, "MazeDemons.AI.Behaviour.BlinkCooldown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourBlinkCooldownTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("max 1 blink per 6 s"), MDBehaviour::BlinkCooldown, 6.0);
	TestTrue(TEXT("never blinked: ready"), MDBehaviour::BlinkOffCooldown(0.0, -1000.0));
	TestFalse(TEXT("1 s after a blink"), MDBehaviour::BlinkOffCooldown(11.0, 10.0));
	TestFalse(TEXT("5.9 s after a blink"), MDBehaviour::BlinkOffCooldown(15.9, 10.0));
	TestTrue(TEXT("6 s after a blink"), MDBehaviour::BlinkOffCooldown(16.0, 10.0));
	TestEqual(TEXT("pre-blink warning 0.4 s"), MDBehaviour::BlinkPreDelay, 0.4);
	TestEqual(TEXT("0.3 s no-attack grace after arriving"), MDBehaviour::BlinkArrivalGrace, 0.3);
	TestFalse(TEXT("Blink is locked at T2"), FMDTierParams::ForTier(EMDIntelligenceTier::T2).IsIntentUnlocked(EMDIntent::Blink));
	TestTrue(TEXT("Blink unlocks at T3"), FMDTierParams::ForTier(EMDIntelligenceTier::T3).IsIntentUnlocked(EMDIntent::Blink));
	return true;
}

// Guards stand on the line between the caster and the player, and hold defence (not attack) tokens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourGuardLineTest, "MazeDemons.AI.Behaviour.GuardLineBetweenCasterAndPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourGuardLineTest::RunTest(const FString& Parameters)
{
	const FVector Caster(0.0, 0.0, 0.0);
	const FVector Player(1500.0, 0.0, 0.0);
	TArray<FVector> Line;
	MDBehaviour::ComputeGuardLine(Caster, Player, 2, Line);
	if (!TestEqual(TEXT("two guards, two points"), Line.Num(), 2))
	{
		return false;
	}
	for (const FVector& P : Line)
	{
		TestTrue(TEXT("between the caster and the player"), P.X > Caster.X && P.X < Player.X);
		TestTrue(TEXT("at least 2 m in front of the caster"), FVector::Dist2D(P, Caster) >= MDBehaviour::GuardMinDistance - 1.0f);
		TestTrue(TEXT("on the line, spread across it"), FMath::Abs(P.Y) <= MDBehaviour::GuardSpacing);
	}
	TestTrue(TEXT("the guards are spaced apart"), FVector::Dist2D(Line[0], Line[1]) >= MDBehaviour::GuardSpacing - 1.0f);

	// A close player: the line stays in front of the caster, never behind the player.
	MDBehaviour::ComputeGuardLine(Caster, FVector(400.0, 0.0, 0.0), 1, Line);
	if (TestEqual(TEXT("one guard"), Line.Num(), 1))
	{
		TestTrue(TEXT("still between them"), Line[0].X > 0.0 && Line[0].X < 400.0);
	}
	// Off-axis player.
	MDBehaviour::ComputeGuardLine(Caster, FVector(0.0, -1200.0, 0.0), 1, Line);
	if (TestEqual(TEXT("one guard (off axis)"), Line.Num(), 1))
	{
		TestTrue(TEXT("between them on Y"), Line[0].Y < 0.0 && Line[0].Y > -1200.0);
	}

	// Roles: with a caster at T2+ the director gives up to MaxGuards Guards; the caster itself never guards; T1 has none.
	{
		const TArray<FMDRoleCandidate> Candidates = MDAIBehaviourTestsLocal::MakeCandidates(5, MDPack::RoleBit(EMDRole::Striker) | MDPack::RoleBit(EMDRole::Guard) | MDPack::RoleBit(EMDRole::Harasser), /*caster*/ 0);
		TArray<EMDRole> Roles;
		MDPackRules::AssignRoles(Candidates, EMDIntelligenceTier::T2, Roles);
		const int32 Guards = MDAIBehaviourTestsLocal::CountRole(Roles, EMDRole::Guard);
		TestTrue(TEXT("at least one guard screens the caster at T2"), Guards >= 1);
		TestTrue(TEXT("at most MaxGuards guards"), Guards <= MDBehaviour::MaxGuards);
		TestTrue(TEXT("the caster is not its own guard"), Roles[0] != EMDRole::Guard);
		MDPackRules::AssignRoles(Candidates, EMDIntelligenceTier::T1, Roles);
		TestEqual(TEXT("no guards at T1"), MDAIBehaviourTestsLocal::CountRole(Roles, EMDRole::Guard), 0);
	}
	TestFalse(TEXT("guards hold defence tokens, never attack tokens"), MDBehaviour::CanHoldAttackToken(EMDRole::Guard));
	TestTrue(TEXT("Guard unlocks at T2"), FMDTierParams::ForTier(EMDIntelligenceTier::T2).IsIntentUnlocked(EMDIntent::Guard));
	return true;
}

// Ambush: cue 0.75 s before, 0.5 s no-attack grace on arrival, T2+ only, never at a checkpoint or a station.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourAmbushTest, "MazeDemons.AI.Behaviour.AmbushGraceAndNoCheckpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourAmbushTest::RunTest(const FString& Parameters)
{
	const MDBehaviour::FMDAmbushTimeline T = MDBehaviour::MakeAmbushTimeline(10.0);
	TestEqual(TEXT("the cue plays at the trigger"), T.CueTime, 10.0);
	TestEqual(TEXT("the group appears 0.75 s after the cue"), T.ArriveTime - T.CueTime, 0.75);
	TestEqual(TEXT("no attacks for 0.5 s after arrival"), T.GraceEnd - T.ArriveTime, 0.5);
	TestTrue(TEXT("grace holds on arrival"), MDBehaviour::IsInArrivalGrace(T.ArriveTime, T.GraceEnd));
	TestTrue(TEXT("grace holds at 0.49 s"), MDBehaviour::IsInArrivalGrace(T.ArriveTime + 0.49, T.GraceEnd));
	TestFalse(TEXT("grace is over at 0.5 s"), MDBehaviour::IsInArrivalGrace(T.GraceEnd, T.GraceEnd));

	TestFalse(TEXT("T0 does not ambush"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T0, false, false));
	TestFalse(TEXT("T1 does not ambush"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T1, false, false));
	TestTrue(TEXT("T2 ambushes"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T2, false, false));
	TestTrue(TEXT("a demo marker ambushes at T0"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T0, true, false));
	TestFalse(TEXT("never at a checkpoint / station (even at T4)"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T4, false, true));
	TestFalse(TEXT("never at a checkpoint / station (even ignoring the tier)"), MDBehaviour::CanAmbush(EMDIntelligenceTier::T4, true, true));

	TArray<FVector> Safe = { FVector(0.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0) };
	TestTrue(TEXT("14.9 m from a checkpoint is too close"), MDBehaviour::IsNearSafeSpot(FVector(1490.0, 0.0, 0.0), Safe));
	TestFalse(TEXT("15.1 m is fine"), MDBehaviour::IsNearSafeSpot(FVector(1510.0, 0.0, 0.0), Safe));
	TestTrue(TEXT("near the second spot"), MDBehaviour::IsNearSafeSpot(FVector(5200.0, 100.0, 0.0), Safe));
	TestFalse(TEXT("no spots, no restriction"), MDBehaviour::IsNearSafeSpot(FVector::ZeroVector, TArray<FVector>()));

	TestTrue(TEXT("the ambush growl is the 12_Ambush_Growl_Cue"), UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue::AmbushGrowl).ToString().Contains(TEXT("12_Ambush_Growl_Cue")));
	TestTrue(TEXT("Ambush unlocks at T2"), FMDTierParams::ForTier(EMDIntelligenceTier::T2).IsIntentUnlocked(EMDIntent::Ambush));
	return true;
}

// Punish greed (T3+): a braced counter after N absorbed hits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourPunishGreedTest, "MazeDemons.AI.Behaviour.PunishGreedThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourPunishGreedTest::RunTest(const FString& Parameters)
{
	MDBehaviour::FMDGreedTracker Greed;
	Greed.RegisterAbsorbedHit(1.0);
	Greed.RegisterAbsorbedHit(1.5);
	TestFalse(TEXT("2 hits of 3: no brace"), Greed.ShouldBrace(EMDIntelligenceTier::T3, 3));
	Greed.RegisterAbsorbedHit(2.0);
	TestTrue(TEXT("3 hits of 3: brace at T3"), Greed.ShouldBrace(EMDIntelligenceTier::T3, 3));
	TestTrue(TEXT("and at T4"), Greed.ShouldBrace(EMDIntelligenceTier::T4, 3));
	TestFalse(TEXT("never at T2"), Greed.ShouldBrace(EMDIntelligenceTier::T2, 3));
	TestFalse(TEXT("threshold 0 = never"), Greed.ShouldBrace(EMDIntelligenceTier::T4, 0));

	// Hits further apart than the window do not chain.
	MDBehaviour::FMDGreedTracker Slow;
	Slow.RegisterAbsorbedHit(0.0);
	Slow.RegisterAbsorbedHit(4.0);
	Slow.RegisterAbsorbedHit(8.0);
	TestFalse(TEXT("spaced hits do not add up"), Slow.ShouldBrace(EMDIntelligenceTier::T4, 3));

	// The brace window expires.
	Greed.StartBrace(10.0);
	TestFalse(TEXT("the counter is still available"), Greed.BraceExpired(13.9));
	TestTrue(TEXT("the counter expires after 4 s"), Greed.BraceExpired(14.1));
	Greed.Reset();
	TestFalse(TEXT("reset clears the chain"), Greed.ShouldBrace(EMDIntelligenceTier::T4, 3));
	TestFalse(TEXT("and the brace"), Greed.bBraced);

	// Archetype table: only the Brute test archetype has a threshold.
	UMDEnemyArchetype* Brute = MDTestArchetypes::Make(GetTransientPackage(), MDTestArchetypes::EKind::Brute);
	UMDEnemyArchetype* Were = MDTestArchetypes::Make(GetTransientPackage(), MDTestArchetypes::EKind::Werewolf);
	if (TestNotNull(TEXT("brute"), Brute) && TestNotNull(TEXT("werewolf"), Were))
	{
		TestEqual(TEXT("the brute braces after 3 hits"), Brute->AIParams.PunishGreedHits, 3);
		TestEqual(TEXT("the werewolf never does"), Were->AIParams.PunishGreedHits, 0);
	}
	return true;
}

// Flankers per player: T2 1, T4 2 (T0 / T1 none); the flanker's attack still needs a token.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourFlankerCapTest, "MazeDemons.AI.Behaviour.FlankerCapPerTier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourFlankerCapTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("T0"), MDPackRules::FlankersFor(EMDIntelligenceTier::T0), 0);
	TestEqual(TEXT("T1"), MDPackRules::FlankersFor(EMDIntelligenceTier::T1), 0);
	TestEqual(TEXT("T2"), MDPackRules::FlankersFor(EMDIntelligenceTier::T2), 1);
	TestEqual(TEXT("T3"), MDPackRules::FlankersFor(EMDIntelligenceTier::T3), 1);
	TestEqual(TEXT("T4"), MDPackRules::FlankersFor(EMDIntelligenceTier::T4), 2);

	const TArray<FMDRoleCandidate> Candidates = MDAIBehaviourTestsLocal::MakeCandidates(6, MDAIBehaviourTestsLocal::FlankMask);
	for (int32 T = 0; T <= 4; ++T)
	{
		TArray<EMDRole> Roles;
		MDPackRules::AssignRoles(Candidates, static_cast<EMDIntelligenceTier>(T), Roles);
		TestEqual(*FString::Printf(TEXT("T%d flankers in a pack of 6"), T), MDAIBehaviourTestsLocal::CountRole(Roles, EMDRole::Flanker),
			MDPackRules::FlankersFor(static_cast<EMDIntelligenceTier>(T)));
	}
	TestTrue(TEXT("Flank unlocks at T2"), FMDTierParams::ForTier(EMDIntelligenceTier::T2).IsIntentUnlocked(EMDIntent::Flank));
	TestFalse(TEXT("Flank is locked at T1"), FMDTierParams::ForTier(EMDIntelligenceTier::T1).IsIntentUnlocked(EMDIntent::Flank));
	return true;
}

// Kite: range keeping, the 1 s reposition cooldown, the tier-scaled draw. The archer fires through FireFromData.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourKiteTest, "MazeDemons.AI.Behaviour.Kite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourKiteTest::RunTest(const FString& Parameters)
{
	using MDBehaviour::EKiteAction;
	const float Preferred = 900.0f;
	TestTrue(TEXT("in range with sight and a token: fire"), MDBehaviour::DecideKite(900.0f, Preferred, true, true, true) == EKiteAction::Fire);
	TestTrue(TEXT("in range without a token: hold"), MDBehaviour::DecideKite(900.0f, Preferred, true, true, false) == EKiteAction::Hold);
	TestTrue(TEXT("too close: back off"), MDBehaviour::DecideKite(300.0f, Preferred, true, true, true) == EKiteAction::Reposition);
	TestTrue(TEXT("too far: close in"), MDBehaviour::DecideKite(1600.0f, Preferred, true, true, true) == EKiteAction::Reposition);
	TestTrue(TEXT("no line of sight: new point"), MDBehaviour::DecideKite(900.0f, Preferred, false, true, true) == EKiteAction::Reposition);
	TestTrue(TEXT("too close inside the cooldown: cannot run yet"), MDBehaviour::DecideKite(300.0f, Preferred, true, false, true) != EKiteAction::Reposition);

	TestEqual(TEXT("one reposition per second"), MDBehaviour::KiteRepositionCooldown, 1.0);
	TestFalse(TEXT("0.5 s after a reposition"), MDBehaviour::KiteRepositionReady(10.5, 10.0));
	TestTrue(TEXT("1 s after"), MDBehaviour::KiteRepositionReady(11.0, 10.0));

	TestTrue(TEXT("a later tier draws faster"), MDBehaviour::KiteDrawTimeFor(FMDTierParams::ForTier(EMDIntelligenceTier::T4)) <= MDBehaviour::KiteDrawTimeFor(FMDTierParams::ForTier(EMDIntelligenceTier::T0)));
	TestTrue(TEXT("Kite is available at T1 for kiters"), FMDTierParams::ForTier(EMDIntelligenceTier::T1).IsIntentUnlocked(EMDIntent::Kite));

	UMDEnemyArchetype* Archer = MDTestArchetypes::Make(GetTransientPackage(), MDTestArchetypes::EKind::Archer);
	if (TestNotNull(TEXT("archer archetype"), Archer))
	{
		TestTrue(TEXT("the test archer is a kiter"), Archer->AIParams.bKiter);
		TestTrue(TEXT("with a long preferred range"), Archer->AIParams.PreferredRange >= 800.0f);
	}
	return true;
}

// Charge: line up, straight line, cancelled by a sideways dodge-slide.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourChargeTest, "MazeDemons.AI.Behaviour.Charge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourChargeTest::RunTest(const FString& Parameters)
{
	const FVector Target(0.0, 0.0, 0.0);
	const FVector From(300.0, 100.0, 0.0);
	const FVector Line = MDBehaviour::ChargeLineUpPoint(Target, From);
	TestTrue(TEXT("lines up 6 m from the target"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(Line, Target)), MDBehaviour::ChargeLineUpDistance, 1.0f));
	TestTrue(TEXT("on the brute's side"), FVector::DotProduct(Line - Target, From - Target) > 0.0);

	const FVector Origin(1000.0, 0.0, 0.0);
	const FVector Dir(-1.0, 0.0, 0.0);
	TestTrue(TEXT("a target on the line is hit"), !MDBehaviour::IsChargeCancelledBySidestep(Origin, Dir, FVector(0.0, 30.0, 0.0)));
	TestTrue(TEXT("a small shuffle does not cancel"), !MDBehaviour::IsChargeCancelledBySidestep(Origin, Dir, FVector(0.0, 100.0, 0.0)));
	TestTrue(TEXT("a dodge-slide to the right cancels"), MDBehaviour::IsChargeCancelledBySidestep(Origin, Dir, FVector(0.0, 220.0, 0.0)));
	TestTrue(TEXT("and to the left"), MDBehaviour::IsChargeCancelledBySidestep(Origin, Dir, FVector(0.0, -220.0, 0.0)));
	TestEqual(TEXT("lateral offset"), MDBehaviour::ChargeLateralOffset(Origin, Dir, FVector(200.0, 150.0, 0.0)), 150.0f);

	TestFalse(TEXT("too close for a run-up"), MDBehaviour::IsChargeDistanceOk(200.0f));
	TestTrue(TEXT("in range"), MDBehaviour::IsChargeDistanceOk(900.0f));
	TestFalse(TEXT("too far"), MDBehaviour::IsChargeDistanceOk(2500.0f));
	TestTrue(TEXT("a dodged charge leaves the brute open longer"), MDBehaviour::ChargeCancelRecovery > MDBehaviour::ChargeRecovery);
	TestTrue(TEXT("Charge unlocks at T1"), FMDTierParams::ForTier(EMDIntelligenceTier::T1).IsIntentUnlocked(EMDIntent::Charge));

	UMDEnemyArchetype* Brute = MDTestArchetypes::Make(GetTransientPackage(), MDTestArchetypes::EKind::Brute);
	if (TestNotNull(TEXT("brute archetype"), Brute))
	{
		TestTrue(TEXT("the test brute can charge"), Brute->AIParams.bCanCharge);
	}
	return true;
}

// Swarm: evenly spaced attackers, the pack bark with its 15 s limiter, md.AI.ForceIntent parsing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBehaviourSwarmTest, "MazeDemons.AI.Behaviour.SwarmAndBarks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBehaviourSwarmTest::RunTest(const FString& Parameters)
{
	TArray<int32> Slots;
	MDBehaviour::EvenSlotSet(3, 0, Slots);
	TestEqual(TEXT("3 attackers use 3 slots"), Slots.Num(), 3);
	Slots.Sort();
	TestTrue(TEXT("0, 2, 4"), Slots.Num() == 3 && Slots[0] == 0 && Slots[1] == 2 && Slots[2] == 4);
	MDBehaviour::EvenSlotSet(2, 1, Slots);
	Slots.Sort();
	TestTrue(TEXT("2 attackers sit opposite each other"), Slots.Num() == 2 && (Slots[1] - Slots[0]) == 3);
	MDBehaviour::EvenSlotSet(6, 2, Slots);
	TestEqual(TEXT("6 attackers use every slot"), Slots.Num(), 6);

	// Through the slot assignment: 3 members bunched on one side still end up evenly spaced.
	{
		TArray<FMDSlotMember> Members;
		for (int32 i = 0; i < 3; ++i)
		{
			FMDSlotMember M;
			M.Id = i + 1;
			M.Role = EMDRole::Striker;
			M.AngleDeg = 10.0f * i;
			Members.Add(M);
		}
		TArray<int32> Assigned;
		MDPackRules::AssignSlots(Members, 0.0f, 0.0f, Assigned, true);
		Assigned.Sort();
		if (TestEqual(TEXT("three slots assigned"), Assigned.Num(), 3))
		{
			TestTrue(TEXT("evenly spaced around the ring"), Assigned[0] >= 0 && (Assigned[1] - Assigned[0]) == 2 && (Assigned[2] - Assigned[1]) == 2);
		}
	}

	TestFalse(TEXT("no swarm at T0"), MDBehaviour::ShouldSwarm(EMDIntelligenceTier::T0, 4, true, 1200.0f, 450.0f));
	TestTrue(TEXT("swarm at T1 with 4 alive"), MDBehaviour::ShouldSwarm(EMDIntelligenceTier::T1, 4, true, 1200.0f, 450.0f));
	TestFalse(TEXT("no swarm with 2 alive"), MDBehaviour::ShouldSwarm(EMDIntelligenceTier::T1, 2, true, 1200.0f, 450.0f));

	TestTrue(TEXT("the first bark is allowed"), MDBehaviour::BarkAllowed(5.0, -1.0));
	TestFalse(TEXT("a second bark 5 s later is not"), MDBehaviour::BarkAllowed(10.0, 5.0));
	TestTrue(TEXT("15 s later it is"), MDBehaviour::BarkAllowed(20.0, 5.0));
	TestTrue(TEXT("swarm howl is the 17_Bloodthirsty_Growl_Cue"), UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue::SwarmHowl).ToString().Contains(TEXT("17_Bloodthirsty_Growl_Cue")));
	TestTrue(TEXT("taunt is the 25_Warning_Growl_Cue"), UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue::Taunt).ToString().Contains(TEXT("25_Warning_Growl_Cue")));

	EMDIntent Forced = EMDIntent::Idle;
	TestTrue(TEXT("Swarm parses"), MDBehaviour::ParseForcedIntent(TEXT("Swarm"), Forced) && Forced == EMDIntent::Swarm);
	TestTrue(TEXT("case-insensitive"), MDBehaviour::ParseForcedIntent(TEXT("feint"), Forced) && Forced == EMDIntent::Feint);
	TestFalse(TEXT("none clears"), MDBehaviour::ParseForcedIntent(TEXT("none"), Forced));
	TestFalse(TEXT("unknown names are ignored"), MDBehaviour::ParseForcedIntent(TEXT("Dance"), Forced));
	return true;
}
