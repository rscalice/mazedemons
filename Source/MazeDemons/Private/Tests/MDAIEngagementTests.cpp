#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDAIVoice.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDPackRules.h"
#include "AI/MDPerceptionRules.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

// Playtest fixes (2026-10-28): the pack that never attacked, the stacked growls, and the ghoul that must never teleport.
//   MazeDemons.AI.Engagement.TokenReachAndTimeout     the request reach covers the threat ring; no token outlives its timeout
//   MazeDemons.AI.Engagement.PackResumesAttacking     a pack around a player it "lost" (sight blocked) senses it again and
//                                                     keeps taking tokens: never a permanent zero-attack state
//   MazeDemons.AI.Engagement.GhoulNeverTeleports      the base ghoul cannot blink or charge; the bait half-step is a walk
//   MazeDemons.AI.Voice.GateRules                     tells / barks / stings: concurrency, per-enemy cooldown, listener throttle

namespace MDAIEngagementTestsLocal
{
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

	// DA_Archetype_Ghoul as it stands: one 170 cm swipe, threat range 450, no blink, no charge.
	UMDEnemyArchetype* MakeGhoul()
	{
		UMDEnemyArchetype* A = NewObject<UMDEnemyArchetype>(GetTransientPackage());
		A->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
		A->MaxHealth = 60.0f;
		A->MaxPoise = 30.0f;
		A->MoveSpeed = 520.0f;
		A->AIParams.PreferredRange = 180.0f;
		A->AIParams.ThreatRange = 450.0f;
		A->AIParams.AllowedRoles = { EMDRole::Striker, EMDRole::Flanker, EMDRole::Bait };
		A->AIParams.bCanBlink = false;
		A->AIParams.bCanCharge = false;
		FMDEnemyAttack& Swipe = A->Attacks.AddDefaulted_GetRef();
		Swipe.Name = TEXT("Swipe");
		Swipe.Range = 170.0f;
		Swipe.TellTime = 0.8f;
		Swipe.Cooldown = 0.8f;
		return A;
	}

	struct FWorldFx
	{
		UWorld* World = nullptr;
		APawn* Player = nullptr;
		AMDPlayerState* PlayerState = nullptr;
		TArray<AMDEnemyCharacter*> Enemies;
		UMDEnemyArchetype* Archetype = nullptr;

		bool Init(FAutomationTestBase& Test, int32 NumEnemies)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIEngagementTestWorld")));
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
			Player->SetRootComponent(MakeRoot(Player));
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
			const FName Group(TEXT("EngagementPack"));
			for (int32 i = 0; i < NumEnemies; ++i)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AMDEnemyCharacter* Enemy = World->SpawnActorDeferred<AMDEnemyCharacter>(AMDEnemyCharacter::StaticClass(), FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Enemy)
				{
					Test.AddError(TEXT("Could not spawn an enemy"));
					return false;
				}
				Enemy->Archetype = Archetype;
				Enemy->SpawnGroupId = Group; // one pack (the ring slots and the swarm need it)
				Enemy->FinishSpawning(FTransform::Identity);
				Enemy->ApplyArchetype(Archetype);
				if (Enemy->GetCharacterMovement())
				{
					Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying); // no floor in the test world
				}
				Enemies.Add(Enemy);
			}
			if (UMDAISubsystem* AI = UMDAISubsystem::Get(World))
			{
				AI->TrackExtraPlayer(Player);
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

	int32 CountHolders(const FWorldFx& F)
	{
		int32 N = 0;
		for (int32 i = 0; i < F.Enemies.Num(); ++i)
		{
			const UMDEnemyBrainComponent* B = F.Brain(i);
			N += (B && B->HoldsAttackToken()) ? 1 : 0;
		}
		return N;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Token reach and timeout (pure)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIEngagementTokenReachTest, "MazeDemons.AI.Engagement.TokenReachAndTimeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIEngagementTokenReachTest::RunTest(const FString& Parameters)
{
	// The ghoul: attack 170, threat 450. Before the fix the reach was 1.5 x 170 = 255 cm while the pack held the threat ring
	// at 450 cm: nobody could ask. The reach now covers the ring with a margin.
	const float Reach = MDAI::TokenRequestReach(170.0f, 450.0f, false, MDBehaviour::ChargeMaxDistance);
	TestEqual(TEXT("ghoul reach = threat range + margin"), Reach, 450.0f + MDAI::TokenReachBeyondThreat);
	TestTrue(TEXT("the threat ring (ThreatRadiusMax) is inside the reach"), Reach >= MDPack::ThreatRadiusMax);
	TestTrue(TEXT("a charger asks from charge range"), MDAI::TokenRequestReach(240.0f, 450.0f, true, MDBehaviour::ChargeMaxDistance) >= MDBehaviour::ChargeMaxDistance);
	TestEqual(TEXT("a long reach still wins"), MDAI::TokenRequestReach(1100.0f, 450.0f, false, MDBehaviour::ChargeMaxDistance), 1650.0f);

	// No token outlives its timeout: grant, refresh, commit and ReleaseAfter never push the expiry past Now + Timeout.
	FMDPackTokenPool Pool;
	TestTrue(TEXT("grant"), Pool.TryGrant(1, 10, 0.0, 2, 1.0f).bGranted);
	TestTrue(TEXT("max left = timeout"), Pool.MaxSecondsLeft(0.0) <= 4.0 + KINDA_SMALL_NUMBER);
	Pool.Commit(1);
	TestTrue(TEXT("commit does not extend"), Pool.MaxSecondsLeft(0.0) <= 4.0 + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("refresh at 3 s"), Pool.TryGrant(1, 10, 3.0, 2, 1.0f).bGranted);
	TestTrue(TEXT("refreshed: 4 s from the refresh, no more"), Pool.MaxSecondsLeft(3.0) <= 4.0 + KINDA_SMALL_NUMBER);
	Pool.ReleaseAfter(1, 3.0, 0.5);
	TestTrue(TEXT("release-after shortens it"), Pool.MaxSecondsLeft(3.0) <= 0.5 + KINDA_SMALL_NUMBER);
	TestFalse(TEXT("gone after the hold"), Pool.Holds(1, 3.6));
	TestTrue(TEXT("an abandoned token expires on its own"), Pool.TryGrant(2, 10, 10.0, 2, 1.0f).bGranted);
	TestFalse(TEXT("... 4 s later it is void"), Pool.Holds(2, 14.01));
	int32 Valid = 0;
	Pool.ForEachValid(10.5, [&](int32, int32, double, bool) { ++Valid; });
	TestEqual(TEXT("ForEachValid lists the live token"), Valid, 1);

	FMDAttackTokenPool Basic;
	TestTrue(TEXT("basic grant"), Basic.TryGrant(1, 10, 0.0, 1));
	TestTrue(TEXT("basic max left"), Basic.MaxSecondsLeft(0.0) <= MDAI::TokenTimeout + KINDA_SMALL_NUMBER);

	// The near sense rule.
	TestTrue(TEXT("3 m is in near sense"), MDPerception::IsInNearSense(300.0f));
	TestTrue(TEXT("the ring edge is in near sense"), MDPerception::IsInNearSense(MDPerception::NearSenseRange));
	TestFalse(TEXT("6 m is not"), MDPerception::IsInNearSense(600.0f));
	TestFalse(TEXT("a negative distance is not"), MDPerception::IsInNearSense(-1.0f));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// A pack always resumes attacking
// ---------------------------------------------------------------------------------------------------------------------

// Four ghouls on the threat ring (3 - 4 m) around a player, T1 (two tokens). Their sight reports the player lost (the trace
// blocked by pack mates, the cone turned past). Before the fix every brain showed "(lost)", nobody could ask for a token
// (the request needed 255 cm) and the pack stood there. Now: the near sense keeps the player sensed, tokens are taken from
// the ring, no token is held past its timeout, and over 20 s of decisions there is never a stretch without a holder.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIEngagementPackResumesTest, "MazeDemons.AI.Engagement.PackResumesAttacking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIEngagementPackResumesTest::RunTest(const FString& Parameters)
{
	using namespace MDAIEngagementTestsLocal;
	FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), 1);
	FWorldFx F;
	if (!F.Init(*this, 4))
	{
		return false;
	}
	UMDAISubsystem* AI = UMDAISubsystem::Get(F.World);
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(F.World);
	if (!TestNotNull(TEXT("AI subsystem"), AI) || !TestNotNull(TEXT("director"), Director))
	{
		return false;
	}
	F.Place(F.Player, FVector::ZeroVector);
	const FVector Ring[4] = { FVector(380.0, 0.0, 0.0), FVector(-360.0, 0.0, 0.0), FVector(0.0, 400.0, 0.0), FVector(0.0, -300.0, 0.0) };
	for (int32 i = 0; i < 4; ++i)
	{
		F.Place(F.Enemies[i], Ring[i]);
		if (!TestNotNull(TEXT("brain"), F.Brain(i)))
		{
			return false;
		}
	}

	// Seen, then lost: the sight stimuli as AIPerception would deliver them.
	for (int32 i = 0; i < 4; ++i)
	{
		F.Brain(i)->ReportStimulus(F.Player, true, F.Player->GetActorLocation());
		F.Brain(i)->ReportStimulus(F.Player, false, F.Player->GetActorLocation());
	}
	double Now = 1.0;
	for (int32 i = 0; i < 4; ++i)
	{
		F.Brain(i)->RunDecision(Now);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		TestTrue(FString::Printf(TEXT("ghoul %d knows the player"), i), F.Brain(i)->GetTarget() == F.Player);
		TestTrue(FString::Printf(TEXT("ghoul %d senses the player through the near sense, sight lost or not"), i), F.Brain(i)->IsTargetSensed());
	}

	// 20 s of decisions at 8 Hz (the director steps with the world ticks). The pack must never go a full token timeout
	// without a holder, and a holder must always be closing in (Chase / Swarm) or attacking, never holding the ring.
	double LastHolderTime = -1.0;
	double LongestGap = 0.0;
	bool bEverHeld = false;
	double MaxLeft = 0.0;
	for (int32 Step = 0; Step < 160; ++Step)
	{
		Now += 0.125;
		F.TickFrames(2, 0.0625f);
		for (int32 i = 0; i < 4; ++i)
		{
			// The sight keeps saying "lost" (the pack mates stand in the way).
			F.Brain(i)->ReportStimulus(F.Player, false, F.Player->GetActorLocation());
			F.Brain(i)->RunDecision(Now);
		}
		MaxLeft = FMath::Max(MaxLeft, Director->GetTokens().MaxSecondsLeft(Now));
		const int32 Holders = CountHolders(F);
		if (Holders > 0)
		{
			bEverHeld = true;
			LastHolderTime = Now;
		}
		else if (bEverHeld)
		{
			LongestGap = FMath::Max(LongestGap, Now - LastHolderTime);
		}
		TestTrue(TEXT("at most MaxTokens (T1: 2) holders"), Holders <= 2);
		for (int32 i = 0; i < 4; ++i)
		{
			const UMDEnemyBrainComponent* B = F.Brain(i);
			if (B->HoldsAttackToken())
			{
				const EMDIntent Intent = B->GetCurrentIntent();
				TestTrue(FString::Printf(TEXT("a token holder closes in or attacks (ghoul %d: %s)"), i, MDIntentToString(Intent)),
					Intent == EMDIntent::Chase || Intent == EMDIntent::Swarm || Intent == EMDIntent::Attack || Intent == EMDIntent::Feint);
			}
		}
	}
	TestTrue(TEXT("tokens were taken from the threat ring"), bEverHeld);
	TestTrue(TEXT("never a token-timeout stretch without a holder"), LongestGap < MDAI::TokenTimeout);
	TestTrue(TEXT("no token outlives its timeout"), MaxLeft <= MDAI::TokenTimeout + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("somebody holds a token at the end"), CountHolders(F) > 0);

	// A dead player is never sensed: the near sense respects the target rules.
	if (UMDAbilitySystemComponent* ASC = F.PlayerState->GetMDAbilitySystemComponent())
	{
		ASC->ApplyDamage(1000.0f, F.Enemies[0]);
	}
	Now += 0.5;
	for (int32 i = 0; i < 4; ++i)
	{
		F.Brain(i)->RunDecision(Now);
	}
	Now += MDAI::LoseTargetTime + 0.5;
	for (int32 i = 0; i < 4; ++i)
	{
		F.Brain(i)->RunDecision(Now);
		TestNull(FString::Printf(TEXT("ghoul %d forgets a dead player"), i), F.Brain(i)->GetTarget());
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The base ghoul never teleports
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIEngagementGhoulNeverTeleportsTest, "MazeDemons.AI.Engagement.GhoulNeverTeleports", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIEngagementGhoulNeverTeleportsTest::RunTest(const FString& Parameters)
{
	using namespace MDAIEngagementTestsLocal;

	// The bait half-step is a walk: a nav point about 90 cm behind the bait, never further than a stride from it.
	FVector Step;
	const FVector Bait(0.0, 0.0, 0.0);
	const FVector Player(300.0, 0.0, 0.0);
	const FVector Centre(-400.0, 0.0, 0.0);
	if (TestTrue(TEXT("the bait steps back when approached"), MDBehaviour::ComputeBaitStep(Bait, Player, Centre, Step)))
	{
		TestTrue(TEXT("a half-step, not a jump"), FVector::Dist2D(Bait, Step) <= 120.0);
		TestTrue(TEXT("it steps away from the player"), Step.X < Bait.X);
	}

	// T3 unlocks Blink and Charge for archetypes that can; the ghoul cannot. 6 m away (blink range), seen, 30 decisions:
	// never Blink or Charge, and the capsule never moves by more than a stride between two decisions (no teleport, no
	// movement at all: there is no executor in this world, so any displacement would be a snap by the brain).
	FScopedIntCVar ForceTier(TEXT("md.AI.ForceTier"), 3);
	FWorldFx F;
	if (!F.Init(*this, 1))
	{
		return false;
	}
	UMDEnemyBrainComponent* Brain = F.Brain(0);
	if (!TestNotNull(TEXT("brain"), Brain))
	{
		return false;
	}
	TestFalse(TEXT("the ghoul cannot blink"), Brain->GetAIParams().bCanBlink);
	TestFalse(TEXT("the ghoul cannot charge"), Brain->GetAIParams().bCanCharge);
	F.Place(F.Player, FVector::ZeroVector);
	F.Place(F.Enemies[0], FVector(800.0, 0.0, 0.0));
	Brain->ReportStimulus(F.Player, true, F.Player->GetActorLocation());
	FVector Last = F.Enemies[0]->GetActorLocation();
	for (int32 i = 0; i < 30; ++i)
	{
		const double Now = 1.0 + 0.125 * i;
		F.TickFrames(2, 0.0625f);
		Brain->ReportStimulus(F.Player, true, F.Player->GetActorLocation());
		Brain->RunDecision(Now);
		const EMDIntent Intent = Brain->GetCurrentIntent();
		TestTrue(FString::Printf(TEXT("decision %d: no blink, no charge (%s)"), i, MDIntentToString(Intent)), Intent != EMDIntent::Blink && Intent != EMDIntent::Charge);
		const FVector Loc = F.Enemies[0]->GetActorLocation();
		TestTrue(FString::Printf(TEXT("decision %d: the capsule did not snap (%.0f cm)"), i, FVector::Dist(Last, Loc)), FVector::Dist(Last, Loc) <= 100.0);
		Last = Loc;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Voice gate
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIVoiceGateTest, "MazeDemons.AI.Voice.GateRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIVoiceGateTest::RunTest(const FString& Parameters)
{
	const FMDVoiceRules Tell = FMDVoiceRules::ForKind(EMDVoiceKind::Tell);
	TestTrue(TEXT("a tell is 0.5 - 0.8 s"), Tell.MaxSeconds >= 0.5f && Tell.MaxSeconds <= 0.8f);
	TestTrue(TEXT("at most 2 tells at once"), Tell.MaxConcurrent >= 1 && Tell.MaxConcurrent <= 2);
	TestTrue(TEXT("tells are quieter than the cue"), Tell.VolumeScale < 1.0f);
	const FMDVoiceRules Sting = FMDVoiceRules::ForKind(EMDVoiceKind::Sting);
	TestTrue(TEXT("max 1 sting per 2 s"), Sting.ListenerInterval >= 2.0 && Sting.MaxConcurrent == 1);
	const FMDVoiceRules Bark = FMDVoiceRules::ForKind(EMDVoiceKind::Bark);
	TestTrue(TEXT("one bark at a time"), Bark.MaxConcurrent == 1);

	FMDVoiceGate Gate;
	FString Reason;
	// Six ghouls tell at once: the listener hears one, then one more after the listener interval, never a third while
	// two play.
	TestTrue(TEXT("first tell plays"), Gate.TryStart(EMDVoiceKind::Tell, 1, 0.0, &Reason));
	TestFalse(TEXT("a second tell in the same instant is throttled"), Gate.TryStart(EMDVoiceKind::Tell, 2, 0.0, &Reason));
	TestEqual(TEXT("... by the listener interval"), Reason, FString(TEXT("listener interval")));
	TestTrue(TEXT("after the interval a second enemy's tell plays"), Gate.TryStart(EMDVoiceKind::Tell, 2, Tell.ListenerInterval, &Reason));
	TestEqual(TEXT("two playing"), Gate.NumPlaying(EMDVoiceKind::Tell, Tell.ListenerInterval), 2);
	TestFalse(TEXT("a third is refused while two play"), Gate.TryStart(EMDVoiceKind::Tell, 3, Tell.ListenerInterval * 2.0, &Reason));
	TestEqual(TEXT("... by concurrency"), Reason, FString(TEXT("concurrency")));
	// The lines are cut at MaxSeconds: afterwards a new one plays.
	const double After = static_cast<double>(Tell.MaxSeconds) + Tell.ListenerInterval + 0.01;
	TestEqual(TEXT("nothing playing after the cut"), Gate.NumPlaying(EMDVoiceKind::Tell, After), 0);
	TestTrue(TEXT("a third enemy's tell plays after the cut"), Gate.TryStart(EMDVoiceKind::Tell, 3, After, &Reason));
	// Per-enemy cooldown: enemy 3 cannot tell again at once, even with room.
	TestFalse(TEXT("the same enemy cannot repeat its tell at once"), Gate.TryStart(EMDVoiceKind::Tell, 3, After + Tell.ListenerInterval + 0.01, &Reason));
	TestEqual(TEXT("... enemy cooldown"), Reason, FString(TEXT("enemy cooldown")));
	TestTrue(TEXT("it can after its cooldown"), Gate.TryStart(EMDVoiceKind::Tell, 3, After + Tell.EnemyCooldown + 0.01, &Reason));

	// Stings: one per 2 s whoever swings.
	Gate.Reset();
	TestTrue(TEXT("sting"), Gate.TryStart(EMDVoiceKind::Sting, 7, 10.0));
	TestFalse(TEXT("another enemy's sting 1 s later is throttled"), Gate.TryStart(EMDVoiceKind::Sting, 8, 11.0));
	TestTrue(TEXT("2 s later it plays"), Gate.TryStart(EMDVoiceKind::Sting, 8, 12.0));

	// Kinds do not block each other: a bark during a tell.
	TestTrue(TEXT("tell"), Gate.TryStart(EMDVoiceKind::Tell, 1, 20.0));
	TestTrue(TEXT("a bark in the same instant"), Gate.TryStart(EMDVoiceKind::Bark, 2, 20.0));
	TestFalse(TEXT("a second bark is throttled"), Gate.TryStart(EMDVoiceKind::Bark, 3, 20.5));

	// Deterministic: the same sequence gives the same answers.
	FMDVoiceGate A;
	FMDVoiceGate B;
	bool bSame = true;
	for (int32 i = 0; i < 40; ++i)
	{
		const double T = 0.1 * i;
		bSame &= A.TryStart(EMDVoiceKind::Tell, i % 5, T) == B.TryStart(EMDVoiceKind::Tell, i % 5, T);
	}
	TestTrue(TEXT("deterministic"), bSame);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
