#include "CoreMinimal.h"
#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDPack.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDPackRules.h"
#include "AI/MDRallyPoint.h"
#include "HAL/IConsoleManager.h"
#include "MDAbilitySystemComponent.h"
#include "MDPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// V1 WP2 pack tests (MazeDemons.AI.Pack.*). The rules are plain structs (MDPackRules.h) and run with no world; the director
// tests use a transient world with real enemies, report their knowledge by hand and step the director with their own clock.

namespace MDAIPackTestsLocal
{
	// md.AI.ForceTier for the length of a test.
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

	struct FPackWorld
	{
		UWorld* World = nullptr;
		APawn* Player = nullptr;
		AMDPlayerState* PlayerState = nullptr;
		TArray<AMDEnemyCharacter*> Enemies;

		bool Init(FAutomationTestBase& Test, int32 NumEnemies)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIPackTestWorld")));
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

		TArray<AActor*> EnemyActors(int32 First, int32 Count) const
		{
			TArray<AActor*> Out;
			for (int32 i = First; i < First + Count && i < Enemies.Num(); ++i)
			{
				Out.Add(Enemies[i]);
			}
			return Out;
		}

		void Place(AActor* Actor, const FVector& Location) const
		{
			if (Actor)
			{
				Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		// Tells the director what each enemy knows: the player, as its target.
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

		~FPackWorld()
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

	constexpr uint32 GhoulRoles = MDPack::RoleBit(EMDRole::Striker) | MDPack::RoleBit(EMDRole::Flanker) | MDPack::RoleBit(EMDRole::Bait);

	int32 CountRole(const TArray<EMDRole>& Roles, EMDRole Role)
	{
		int32 N = 0;
		for (const EMDRole R : Roles)
		{
			N += R == Role ? 1 : 0;
		}
		return N;
	}
}

// Per-player pools: MaxTokens by tier (1/2/2/3/3), each player has its own pool, and the priority rules (Striker > Flanker >
// others, closest wins a near tie, an attacking holder is never robbed).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackTokenPoolPerPlayerTest, "MazeDemons.AI.Pack.TokenPoolPerPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackTokenPoolPerPlayerTest::RunTest(const FString& Parameters)
{
	const int32 Expected[] = { 1, 2, 2, 3, 3 };
	for (int32 Tier = 0; Tier < 5; ++Tier)
	{
		const int32 Max = FMDTierParams::ForTier(static_cast<EMDIntelligenceTier>(Tier)).MaxTokensPerPlayer;
		TestEqual(*FString::Printf(TEXT("T%d tokens per player"), Tier), Max, Expected[Tier]);

		FMDPackTokenPool Pool;
		const int32 PlayerA = 1000;
		const int32 PlayerB = 2000;
		for (int32 i = 1; i <= Max; ++i)
		{
			TestTrue(TEXT("player A token granted"), Pool.TryGrant(i, PlayerA, 0.0, Max, 3000.0f).bGranted);
		}
		TestFalse(TEXT("player A is full"), Pool.TryGrant(50, PlayerA, 0.0, Max, 3000.0f).bGranted);
		// Player B has a pool of its own.
		for (int32 i = 1; i <= Max; ++i)
		{
			TestTrue(TEXT("player B token granted"), Pool.TryGrant(100 + i, PlayerB, 0.0, Max, 3000.0f).bGranted);
		}
		TestFalse(TEXT("player B is full"), Pool.TryGrant(150, PlayerB, 0.0, Max, 3000.0f).bGranted);
		TestEqual(TEXT("A count"), Pool.CountForTarget(PlayerA, 0.1), Max);
		TestEqual(TEXT("B count"), Pool.CountForTarget(PlayerB, 0.1), Max);
		TestTrue(TEXT("a holder asking again keeps its token"), Pool.TryGrant(1, PlayerA, 0.2, Max, 3000.0f).bGranted);
		TestEqual(TEXT("still full, not over"), Pool.CountForTarget(PlayerA, 0.3), Max);
	}

	// Priority: a Striker takes the token of an uncommitted Harasser; a Flanker cannot take a Striker's.
	{
		FMDPackTokenPool Pool;
		const float Harasser = MDPackRules::TokenPriority(EMDRole::Harasser, 300.0f, 7, 1);
		const float Flanker = MDPackRules::TokenPriority(EMDRole::Flanker, 300.0f, 7, 3);
		const float Striker = MDPackRules::TokenPriority(EMDRole::Striker, 300.0f, 7, 2);
		TestTrue(TEXT("role ranks dominate"), Striker > Flanker && Flanker > Harasser);

		TestTrue(TEXT("harasser takes the token"), Pool.TryGrant(1, 10, 0.0, 1, Harasser).bGranted);
		const FMDPackTokenPool::FGrantResult Stolen = Pool.TryGrant(2, 10, 0.1, 1, Striker);
		TestTrue(TEXT("striker outranks the uncommitted harasser"), Stolen.bGranted && Stolen.EvictedHolder == 1);
		TestFalse(TEXT("the harasser lost it"), Pool.Holds(1, 0.2));
		TestFalse(TEXT("a flanker does not outrank the striker"), Pool.TryGrant(3, 10, 0.2, 1, Flanker).bGranted);

		// Once the striker is attacking nobody takes the token, whatever the priority.
		Pool.Commit(2);
		TestFalse(TEXT("an attacking holder keeps its token"), Pool.TryGrant(4, 10, 0.3, 1, Striker + 500.0f).bGranted);
		TestTrue(TEXT("and still holds"), Pool.Holds(2, 0.4));
	}

	// Tie-break: the same role, the closer enemy wins a clear gap (> 50 priority = about 10 m closer), a near tie is not stolen.
	{
		const float Near = MDPackRules::TokenPriority(EMDRole::Striker, 100.0f, 7, 5);
		const float Far = MDPackRules::TokenPriority(EMDRole::Striker, 1800.0f, 7, 6);
		const float Near2 = MDPackRules::TokenPriority(EMDRole::Striker, 150.0f, 7, 8);
		TestEqual(TEXT("the priority is deterministic for a seed"), Near, MDPackRules::TokenPriority(EMDRole::Striker, 100.0f, 7, 5));
		TestNotEqual(TEXT("the seeded noise differs per enemy"), Near, Near2);

		FMDPackTokenPool Pool;
		Pool.TryGrant(6, 10, 0.0, 1, Far);
		TestTrue(TEXT("the much closer striker takes it"), Pool.TryGrant(5, 10, 0.1, 1, Near).bGranted);
		FMDPackTokenPool Pool2;
		Pool2.TryGrant(5, 10, 0.0, 1, Near);
		TestFalse(TEXT("the far one does not take it back"), Pool2.TryGrant(6, 10, 0.1, 1, Far).bGranted);
		FMDPackTokenPool Pool3;
		Pool3.TryGrant(5, 10, 0.0, 1, Near);
		TestFalse(TEXT("a near tie does not flap the token"), Pool3.TryGrant(8, 10, 0.1, 1, Near2).bGranted);
	}

	// The same through the AI subsystem's old calls: they delegate to the director.
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(1); // two tokens per player
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 3))
		{
			UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			if (TestNotNull(TEXT("AI subsystem"), AI) && TestNotNull(TEXT("director"), Director))
			{
				TestTrue(TEXT("first"), AI->RequestAttackToken(F.Enemies[0], F.Player));
				TestTrue(TEXT("second"), AI->RequestAttackToken(F.Enemies[1], F.Player));
				TestFalse(TEXT("third is refused at T1"), AI->RequestAttackToken(F.Enemies[2], F.Player));
				TestEqual(TEXT("two tokens on the player"), AI->CountAttackTokensOn(F.Player), 2);
				TestTrue(TEXT("director agrees"), Director->HoldsToken(F.Enemies[0]) && AI->HoldsAttackToken(F.Enemies[1]));
				AI->ReleaseAttackToken(F.Enemies[0]);
				TestTrue(TEXT("a released token is free for the next one"), AI->RequestAttackToken(F.Enemies[2], F.Player));
			}
		}
	}
	return true;
}

// Expiry: attack end + 0.5 s, stagger and death at once, 4 s after the grant otherwise.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackTokenExpiryTest, "MazeDemons.AI.Pack.TokenExpiryOnStaggerDeathTimeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackTokenExpiryTest::RunTest(const FString& Parameters)
{
	// Attack ended at t = 2: the token lives 0.5 s longer.
	{
		FMDPackTokenPool Pool;
		Pool.TryGrant(1, 10, 1.0, 1, 3000.0f);
		Pool.Commit(1);
		Pool.ReleaseAfter(1, 2.0, 0.5);
		TestTrue(TEXT("held just before the 0.5 s are up"), Pool.Holds(1, 2.49));
		TestFalse(TEXT("void once they are"), Pool.Holds(1, 2.51));
		TestTrue(TEXT("so the next enemy can take it"), Pool.TryGrant(2, 10, 2.51, 1, 3000.0f).bGranted);
	}
	// The hold never extends a token past its own timeout.
	{
		FMDPackTokenPool Pool;
		Pool.TryGrant(1, 10, 0.0, 1, 3000.0f);
		Pool.ReleaseAfter(1, 3.9, 0.5);
		TestFalse(TEXT("the 4 s timeout still wins"), Pool.Holds(1, 4.01));
	}
	// Stagger and death hand it back at once (Release).
	{
		FMDPackTokenPool Pool;
		Pool.TryGrant(1, 10, 0.0, 1, 3000.0f);
		TestTrue(TEXT("held"), Pool.Holds(1, 0.5));
		Pool.Release(1);
		TestFalse(TEXT("released on stagger or death"), Pool.Holds(1, 0.5));
		TestTrue(TEXT("free at once for another enemy"), Pool.TryGrant(2, 10, 0.5, 1, 3000.0f).bGranted);
	}
	// Timeout: 4 s after the grant, whatever happened.
	{
		FMDPackTokenPool Pool;
		Pool.TryGrant(1, 10, 1.0, 1, 3000.0f);
		TestTrue(TEXT("held at 4.9 s"), Pool.Holds(1, 4.99));
		TestFalse(TEXT("void at 5.0 s (grant + 4 s)"), Pool.Holds(1, 5.01));
		Pool.Purge(5.01);
		TestEqual(TEXT("purged"), Pool.Num(), 0);
	}
	// The same through a real enemy: a staggered or dead enemy returns its token to the director.
	{
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 2))
		{
			UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			if (AI && Director)
			{
				AI->RequestAttackToken(F.Enemies[0], F.Player);
				TestTrue(TEXT("granted"), AI->HoldsAttackToken(F.Enemies[0]));
				Director->UnregisterEnemy(F.Enemies[0]); // what the brain does on death
				TestFalse(TEXT("a dead enemy holds no token"), AI->HoldsAttackToken(F.Enemies[0]));
				TestEqual(TEXT("and the player has none on it"), AI->CountAttackTokensOn(F.Player), 0);
			}
		}
	}
	return true;
}

// Ring slots: 6 slots 60 degrees apart at 1.8 - 2.6 m; flankers take the slots behind the player; nobody shares a slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackRingSlotsEvenlySpacedTest, "MazeDemons.AI.Pack.RingSlotsEvenlySpaced", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackRingSlotsEvenlySpacedTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("six ring slots"), MDPack::NumRingSlots, 6);
	for (const float Base : { 0.0f, 30.0f, 123.0f, 300.0f })
	{
		for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
		{
			const float Next = MDPackRules::SlotAngle((Slot + 1) % MDPack::NumRingSlots, Base);
			TestEqual(TEXT("neighbouring slots are 60 degrees apart"), MDPackRules::AngleDiff(MDPackRules::SlotAngle(Slot, Base), Next), 60.0f, 0.01f);
		}
	}

	// On the ground: every slot point is the same distance from the target, inside 1.8 - 2.6 m.
	const FVector Target(1000.0, -400.0, 40.0);
	const float Radius = MDPackRules::SlotRadiusFor(180.0f, 220.0f);
	TestTrue(TEXT("slot radius within 1.8 - 2.6 m"), Radius >= MDPack::SlotRadiusMin && Radius <= MDPack::SlotRadiusMax);
	for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
	{
		const FVector P = MDPackRules::SlotLocation(Target, Slot, 17.0f, Radius);
		TestEqual(TEXT("slot is on the ring"), static_cast<float>(FVector::Dist2D(P, Target)), Radius, 0.01f);
	}
	TestEqual(TEXT("a short preferred range clamps up to 1.8 m"), MDPackRules::SlotRadiusFor(100.0f, 400.0f), 180.0f);
	TestEqual(TEXT("a long preferred range clamps down to 2.6 m"), MDPackRules::SlotRadiusFor(900.0f, 800.0f), 260.0f);
	TestTrue(TEXT("the slot stays inside the attack reach"), MDPackRules::SlotRadiusFor(260.0f, 220.0f) <= 220.0f);
	TestEqual(TEXT("threat ring clamps to 4 m"), MDPackRules::ThreatRadiusFor(150.0f), 400.0f);
	TestEqual(TEXT("threat ring keeps 4.5 m"), MDPackRules::ThreatRadiusFor(450.0f), 450.0f);
	TestEqual(TEXT("threat ring clamps to 6 m"), MDPackRules::ThreatRadiusFor(1500.0f), 600.0f);

	// Assignment: 5 strikers + 1 flanker (the flanker is in front of the player and must still go behind it).
	TArray<FMDSlotMember> Members;
	for (int32 i = 0; i < 6; ++i)
	{
		FMDSlotMember& M = Members.AddDefaulted_GetRef();
		M.Id = i + 1;
		M.Role = i == 2 ? EMDRole::Flanker : EMDRole::Striker;
		M.AngleDeg = static_cast<float>(i) * 10.0f; // all bunched in front of the player
	}
	TArray<int32> Slots;
	const float FacingYaw = 0.0f;
	MDPackRules::AssignSlots(Members, 0.0f, FacingYaw, Slots);
	TSet<int32> Used;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		TestTrue(TEXT("every member of six gets a slot"), Slots[i] >= 0 && Slots[i] < MDPack::NumRingSlots);
		Used.Add(Slots[i]);
	}
	TestEqual(TEXT("six distinct slots"), Used.Num(), 6);
	TestTrue(TEXT("the flanker took a slot at least 120 degrees from the player's facing"), MDPackRules::SlotAngleFromFacing(Slots[2], 0.0f, FacingYaw) >= MDPack::FlankMinAngle);

	// The facing rotates: the flanker follows the "behind" slots.
	MDPackRules::AssignSlots(Members, 0.0f, 180.0f, Slots);
	TestTrue(TEXT("facing the other way, the flanker still sits behind"), MDPackRules::SlotAngleFromFacing(Slots[2], 0.0f, 180.0f) >= MDPack::FlankMinAngle);

	// A seventh member has no slot (overflow stays on the threat ring).
	FMDSlotMember& Extra = Members.AddDefaulted_GetRef();
	Extra.Id = 99;
	MDPackRules::AssignSlots(Members, 0.0f, FacingYaw, Slots);
	TestEqual(TEXT("the overflow member has no slot"), Slots.Last(), -1);

	// Stable: with current slots filled in, the same inputs give the same slots (no shuffling).
	Members.RemoveAt(Members.Num() - 1);
	MDPackRules::AssignSlots(Members, 0.0f, FacingYaw, Slots);
	TArray<int32> First = Slots;
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		Members[i].CurrentSlot = First[i];
		Members[i].AngleDeg = MDPackRules::SlotAngle(First[i], 0.0f);
	}
	MDPackRules::AssignSlots(Members, 0.0f, FacingYaw, Slots);
	TestTrue(TEXT("members keep the slots they stand on"), Slots == First);
	return true;
}

// Focus target: the isolated player scores above the grouped one; low health, staggered and threat add; crowding subtracts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackFocusTargetPrefersIsolatedTest, "MazeDemons.AI.Pack.FocusTargetPrefersIsolated", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackFocusTargetPrefersIsolatedTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("no other player = fully isolated"), MDPackRules::NormalizeIsolation(-1.0f), 1.0f);
	TestEqual(TEXT("standing together = 0"), MDPackRules::NormalizeIsolation(0.0f), 0.0f);
	TestEqual(TEXT("half way"), MDPackRules::NormalizeIsolation(750.0f), 0.5f);
	TestEqual(TEXT("saturates at 15 m"), MDPackRules::NormalizeIsolation(5000.0f), 1.0f);

	FMDFocusInputs Isolated;
	Isolated.Isolation = MDPackRules::NormalizeIsolation(2000.0f);
	FMDFocusInputs Grouped;
	Grouped.Isolation = MDPackRules::NormalizeIsolation(200.0f);
	const float IsolatedScore = MDPackRules::FocusScore(Isolated);
	const float GroupedScore = MDPackRules::FocusScore(Grouped);
	TestTrue(TEXT("the isolated player outscores the grouped one"), IsolatedScore > GroupedScore);

	TArray<float> Scores = { GroupedScore, IsolatedScore };
	TestEqual(TEXT("the pack picks the isolated player"), MDPackRules::PickFocus(Scores, INDEX_NONE), 1);
	TestEqual(TEXT("and keeps it when it is the current one"), MDPackRules::PickFocus(Scores, 1), 1);

	// The exact weights (2.4): 0.35 isolation, 0.25 health, 0.15 staggered, 0.15 threat, -0.10 crowding.
	FMDFocusInputs All;
	All.Isolation = 1.0f;
	All.HealthFraction = 0.0f;
	All.bStaggered = true;
	All.Threat = 1.0f;
	All.Crowding = 0.0f;
	TestEqual(TEXT("all four positive terms"), MDPackRules::FocusScore(All), 0.90f, 0.0001f);
	All.Crowding = 1.0f;
	TestEqual(TEXT("crowding takes 0.10 off"), MDPackRules::FocusScore(All), 0.80f, 0.0001f);
	FMDFocusInputs Healthy;
	FMDFocusInputs Hurt;
	Hurt.HealthFraction = 0.2f;
	TestEqual(TEXT("health weight is 0.25 on the missing health"), MDPackRules::FocusScore(Hurt) - MDPackRules::FocusScore(Healthy), 0.25f * 0.8f, 0.0001f);

	// A hurt player standing in the group can beat a healthy one who is only slightly more isolated, but not a fully
	// isolated one at full health with a large gap.
	FMDFocusInputs HurtGrouped;
	HurtGrouped.Isolation = MDPackRules::NormalizeIsolation(200.0f);
	HurtGrouped.HealthFraction = 0.1f;
	FMDFocusInputs HealthySlightlyAlone;
	HealthySlightlyAlone.Isolation = MDPackRules::NormalizeIsolation(500.0f);
	TestTrue(TEXT("a nearly dead player draws the pack"), MDPackRules::FocusScore(HurtGrouped) > MDPackRules::FocusScore(HealthySlightlyAlone));

	// Anti flip-flop: a rival only just ahead does not take the focus; a clear lead does.
	TArray<float> Close = { 0.50f, 0.52f };
	TestEqual(TEXT("a 0.02 lead is not enough"), MDPackRules::PickFocus(Close, 0), 0);
	TArray<float> Clear = { 0.50f, 0.60f };
	TestEqual(TEXT("a 0.10 lead is"), MDPackRules::PickFocus(Clear, 0), 1);
	TestEqual(TEXT("no candidates"), MDPackRules::PickFocus(TArray<float>(), INDEX_NONE), static_cast<int32>(INDEX_NONE));

	// Through the director with a real player pawn: the pack focuses the only known player.
	{
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 2))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			if (TestNotNull(TEXT("director"), Director))
			{
				const int32 PackId = Director->RegisterSpawnGroup(F.EnemyActors(0, 2), 0, TEXT("Room"));
				F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
				Director->StepPacks(5.0);
				const FMDPack* Pack = Director->FindPack(PackId);
				if (TestNotNull(TEXT("pack"), Pack))
				{
					TestTrue(TEXT("the pack's focus is the known player"), Pack->FocusTarget.Get() == F.Player);
				}
			}
		}
	}
	return true;
}

// Re-target interval per tier (3.2): T0 never, T1 6 s, T2 3 s, T3 2 s, T4 1.5 s; a lost focus is replaced at once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackRetargetIntervalPerTierTest, "MazeDemons.AI.Pack.RetargetIntervalPerTier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackRetargetIntervalPerTierTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("T0 never re-targets"), MDPackRules::RetargetInterval(EMDIntelligenceTier::T0) < 0.0f);
	TestEqual(TEXT("T1"), MDPackRules::RetargetInterval(EMDIntelligenceTier::T1), 6.0f);
	TestEqual(TEXT("T2"), MDPackRules::RetargetInterval(EMDIntelligenceTier::T2), 3.0f);
	TestEqual(TEXT("T3"), MDPackRules::RetargetInterval(EMDIntelligenceTier::T3), 2.0f);
	TestEqual(TEXT("T4"), MDPackRules::RetargetInterval(EMDIntelligenceTier::T4), 1.5f);

	TestFalse(TEXT("T0 keeps a valid focus for ever"), MDPackRules::CanRetarget(EMDIntelligenceTier::T0, 1000.0, true));
	TestTrue(TEXT("T0 still picks when it has none"), MDPackRules::CanRetarget(EMDIntelligenceTier::T0, 0.0, false));
	TestFalse(TEXT("T1 at 5.9 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T1, 5.9, true));
	TestTrue(TEXT("T1 at 6.0 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T1, 6.0, true));
	TestFalse(TEXT("T2 at 2.9 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T2, 2.9, true));
	TestTrue(TEXT("T2 at 3.0 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T2, 3.0, true));
	TestFalse(TEXT("T3 at 1.9 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T3, 1.9, true));
	TestTrue(TEXT("T3 at 2.0 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T3, 2.0, true));
	TestFalse(TEXT("T4 at 1.4 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T4, 1.4, true));
	TestTrue(TEXT("T4 at 1.5 s"), MDPackRules::CanRetarget(EMDIntelligenceTier::T4, 1.5, true));
	TestTrue(TEXT("an invalid focus is replaced at once at T4"), MDPackRules::CanRetarget(EMDIntelligenceTier::T4, 0.0, false));

	// Through the director: two known players, the pack keeps its first pick until the tier's interval is up.
	for (const int32 TierValue : { 1, 2 })
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(TierValue);
		MDAIPackTestsLocal::FPackWorld F;
		if (!F.Init(*this, 2))
		{
			continue;
		}
		UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
		APawn* Second = F.World->SpawnActor<APawn>();
		AMDPlayerState* SecondState = F.World->SpawnActor<AMDPlayerState>();
		if (!Director || !Second || !SecondState)
		{
			AddError(TEXT("could not set up the second player"));
			continue;
		}
		SecondState->SetRole(ROLE_Authority);
		Second->SetPlayerState(SecondState);
		if (UMDAbilitySystemComponent* ASC = SecondState->GetMDAbilitySystemComponent())
		{
			ASC->InitAbilityActorInfo(SecondState, Second);
			ASC->RegisterAttributeSet(SecondState->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
		}
		const int32 PackId = Director->RegisterSpawnGroup(F.EnemyActors(0, 2), 0, TEXT("Room"));
		for (AMDEnemyCharacter* Enemy : F.Enemies)
		{
			FMDMemberReport Report;
			Report.Target = F.Player;
			Report.Known.Add(F.Player);
			Report.Known.Add(Second);
			Report.AllowedMask = MDAIPackTestsLocal::GhoulRoles;
			Director->ReportMember(Enemy, Report);
		}
		Director->StepPacks(10.0);
		const FMDPack* Pack = Director->FindPack(PackId);
		if (!TestNotNull(TEXT("pack"), Pack))
		{
			continue;
		}
		AActor* First = Pack->FocusTarget.Get();
		TestNotNull(TEXT("the pack picked a focus"), First);
		// The pack keeps its pick until the tier's interval is up, then re-scores (its re-target time moves).
		const float Interval = MDPackRules::RetargetInterval(static_cast<EMDIntelligenceTier>(TierValue));
		Director->StepPacks(10.0 + Interval - 0.5);
		TestTrue(TEXT("focus unchanged before the interval is up"), Pack->FocusTarget.Get() == First);
		Director->StepPacks(10.0 + Interval + 0.1);
		TestTrue(TEXT("a valid focus is still valid after re-scoring"), Pack->FocusTarget.IsValid());
		TestEqual(TEXT("last re-target time moved to the re-score"), static_cast<float>(Pack->LastRetargetTime), static_cast<float>(10.0 + Interval + 0.1), 0.001f);
	}
	return true;
}

// Retreat: alive <= 40% of the spawn count at T2+ (50% at T3/T4), only with a rally point; never at T0/T1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackRetreatThresholdTest, "MazeDemons.AI.Pack.RetreatThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackRetreatThresholdTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("T0 never retreats"), MDPackRules::RetreatFraction(EMDIntelligenceTier::T0) < 0.0f);
	TestTrue(TEXT("T1 never retreats"), MDPackRules::RetreatFraction(EMDIntelligenceTier::T1) < 0.0f);
	TestEqual(TEXT("T2 40%"), MDPackRules::RetreatFraction(EMDIntelligenceTier::T2), 0.4f);
	TestEqual(TEXT("T3 50%"), MDPackRules::RetreatFraction(EMDIntelligenceTier::T3), 0.5f);
	TestEqual(TEXT("T4 50%"), MDPackRules::RetreatFraction(EMDIntelligenceTier::T4), 0.5f);

	TestFalse(TEXT("T2: 3 of 5 alive is above 40%"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T2, 3, 5, true));
	TestTrue(TEXT("T2: 2 of 5 alive is exactly 40%"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T2, 2, 5, true));
	TestTrue(TEXT("T2: 1 of 5 alive"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T2, 1, 5, true));
	TestFalse(TEXT("T2: nobody left, nobody to retreat"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T2, 0, 5, true));
	TestFalse(TEXT("no rally point, no retreat"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T2, 1, 5, false));
	TestFalse(TEXT("T1 never, even with 1 of 5"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T1, 1, 5, true));
	TestFalse(TEXT("T0 never"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T0, 1, 5, true));
	TestFalse(TEXT("T3: 3 of 5 is above 50%"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T3, 3, 5, true));
	TestTrue(TEXT("T3: 2 of 5"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T3, 2, 5, true));
	TestTrue(TEXT("T4: 1 of 2 is 50%"), MDPackRules::ShouldRetreat(EMDIntelligenceTier::T4, 1, 2, true));

	// Pack state machine: Engage -> Regroup -> (time up, still low) Routed; Pressing with all tokens out; recovery -> Engage.
	using R = EMDPackState;
	const EMDIntelligenceTier T2 = EMDIntelligenceTier::T2;
	TestEqual(TEXT("healthy pack engages"), static_cast<int32>(MDPackRules::ResolvePackState(R::Engage, T2, 5, 5, true, false, false, 0, 2)), static_cast<int32>(R::Engage));
	TestEqual(TEXT("all tokens out = pressing"), static_cast<int32>(MDPackRules::ResolvePackState(R::Engage, T2, 5, 5, true, false, false, 2, 2)), static_cast<int32>(R::Pressing));
	TestEqual(TEXT("below the threshold with a rally point = regroup"), static_cast<int32>(MDPackRules::ResolvePackState(R::Engage, T2, 2, 5, true, false, false, 0, 2)), static_cast<int32>(R::Regroup));
	TestEqual(TEXT("regroup holds until the time is up"), static_cast<int32>(MDPackRules::ResolvePackState(R::Regroup, T2, 2, 5, true, true, false, 0, 2)), static_cast<int32>(R::Regroup));
	TestEqual(TEXT("time up, still low: back in the fight as Routed"), static_cast<int32>(MDPackRules::ResolvePackState(R::Regroup, T2, 2, 5, true, true, true, 0, 2)), static_cast<int32>(R::Routed));
	TestEqual(TEXT("reinforcements end the regroup"), static_cast<int32>(MDPackRules::ResolvePackState(R::Regroup, T2, 4, 5, true, true, false, 0, 2)), static_cast<int32>(R::Engage));
	TestEqual(TEXT("no rally point = routed"), static_cast<int32>(MDPackRules::ResolvePackState(R::Engage, T2, 2, 5, false, false, false, 0, 2)), static_cast<int32>(R::Routed));
	TestEqual(TEXT("T1 is never routed"), static_cast<int32>(MDPackRules::ResolvePackState(R::Engage, EMDIntelligenceTier::T1, 1, 5, true, false, false, 0, 2)), static_cast<int32>(R::Engage));

	// The director: 5 ghouls at T2, a rally point; 3 die -> the pack regroups at the rally point and comes back.
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(2);
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 5))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			AMDRallyPoint* Rally = F.World->SpawnActor<AMDRallyPoint>(FVector(3000.0, 0.0, 0.0), FRotator::ZeroRotator);
			if (TestNotNull(TEXT("director"), Director) && TestNotNull(TEXT("rally point"), Rally))
			{
				TestTrue(TEXT("the rally point carries the tag"), Rally->ActorHasTag(AMDRallyPoint::RallyTag));
				for (int32 i = 0; i < 5; ++i)
				{
					F.Place(F.Enemies[i], FVector(600.0 + 80.0 * i, 0.0, 0.0));
				}
				const int32 PackId = Director->RegisterSpawnGroup(F.EnemyActors(0, 5), 0, TEXT("Zone2"));
				F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
				Director->StepPacks(10.0);

				const FMDPack* Pack = Director->FindPack(PackId);
				if (TestNotNull(TEXT("pack"), Pack))
				{
					TestEqual(TEXT("spawn count"), Pack->SpawnCount, 5);
					TestEqual(TEXT("alive"), Pack->NumAlive(), 5);
					TestEqual(TEXT("state"), static_cast<int32>(Pack->State), static_cast<int32>(R::Engage));

					// Roles at T2 with 5 alive: one Bait, one Flanker, three Strikers.
					TArray<EMDRole> Roles;
					for (const FMDPackMember& M : Pack->Members)
					{
						Roles.Add(M.Role);
					}
					TestEqual(TEXT("one bait"), MDAIPackTestsLocal::CountRole(Roles, EMDRole::Bait), 1);
					TestEqual(TEXT("one flanker per player at T2"), MDAIPackTestsLocal::CountRole(Roles, EMDRole::Flanker), 1);
					TestEqual(TEXT("three strikers"), MDAIPackTestsLocal::CountRole(Roles, EMDRole::Striker), 3);

					// Two die: 3 of 5 alive, still fighting. Roles are reassigned: with 3 alive the bait stays, it is still a pack of 3.
					Director->UnregisterEnemy(F.Enemies[0]);
					Director->UnregisterEnemy(F.Enemies[1]);
					Director->StepPacks(10.5);
					TestEqual(TEXT("3 of 5 alive: no retreat"), static_cast<int32>(Pack->State), static_cast<int32>(R::Engage));

					// The third dies: 2 of 5 = 40%.
					Director->UnregisterEnemy(F.Enemies[2]);
					Director->StepPacks(11.0);
					TestEqual(TEXT("2 of 5 alive at T2: regroup"), static_cast<int32>(Pack->State), static_cast<int32>(R::Regroup));
					{
						TArray<EMDRole> Left;
						for (const FMDPackMember& M : Pack->Members)
						{
							Left.Add(M.Role);
						}
						TestEqual(TEXT("with 2 alive both are strikers (no bait)"), MDAIPackTestsLocal::CountRole(Left, EMDRole::Striker), 2);
					}

					FMDEnemyAssignment Assignment;
					if (TestTrue(TEXT("a survivor has an assignment"), Director->GetAssignment(F.Enemies[3], Assignment)))
					{
						TestTrue(TEXT("it is told to retreat"), Assignment.bHasRally);
						TestEqual(TEXT("to the rally point"), static_cast<float>(Assignment.RallyPoint.X), 3000.0f);
						TestEqual(TEXT("pack state in the assignment"), static_cast<int32>(Assignment.PackState), static_cast<int32>(R::Regroup));
					}
					TestFalse(TEXT("a retreating pack hands out no tokens"), Director->RequestToken(F.Enemies[3], F.Player));

					// The regroup runs 8 s, then the pack comes back (no longer retreating).
					Director->StepPacks(11.0 + MDPack::RegroupTime - 1.0);
					TestEqual(TEXT("still regrouping before the time is up"), static_cast<int32>(Pack->State), static_cast<int32>(R::Regroup));
					Director->StepPacks(11.0 + MDPack::RegroupTime + 0.5);
					TestTrue(TEXT("regroup is over"), Pack->State != R::Regroup);
					if (Director->GetAssignment(F.Enemies[3], Assignment))
					{
						TestFalse(TEXT("no rally order after coming back"), Assignment.bHasRally);
					}
				}
			}
		}
	}

	// No rally point in the level: the same losses leave the pack Routed, fighting on.
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(2);
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 5))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			if (TestNotNull(TEXT("director"), Director))
			{
				const int32 PackId = Director->RegisterSpawnGroup(F.EnemyActors(0, 5), 0, TEXT("Zone2"));
				F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
				Director->UnregisterEnemy(F.Enemies[0]);
				Director->UnregisterEnemy(F.Enemies[1]);
				Director->UnregisterEnemy(F.Enemies[2]);
				Director->StepPacks(10.0);
				if (const FMDPack* Pack = Director->FindPack(PackId))
				{
					TestEqual(TEXT("no rally point: routed, not regrouping"), static_cast<int32>(Pack->State), static_cast<int32>(R::Routed));
				}
			}
		}
	}

	// At T0 the same losses change nothing.
	{
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 5))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			AMDRallyPoint* Rally = F.World->SpawnActor<AMDRallyPoint>(FVector(3000.0, 0.0, 0.0), FRotator::ZeroRotator);
			if (Director && Rally)
			{
				const int32 PackId = Director->RegisterSpawnGroup(F.EnemyActors(0, 5), 0, TEXT("Zone2"));
				F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
				Director->UnregisterEnemy(F.Enemies[0]);
				Director->UnregisterEnemy(F.Enemies[1]);
				Director->UnregisterEnemy(F.Enemies[2]);
				Director->UnregisterEnemy(F.Enemies[3]);
				Director->StepPacks(10.0);
				if (const FMDPack* Pack = Director->FindPack(PackId))
				{
					TestEqual(TEXT("T0 never retreats, even down to one"), static_cast<int32>(Pack->State), static_cast<int32>(R::Engage));
				}
			}
		}
	}
	return true;
}

// Merge: at T2+, packs whose members are within 10 m and have seen the same target become one pack (roles reassigned);
// at T0/T1, farther than 10 m or with different targets they stay apart.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIPackMergeWithin10mTest, "MazeDemons.AI.Pack.MergeWithin10m", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIPackMergeWithin10mTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("T2, 9.9 m, same target: merge"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T2, 990.0f, true));
	TestTrue(TEXT("T2, exactly 10 m: merge"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T2, 1000.0f, true));
	TestFalse(TEXT("T2, 10.1 m: no"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T2, 1010.0f, true));
	TestFalse(TEXT("T2, close but no shared target: no"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T2, 300.0f, false));
	TestFalse(TEXT("T1: packs never merge"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T1, 300.0f, true));
	TestFalse(TEXT("T0: packs never merge"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T0, 300.0f, true));
	TestTrue(TEXT("T4 merges too"), MDPackRules::ShouldMerge(EMDIntelligenceTier::T4, 300.0f, true));

	// Four enemies in two groups of two. Group A at x = 0, group B at x = 900 (9 m): they merge at T2; at x = 1500 they do not.
	for (const double Gap : { 900.0, 1500.0 })
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(2);
		MDAIPackTestsLocal::FPackWorld F;
		if (!F.Init(*this, 4))
		{
			continue;
		}
		UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
		if (!TestNotNull(TEXT("director"), Director))
		{
			continue;
		}
		F.Place(F.Enemies[0], FVector(0.0, 0.0, 0.0));
		F.Place(F.Enemies[1], FVector(0.0, 100.0, 0.0));
		F.Place(F.Enemies[2], FVector(Gap, 0.0, 0.0));
		F.Place(F.Enemies[3], FVector(Gap, 100.0, 0.0));
		const int32 PackA = Director->RegisterSpawnGroup(F.EnemyActors(0, 2), 0, TEXT("A"));
		const int32 PackB = Director->RegisterSpawnGroup(F.EnemyActors(2, 2), 0, TEXT("B"));
		TestTrue(TEXT("two different packs"), PackA != PackB);
		F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
		TestEqual(TEXT("two packs before the step"), Director->NumPacks(), 2);
		Director->StepPacks(10.0);
		if (Gap < MDPack::MergeDistance)
		{
			TestEqual(TEXT("within 10 m: one pack"), Director->NumPacks(), 1);
			if (const FMDPack* Merged = Director->FindPack(PackA))
			{
				TestEqual(TEXT("the older pack keeps its id and takes everyone"), Merged->NumAlive(), 4);
				TestEqual(TEXT("spawn counts add up"), Merged->SpawnCount, 4);
				TArray<EMDRole> Roles;
				for (const FMDPackMember& M : Merged->Members)
				{
					Roles.Add(M.Role);
				}
				TestEqual(TEXT("roles were reassigned for the merged pack: one bait"), MDAIPackTestsLocal::CountRole(Roles, EMDRole::Bait), 1);
			}
			else
			{
				AddError(TEXT("the older pack id is gone after the merge"));
			}
			TestEqual(TEXT("an enemy of the absorbed pack reports the surviving id"), Director->GetPackIdOf(F.Enemies[3]), PackA);
		}
		else
		{
			TestEqual(TEXT("beyond 10 m: still two packs"), Director->NumPacks(), 2);
		}
	}

	// Close packs that saw different players stay apart.
	{
		MDAIPackTestsLocal::FScopedForceTier Tier(2);
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 2))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			APawn* Other = F.World->SpawnActor<APawn>();
			if (Director && Other)
			{
				F.Place(F.Enemies[0], FVector(0.0, 0.0, 0.0));
				F.Place(F.Enemies[1], FVector(300.0, 0.0, 0.0));
				Director->RegisterSpawnGroup(F.EnemyActors(0, 1), 0, TEXT("A"));
				Director->RegisterSpawnGroup(F.EnemyActors(1, 1), 0, TEXT("B"));
				FMDMemberReport ReportA;
				ReportA.Known.Add(F.Player);
				ReportA.Target = F.Player;
				FMDMemberReport ReportB;
				ReportB.Known.Add(Other);
				ReportB.Target = Other;
				Director->ReportMember(F.Enemies[0], ReportA);
				Director->ReportMember(F.Enemies[1], ReportB);
				Director->StepPacks(10.0);
				TestEqual(TEXT("different targets: no merge"), Director->NumPacks(), 2);
			}
		}
	}

	// At T0 nothing merges.
	{
		MDAIPackTestsLocal::FPackWorld F;
		if (F.Init(*this, 2))
		{
			UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
			if (Director)
			{
				F.Place(F.Enemies[0], FVector(0.0, 0.0, 0.0));
				F.Place(F.Enemies[1], FVector(300.0, 0.0, 0.0));
				Director->RegisterSpawnGroup(F.EnemyActors(0, 1), 0, TEXT("A"));
				Director->RegisterSpawnGroup(F.EnemyActors(1, 1), 0, TEXT("B"));
				F.ReportAll(*Director, MDAIPackTestsLocal::GhoulRoles);
				Director->StepPacks(10.0);
				TestEqual(TEXT("T0: no merge"), Director->NumPacks(), 2);
			}
		}
	}
	return true;
}
