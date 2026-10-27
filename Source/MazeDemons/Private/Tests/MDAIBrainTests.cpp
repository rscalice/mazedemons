#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDEnemyAnimInstance.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "AI/StateTree/MDSTEnemyTasks.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatStatics.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// V1 WP1 brain tests (MazeDemons.AI.Brain.*). The rules are plain structs (MDAIRules.h); the component tests use a transient
// world with an authoritative enemy (its AMDEnemyController and brain spawn with it) and a player-team pawn as the target.

namespace MDAIBrainTestsLocal
{
	FMDBrainContext MakeContext(float Distance, bool bToken, bool bSensed = true)
	{
		FMDBrainContext C;
		C.bHasTarget = true;
		C.bTargetSensed = bSensed;
		C.DistanceToTarget = Distance;
		C.bHasToken = bToken;
		C.bAttackReady = true;
		C.AttackRange = 220.0f;
		C.ThreatRange = 450.0f;
		C.RunSeed = 1234;
		C.EnemyId = 7;
		C.DecisionIndex = 3;
		return C;
	}

	float ScoreOf(const MDAI::FScores& Scores, EMDIntent Intent)
	{
		const FMDScoredIntent* S = FMDUtilityScorer::Find(Scores, Intent);
		return S ? S->Score : -1.0f;
	}

	UMDEnemyArchetype* MakeGhoulArchetype(float MaxPoise)
	{
		UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
		Archetype->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
		Archetype->MaxHealth = 60.0f;
		Archetype->MaxPoise = MaxPoise;
		Archetype->KillGold = 5;
		Archetype->MoveSpeed = 520.0f;
		FMDEnemyAttack& Swipe = Archetype->Attacks.AddDefaulted_GetRef();
		Swipe.Name = TEXT("Swipe");
		Swipe.Range = 220.0f;
		Swipe.TellTime = 0.8f;
		FMDEnemyAttack& Lunge = Archetype->Attacks.AddDefaulted_GetRef();
		Lunge.Name = TEXT("Lunge");
		Lunge.Range = 450.0f;
		Lunge.TellTime = 0.9f;
		return Archetype;
	}

	// Transient world: one player-team pawn (AMDPlayerState) and N authoritative enemies with brains.
	struct FBrainFixture
	{
		UWorld* World = nullptr;
		APawn* Player = nullptr;
		AMDPlayerState* PlayerState = nullptr;
		TArray<AMDEnemyCharacter*> Enemies;
		UMDEnemyArchetype* Archetype = nullptr;

		bool Init(FAutomationTestBase& Test, int32 NumEnemies, float MaxPoise = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIBrainTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
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
			UMDAbilitySystemComponent* PlayerASC = PlayerState->GetMDAbilitySystemComponent();
			if (!PlayerASC)
			{
				Test.AddError(TEXT("Player state has no ASC"));
				return false;
			}
			PlayerASC->InitAbilityActorInfo(PlayerState, Player);
			PlayerASC->RegisterAttributeSet(PlayerState->Attributes);
			PlayerASC->InitializeStats(100.0f, 10.0f, 0.0f);

			Archetype = MakeGhoulArchetype(MaxPoise);
			for (int32 i = 0; i < NumEnemies; ++i)
			{
				AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>();
				if (!Enemy)
				{
					Test.AddError(TEXT("Could not spawn an enemy"));
					return false;
				}
				Enemy->ApplyArchetype(Archetype);
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

		// The player becomes visible to every brain at world time 0.
		void SeePlayer() const
		{
			for (int32 i = 0; i < Enemies.Num(); ++i)
			{
				if (UMDEnemyBrainComponent* B = Brain(i))
				{
					B->ReportStimulus(Player, true, Player->GetActorLocation());
				}
			}
		}

		~FBrainFixture()
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
}

// Same seed, enemy and decision index -> identical scores; a different decision index or seed moves them; noise stays
// inside the tier amplitude; gated intents stay exactly 0 whatever the noise.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainScorerDeterministicForSeedTest, "MazeDemons.AI.Brain.ScorerDeterministicForSeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainScorerDeterministicForSeedTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;
	const FMDArchetypeAIParams Params;
	const FMDTierParams T0 = FMDTierParams::ForTier(EMDIntelligenceTier::T0);

	const FMDBrainContext Far = MakeContext(900.0f, false);
	const MDAI::FScores A = FMDUtilityScorer::Score(Far, Params, T0);
	const MDAI::FScores B = FMDUtilityScorer::Score(Far, Params, T0);
	if (!TestEqual(TEXT("scores one slot per scored intent"), A.Num(), 4) || !TestEqual(TEXT("same count twice"), B.Num(), A.Num()))
	{
		return false;
	}
	for (int32 i = 0; i < A.Num(); ++i)
	{
		TestEqual(*FString::Printf(TEXT("intent %d identical"), i), static_cast<int32>(A[i].Intent), static_cast<int32>(B[i].Intent));
		TestEqual(*FString::Printf(TEXT("score %d identical"), i), A[i].Score, B[i].Score);
	}

	// Far away, no token: Chase leads; the raw bases are 0.3-ish Idle? no: Idle 0.05, Chase 0.5 + 0.3 * (680/1500).
	TestTrue(TEXT("far target: chase scores highest"), FMDUtilityScorer::Select(A, EMDIntent::Idle, true) == EMDIntent::Chase);
	TestEqual(TEXT("far target, no token: attack is gated to exactly 0"), ScoreOf(A, EMDIntent::Attack), 0.0f);
	TestEqual(TEXT("far target: hold threat is 0 outside the threat ring"), ScoreOf(A, EMDIntent::HoldThreat), 0.0f);

	// Noise: bounded by the amplitude around the base, and different across decisions / seeds.
	FMDBrainContext Next = Far;
	Next.DecisionIndex = 4;
	const MDAI::FScores C = FMDUtilityScorer::Score(Next, Params, T0);
	FMDBrainContext OtherSeed = Far;
	OtherSeed.RunSeed = 99;
	const MDAI::FScores D = FMDUtilityScorer::Score(OtherSeed, Params, T0);
	bool bAnyDifferentDecision = false;
	bool bAnyDifferentSeed = false;
	for (int32 i = 0; i < A.Num(); ++i)
	{
		bAnyDifferentDecision |= !FMath::IsNearlyEqual(A[i].Score, C[i].Score, 1e-6f);
		bAnyDifferentSeed |= !FMath::IsNearlyEqual(A[i].Score, D[i].Score, 1e-6f);
	}
	TestTrue(TEXT("the next decision index draws different noise"), bAnyDifferentDecision);
	TestTrue(TEXT("another run seed draws different noise"), bAnyDifferentSeed);
	const float ChaseBase = 0.5f + 0.3f * ((900.0f - 220.0f) / 1500.0f);
	TestTrue(TEXT("chase noise within the T0 amplitude"), FMath::Abs(ScoreOf(A, EMDIntent::Chase) - ChaseBase) <= T0.NoiseAmplitude + 1e-5f);
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const float N = FMDUtilityScorer::Noise(1234, 7, 3, Slot, 0.05f);
		TestTrue(*FString::Printf(TEXT("noise slot %d bounded"), Slot), FMath::Abs(N) <= 0.05f);
		TestEqual(*FString::Printf(TEXT("noise slot %d repeatable"), Slot), N, FMDUtilityScorer::Noise(1234, 7, 3, Slot, 0.05f));
	}
	TestEqual(TEXT("zero amplitude = no noise"), FMDUtilityScorer::Noise(1234, 7, 3, 0, 0.0f), 0.0f);

	// A dead brain scores nothing; the T0 table row matches v1-design 3.2.
	FMDBrainContext Dead = Far;
	Dead.bDead = true;
	TestEqual(TEXT("dead: no scores"), FMDUtilityScorer::Score(Dead, Params, T0).Num(), 0);
	TestEqual(TEXT("T0 reaction time"), T0.ReactionTime, 0.9f);
	TestEqual(TEXT("T0 tokens per player"), T0.MaxTokensPerPlayer, 1);
	TestEqual(TEXT("T0 aggression"), T0.Aggression, 0.6f);
	TestEqual(TEXT("T0 sight"), T0.SightRadius, 1500.0f);
	const FMDTierParams T4 = FMDTierParams::ForTier(EMDIntelligenceTier::T4);
	TestEqual(TEXT("T4 reaction time"), T4.ReactionTime, 0.3f);
	TestEqual(TEXT("T4 tokens per player"), T4.MaxTokensPerPlayer, 3);
	TestEqual(TEXT("tell floor"), T4.ResolveTellTime(0.5f), 0.35f);
	TestEqual(TEXT("T0 tell unchanged"), T0.ResolveTellTime(0.8f), 0.8f);
	TestFalse(TEXT("feint locked at T0"), T0.IsIntentUnlocked(EMDIntent::Feint));
	TestTrue(TEXT("feint unlocked at T3"), FMDTierParams::ForTier(EMDIntelligenceTier::T3).IsIntentUnlocked(EMDIntent::Feint));
	TestTrue(TEXT("hold threat allowed at T0 (fairness fallback)"), T0.IsIntentUnlocked(EMDIntent::HoldThreat));
	return true;
}

// A new intent must beat the running one by SwitchMargin (0.15); a finished intent lets the top score win outright.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainHysteresisMarginTest, "MazeDemons.AI.Brain.HysteresisMargin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainHysteresisMarginTest::RunTest(const FString& Parameters)
{
	MDAI::FScores Scores;
	auto Set = [&Scores](EMDIntent Intent, float Score)
	{
		FMDScoredIntent& S = Scores.AddDefaulted_GetRef();
		S.Intent = Intent;
		S.Score = Score;
	};
	Set(EMDIntent::Idle, 0.05f);
	Set(EMDIntent::Chase, 0.50f);
	Set(EMDIntent::HoldThreat, 0.60f);
	Set(EMDIntent::Attack, 0.0f);

	TestEqual(TEXT("no current intent: the best wins"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Idle, true)), static_cast<int32>(EMDIntent::HoldThreat));
	TestEqual(TEXT("chase running, hold ahead by 0.10 < margin: keep chasing"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Chase, false)), static_cast<int32>(EMDIntent::Chase));
	TestEqual(TEXT("chase finished: switch"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Chase, true)), static_cast<int32>(EMDIntent::HoldThreat));
	TestEqual(TEXT("the current intent is already the best: keep it"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::HoldThreat, false)), static_cast<int32>(EMDIntent::HoldThreat));

	Scores[2].Score = 0.66f; // ahead by 0.16 > margin
	TestEqual(TEXT("ahead by more than the margin: switch"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Chase, false)), static_cast<int32>(EMDIntent::HoldThreat));
	Scores[2].Score = 0.65f; // ahead by exactly the margin
	TestEqual(TEXT("ahead by exactly the margin: switch"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Chase, false)), static_cast<int32>(EMDIntent::HoldThreat));
	TestEqual(TEXT("a wider margin keeps it"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Chase, false, 0.3f)), static_cast<int32>(EMDIntent::Chase));

	// A current intent that scores 0 (gated away, e.g. Attack without a token) never holds the brain.
	TestEqual(TEXT("a gated current intent does not stick"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Attack, false)), static_cast<int32>(EMDIntent::HoldThreat));
	// A current intent that is not scored at all (Stagger) does not stick either.
	TestEqual(TEXT("an unscored current intent does not stick"), static_cast<int32>(FMDUtilityScorer::Select(Scores, EMDIntent::Stagger, false)), static_cast<int32>(EMDIntent::HoldThreat));

	MDAI::FScores Empty;
	TestEqual(TEXT("no scores: Idle"), static_cast<int32>(FMDUtilityScorer::Select(Empty, EMDIntent::Chase, false)), static_cast<int32>(EMDIntent::Idle));

	FMDUtilityScorer::SortDescending(Scores);
	TestTrue(TEXT("sorted descending"), Scores[0].Score >= Scores[1].Score && Scores[1].Score >= Scores[2].Score && Scores[2].Score >= Scores[3].Score);
	return true;
}

// Stimuli reach the brain only ReactionTime after they happened (T0 0.9 s): the queue holds them, and a brain that saw
// the player at t=0 is still Idle at t=0.5 and chasing at t=1.0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainReactionDelayGateTest, "MazeDemons.AI.Brain.ReactionDelayGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainReactionDelayGateTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;

	// Pure queue.
	FMDDelayedStimulusQueue Queue;
	FMDStimulus S;
	S.Time = 0.0;
	S.Location = FVector(100.0, 0.0, 0.0);
	Queue.Push(S);
	S.Time = 0.3;
	Queue.Push(S);
	int32 Delivered = 0;
	Queue.Pop(0.5, 0.9, [&Delivered](const FMDStimulus&) { ++Delivered; });
	TestEqual(TEXT("nothing before the delay"), Delivered, 0);
	TestEqual(TEXT("both still pending"), Queue.Num(), 2);
	Queue.Pop(0.9, 0.9, [&Delivered](const FMDStimulus&) { ++Delivered; });
	TestEqual(TEXT("the first one at exactly the delay"), Delivered, 1);
	TestEqual(TEXT("one left"), Queue.Num(), 1);
	Queue.Pop(1.3, 0.9, [&Delivered](const FMDStimulus&) { ++Delivered; });
	TestEqual(TEXT("the second one 0.3 s later"), Delivered, 2);
	TestEqual(TEXT("queue drained"), Queue.Num(), 0);

	// Brain component: the player is reported at world time 0 (the test world never ticks).
	FBrainFixture Fx;
	if (!Fx.Init(*this, 1))
	{
		return false;
	}
	UMDEnemyBrainComponent* Brain = Fx.Brain(0);
	if (!TestNotNull(TEXT("the enemy's controller carries a brain"), Brain))
	{
		return false;
	}
	TestEqual(TEXT("T0 reaction time on the brain"), Brain->GetTier().ReactionTime, 0.9f);
	// The bare test pawn has no root component, so the enemy is the one that moves: 9 m away from the player at the origin.
	Fx.Place(Fx.Enemies[0], FVector(900.0, 0.0, 0.0));
	Fx.SeePlayer();

	Brain->RunDecision(0.5);
	TestEqual(TEXT("0.5 s after the sighting: still idle"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Idle));
	TestNull(TEXT("no target yet"), Brain->GetTarget());
	Brain->RunDecision(1.0);
	TestEqual(TEXT("1.0 s after the sighting: chasing"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Chase));
	TestTrue(TEXT("the player is the target"), Brain->GetTarget() == Fx.Player);
	TestTrue(TEXT("replicated intent mirrors the brain"), Fx.Enemies[0]->GetIntent() == EMDIntent::Chase);
	TestTrue(TEXT("decisions are counted"), Brain->GetDecisionIndex() >= 2);

	// Losing the player: a "lost" stimulus, then LoseTargetTime without a sighting -> forgotten -> Idle.
	Brain->ReportStimulus(Fx.Player, false, Fx.Player->GetActorLocation()); // stamped at world time 0
	Brain->RunDecision(1.5);
	TestTrue(TEXT("just lost: still chasing the last known position"), Brain->GetTarget() == Fx.Player && !Brain->IsTargetSensed());
	Brain->RunDecision(0.9 + MDAI::LoseTargetTime + 0.2);
	TestNull(TEXT("forgotten after LoseTargetTime"), Brain->GetTarget());
	TestEqual(TEXT("back to idle"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Idle));
	return true;
}

// A stagger interrupts whatever the brain was doing, returns its token at once and the brain resumes with a fresh
// decision when the stun ends.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainStaggerInterruptsAndReturnsTokenTest, "MazeDemons.AI.Brain.StaggerInterruptsAndReturnsToken", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainStaggerInterruptsAndReturnsTokenTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;

	// Pure pool: release returns it immediately, ReleaseAfter keeps it for the hold time.
	FMDAttackTokenPool Pool;
	TestTrue(TEXT("grant"), Pool.TryGrant(1, 10, 0.0, 1));
	TestTrue(TEXT("held"), Pool.Holds(1, 0.1));
	Pool.Release(1);
	TestFalse(TEXT("released at once"), Pool.Holds(1, 0.1));
	TestTrue(TEXT("grant again"), Pool.TryGrant(1, 10, 1.0, 1));
	Pool.ReleaseAfter(1, 1.0, 0.5);
	TestTrue(TEXT("still held during the 0.5 s hold"), Pool.Holds(1, 1.4));
	TestFalse(TEXT("gone after the hold"), Pool.Holds(1, 1.5));
	TestEqual(TEXT("nobody on the target afterwards"), Pool.CountForTarget(10, 1.5), 0);

	// Brain: poise 10 so one default light hit (10 poise damage) breaks it.
	FBrainFixture Fx;
	if (!Fx.Init(*this, 1, 10.0f))
	{
		return false;
	}
	UMDEnemyBrainComponent* Brain = Fx.Brain(0);
	AMDEnemyCharacter* Enemy = Fx.Enemies[0];
	if (!TestNotNull(TEXT("brain"), Brain))
	{
		return false;
	}
	Fx.Place(Enemy, FVector(150.0, 0.0, 0.0)); // the player (no root component) stays at the origin
	Fx.SeePlayer();
	Brain->RunDecision(1.0);
	TestEqual(TEXT("in reach with the only token: attack"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Attack));
	TestTrue(TEXT("holds the token"), Brain->HoldsAttackToken());
	TestEqual(TEXT("the swipe was chosen up close"), Brain->GetChosenAttackIndex(), 0);
	TestEqual(TEXT("tier-scaled tell at T0"), Brain->GetChosenTellTime(), 0.8f);

	// The player hits the ghoul through the C1 entry point: poise breaks -> Stagger reaction -> interrupt.
	const FHitResult NoHit;
	TestTrue(TEXT("the hit lands"), UMDCombatStatics::ApplyMeleeHit(Fx.Player, Enemy, NoHit));
	TestTrue(TEXT("poise broke (stunned)"), Enemy->Poise->IsStunned());
	TestEqual(TEXT("stagger interrupts the attack"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Stagger));
	TestFalse(TEXT("the token is returned at once"), Brain->HoldsAttackToken());
	TestTrue(TEXT("replicated intent is Stagger"), Enemy->GetIntent() == EMDIntent::Stagger);

	// While stunned, decisions change nothing.
	Brain->RunDecision(1.2);
	TestEqual(TEXT("still staggered while stunned"), static_cast<int32>(Brain->GetCurrentIntent()), static_cast<int32>(EMDIntent::Stagger));

	// The stun ends: the brain decides again at once and may take the token back.
	Enemy->Poise->ClearStun();
	TestTrue(TEXT("stun over: a fresh intent"), Brain->GetCurrentIntent() != EMDIntent::Stagger);
	TestTrue(TEXT("the pool is free again for the next decision"), Brain->HoldsAttackToken() || Brain->GetCurrentIntent() == EMDIntent::HoldThreat);

	// Death: Dead intent, token gone, unscheduled.
	int32 Guard = 0;
	while (!Enemy->AbilitySystem->IsDead() && Guard++ < 30)
	{
		UMDCombatStatics::ApplyMeleeHit(Fx.Player, Enemy, NoHit);
	}
	TestTrue(TEXT("killed"), Enemy->IsEnemyDead());
	TestTrue(TEXT("replicated intent is Dead"), Enemy->GetIntent() == EMDIntent::Dead);
	TestFalse(TEXT("a dead enemy holds no token"), UMDAISubsystem::Get(Fx.World)->HoldsAttackToken(Enemy));
	return true;
}

// One token per player at T0: with two ghouls in reach only one may attack; the other holds the threat range until the
// token comes back. The scorer itself gates Attack to 0 without a token.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainNoAttackWithoutTokenTest, "MazeDemons.AI.Brain.NoAttackWithoutToken", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainNoAttackWithoutTokenTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;
	const FMDArchetypeAIParams Params;
	const FMDTierParams T0 = FMDTierParams::ForTier(EMDIntelligenceTier::T0);

	// Scorer gate.
	const MDAI::FScores NoToken = FMDUtilityScorer::Score(MakeContext(150.0f, false), Params, T0);
	TestEqual(TEXT("no token: attack scores 0"), ScoreOf(NoToken, EMDIntent::Attack), 0.0f);
	TestEqual(TEXT("no token in reach: hold the threat"), static_cast<int32>(FMDUtilityScorer::Select(NoToken, EMDIntent::Chase, false)), static_cast<int32>(EMDIntent::HoldThreat));
	const MDAI::FScores WithToken = FMDUtilityScorer::Score(MakeContext(150.0f, true), Params, T0);
	TestTrue(TEXT("token in reach: attack"), FMDUtilityScorer::Select(WithToken, EMDIntent::HoldThreat, false) == EMDIntent::Attack);
	TestEqual(TEXT("with a token, hold threat is off"), ScoreOf(WithToken, EMDIntent::HoldThreat), 0.0f);
	const MDAI::FScores Unseen = FMDUtilityScorer::Score(MakeContext(150.0f, true, false), Params, T0);
	TestEqual(TEXT("a target out of sight is never swung at"), ScoreOf(Unseen, EMDIntent::Attack), 0.0f);
	FMDBrainContext Cooling = MakeContext(150.0f, true);
	Cooling.bAttackReady = false;
	TestEqual(TEXT("cooldown gates the attack"), ScoreOf(FMDUtilityScorer::Score(Cooling, Params, T0), EMDIntent::Attack), 0.0f);

	// Pool: a second holder on the same target is refused at MaxPerTarget 1, allowed at 2, allowed once the first lets go.
	FMDAttackTokenPool Pool;
	TestTrue(TEXT("first token"), Pool.TryGrant(1, 10, 0.0, 1));
	TestFalse(TEXT("second refused"), Pool.TryGrant(2, 10, 0.0, 1));
	TestTrue(TEXT("refreshing your own token is fine"), Pool.TryGrant(1, 10, 0.5, 1));
	TestTrue(TEXT("another target is a separate pool"), Pool.TryGrant(2, 11, 0.5, 1));
	Pool.Release(2);
	TestTrue(TEXT("T1 allows two"), Pool.TryGrant(2, 10, 0.5, 2));
	Pool.Release(2);
	TestFalse(TEXT("back to one: refused"), Pool.TryGrant(2, 10, 0.5, 1));
	TestFalse(TEXT("a token times out after 4 s"), Pool.Holds(1, 4.5));
	TestTrue(TEXT("after the timeout the other gets it"), Pool.TryGrant(2, 10, 4.5, 1));
	Pool.Purge(4.5);
	TestEqual(TEXT("purged"), Pool.Num(), 1);

	// Two ghouls, one player, both in reach: only one attacks.
	FBrainFixture Fx;
	if (!Fx.Init(*this, 2))
	{
		return false;
	}
	UMDEnemyBrainComponent* A = Fx.Brain(0);
	UMDEnemyBrainComponent* B = Fx.Brain(1);
	if (!TestNotNull(TEXT("brain A"), A) || !TestNotNull(TEXT("brain B"), B))
	{
		return false;
	}
	TestTrue(TEXT("enemy ids are distinct"), A->GetEnemyId() != B->GetEnemyId());
	Fx.Place(Fx.Player, FVector::ZeroVector);
	Fx.Place(Fx.Enemies[0], FVector(150.0, 0.0, 0.0));
	Fx.Place(Fx.Enemies[1], FVector(-150.0, 0.0, 0.0));
	Fx.SeePlayer();
	A->RunDecision(1.0);
	B->RunDecision(1.0);
	TestEqual(TEXT("A attacks"), static_cast<int32>(A->GetCurrentIntent()), static_cast<int32>(EMDIntent::Attack));
	TestTrue(TEXT("A holds the token"), A->HoldsAttackToken());
	TestEqual(TEXT("B holds the threat range"), static_cast<int32>(B->GetCurrentIntent()), static_cast<int32>(EMDIntent::HoldThreat));
	TestFalse(TEXT("B has no token"), B->HoldsAttackToken());
	TestEqual(TEXT("exactly one token on the player"), UMDAISubsystem::Get(Fx.World)->CountAttackTokensOn(Fx.Player), 1);
	for (int32 i = 0; i < 5; ++i)
	{
		B->RunDecision(1.0 + 0.125 * (i + 1));
	}
	TestTrue(TEXT("B never attacks while A holds the token"), B->GetCurrentIntent() != EMDIntent::Attack);

	// A dies: the token comes back and B takes it.
	A->NotifyDeath();
	TestEqual(TEXT("A is dead"), static_cast<int32>(A->GetCurrentIntent()), static_cast<int32>(EMDIntent::Dead));
	B->RunDecision(2.0);
	TestEqual(TEXT("B attacks once the token is free"), static_cast<int32>(B->GetCurrentIntent()), static_cast<int32>(EMDIntent::Attack));
	TestTrue(TEXT("B holds the token"), B->HoldsAttackToken());
	return true;
}

// Scheduler, hold-point, wind-up rate, attack pick, intent tags and the executor task types.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainSchedulerAndRulesTest, "MazeDemons.AI.Brain.SchedulerAndRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainSchedulerAndRulesTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;

	// Round robin: 40 brains at 8 Hz over one second of 60 fps frames = 320 runs, at most 40 per frame, spread evenly.
	float Acc = 0.0f;
	int32 Total = 0;
	int32 MaxBatch = 0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		const int32 Batch = MDBrainScheduling::BatchSize(40, 8.0f, 1.0f / 60.0f, Acc);
		Total += Batch;
		MaxBatch = FMath::Max(MaxBatch, Batch);
	}
	TestTrue(TEXT("about 320 decisions per second"), FMath::Abs(Total - 320) <= 1);
	TestTrue(TEXT("spread over frames (5 or 6 per frame)"), MaxBatch <= 6);
	Acc = 0.0f;
	TestEqual(TEXT("a hitch never runs more than one full round"), MDBrainScheduling::BatchSize(40, 8.0f, 5.0f, Acc), 40);
	TestEqual(TEXT("no brains, no work"), MDBrainScheduling::BatchSize(0, 8.0f, 0.016f, Acc), 0);

	// The subsystem runs each registered brain once per round.
	{
		FBrainFixture Fx;
		if (Fx.Init(*this, 3))
		{
			UMDAISubsystem* Sub = UMDAISubsystem::Get(Fx.World);
			if (TestNotNull(TEXT("AI subsystem"), Sub))
			{
				TestEqual(TEXT("three brains registered"), Sub->NumBrains(), 3);
				const int32 Before0 = Fx.Brain(0)->GetDecisionIndex();
				int32 Ran = 0;
				for (int32 Frame = 0; Frame < 8; ++Frame)
				{
					Ran += Sub->StepScheduler(0.125f); // one second at 8 Hz in 8 frames: 24 decisions, 8 per brain
				}
				TestEqual(TEXT("one full second of decisions"), Ran, 24);
				TestEqual(TEXT("brain 0 ran 8 times"), Fx.Brain(0)->GetDecisionIndex() - Before0, 8);
				TestEqual(TEXT("brain 2 ran 8 times"), Fx.Brain(2)->GetDecisionIndex(), 8);
				TestTrue(TEXT("a run seed exists"), Sub->GetRunSeed() != 0);
			}
		}
	}

	// Hold point: on the target->enemy line at the range; no move inside the tolerance.
	FVector Point;
	TestTrue(TEXT("too close: step back"), FMDST_EnemyHoldThreat::ComputeHoldPoint(FVector::ZeroVector, FVector(100.0, 0.0, 50.0), 450.0f, 120.0f, Point));
	TestTrue(TEXT("back to the range on the same line"), Point.Equals(FVector(450.0, 0.0, 50.0), 1.0));
	TestTrue(TEXT("too far: step in"), FMDST_EnemyHoldThreat::ComputeHoldPoint(FVector::ZeroVector, FVector(0.0, 900.0, 0.0), 450.0f, 120.0f, Point));
	TestTrue(TEXT("in to the range"), Point.Equals(FVector(0.0, 450.0, 0.0), 1.0));
	TestFalse(TEXT("within tolerance: stay"), FMDST_EnemyHoldThreat::ComputeHoldPoint(FVector::ZeroVector, FVector(500.0, 0.0, 0.0), 450.0f, 120.0f, Point));

	// Wind-up rate: a window authored at 0.4 s plays at 0.5x to reach a 0.8 s tell; never faster than authored; floors.
	TestEqual(TEXT("0.4 s window, 0.8 s tell"), MDEnemyAttackRules::WindupRate(0.4f, 0.8f), 0.5f);
	TestEqual(TEXT("already slow enough"), MDEnemyAttackRules::WindupRate(1.0f, 0.8f), 1.0f);
	TestEqual(TEXT("minimum rate"), MDEnemyAttackRules::WindupRate(0.1f, 0.8f), 0.25f);
	TestEqual(TEXT("no window: as authored"), MDEnemyAttackRules::WindupRate(-1.0f, 0.8f), 1.0f);
	TestEqual(TEXT("no tell: as authored"), MDEnemyAttackRules::WindupRate(0.4f, 0.0f), 1.0f);

	// Attack pick: the tightest attack that reaches.
	UMDEnemyArchetype* Ghoul = MakeGhoulArchetype(30.0f);
	TestEqual(TEXT("attack range is the longest reach"), Ghoul->GetAttackRange(), 450.0f);
	TestEqual(TEXT("up close: swipe"), Ghoul->PickAttack(150.0f), 0);
	TestEqual(TEXT("mid range: lunge"), Ghoul->PickAttack(300.0f), 1);
	TestEqual(TEXT("out of reach: none"), Ghoul->PickAttack(600.0f), static_cast<int32>(INDEX_NONE));

	// Intent tags and task instance data.
	TestTrue(TEXT("chase tag"), MDIntentToTag(EMDIntent::Chase) == TAG_AI_Intent_Chase.GetTag());
	TestTrue(TEXT("attack tag"), MDIntentToTag(EMDIntent::Attack) == TAG_AI_Intent_Attack.GetTag());
	TestFalse(TEXT("no tag for a WP5 intent"), MDIntentToTag(EMDIntent::Feint).IsValid());
	TestEqual(TEXT("dead tag name"), MDIntentToTag(EMDIntent::Dead).ToString(), FString(TEXT("AI.Intent.Dead")));
	TestEqual(TEXT("intent name"), FString(MDIntentToString(EMDIntent::HoldThreat)), FString(TEXT("HoldThreat")));
	const FMDST_EnemyChase Chase;
	TestTrue(TEXT("chase task instance data"), Chase.GetInstanceDataType() == FMDSTEnemyTaskInstanceData::StaticStruct());
	const FMDST_EnemyAttack AttackTask;
	TestFalse(TEXT("attack task does not tick"), AttackTask.bShouldCallTick);
	return true;
}

// The enemy anim instance reads the pawn's velocity on every update (listen server and clients alike): the locomotion
// blend space in ABP_Ghoul is driven by Speed, so a stale Speed means an enemy gliding in its idle pose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainAnimInstanceSpeedTest, "MazeDemons.AI.Brain.AnimInstanceSpeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainAnimInstanceSpeedTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;
	FBrainFixture F;
	if (!F.Init(*this, 1))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = F.Enemies[0];
	UMDEnemyAnimInstance* Anim = NewObject<UMDEnemyAnimInstance>(Enemy->GetMesh());
	if (!TestNotNull(TEXT("anim instance"), Anim))
	{
		return false;
	}
	TestTrue(TEXT("pawn owner resolves through the mesh"), Anim->TryGetPawnOwner() == Enemy);

	Enemy->GetCharacterMovement()->Velocity = FVector(300.0, 0.0, 0.0);
	Anim->NativeUpdateAnimation(0.016f);
	TestEqual(TEXT("speed from the pawn velocity"), Anim->Speed, 300.0f, 0.01f);
	TestEqual(TEXT("fraction of the archetype MoveSpeed (520)"), Anim->SpeedFraction, 300.0f / 520.0f, 0.001f);
	TestTrue(TEXT("moving"), Anim->bMoving);
	TestFalse(TEXT("not attacking"), Anim->bAttacking);
	TestFalse(TEXT("not dead"), Anim->bDead);

	Enemy->GetCharacterMovement()->Velocity = FVector(0.0, 0.0, -400.0); // falling: horizontal speed only
	Anim->NativeUpdateAnimation(0.016f);
	TestEqual(TEXT("vertical velocity is not speed"), Anim->Speed, 0.0f, 0.01f);
	TestFalse(TEXT("not moving"), Anim->bMoving);

	Enemy->SetReplicatedIntent(EMDIntent::Chase);
	Anim->NativeUpdateAnimation(0.016f);
	TestTrue(TEXT("intent mirrors the replicated intent"), Anim->Intent == EMDIntent::Chase);
	return true;
}

// The facing rule (AMDEnemyCharacter::FaceMovement / FaceTarget) for every enemy: chase turns with the movement, holding
// the ring and winding up face the target through controller focus + desired rotation, never through controller yaw.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIBrainFacingRuleTest, "MazeDemons.AI.Brain.FacingRule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIBrainFacingRuleTest::RunTest(const FString& Parameters)
{
	using namespace MDAIBrainTestsLocal;
	FBrainFixture F;
	if (!F.Init(*this, 1))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = F.Enemies[0];
	UCharacterMovementComponent* Move = Enemy->GetCharacterMovement();
	AAIController* Controller = Cast<AAIController>(Enemy->GetController());
	if (!TestNotNull(TEXT("movement"), Move) || !TestNotNull(TEXT("AI controller"), Controller))
	{
		return false;
	}
	// Defaults: orient to movement at a brisk yaw rate, never controller yaw.
	TestFalse(TEXT("no controller yaw"), Enemy->bUseControllerRotationYaw);
	TestTrue(TEXT("default: orient to movement"), Move->bOrientRotationToMovement);
	TestFalse(TEXT("default: no desired rotation"), Move->bUseControllerDesiredRotation);
	TestTrue(TEXT("yaw rate 540..720"), Move->RotationRate.Yaw >= 540.0f && Move->RotationRate.Yaw <= 720.0f);

	Enemy->FaceTarget(F.Player);
	TestTrue(TEXT("face target: focus on the target"), Controller->GetFocusActor() == F.Player);
	TestTrue(TEXT("face target: desired rotation"), Move->bUseControllerDesiredRotation);
	TestFalse(TEXT("face target: not orient to movement"), Move->bOrientRotationToMovement);
	TestTrue(TEXT("face target: flagged"), Enemy->IsFacingTarget());

	Enemy->FaceMovement();
	TestNull(TEXT("face movement: focus cleared"), Controller->GetFocusActor());
	TestTrue(TEXT("face movement: orient to movement"), Move->bOrientRotationToMovement);
	TestFalse(TEXT("face movement: no desired rotation"), Move->bUseControllerDesiredRotation);
	TestFalse(TEXT("face movement: not flagged"), Enemy->IsFacingTarget());

	Enemy->FaceTarget(nullptr);
	TestFalse(TEXT("no target: falls back to movement facing"), Enemy->IsFacingTarget());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
