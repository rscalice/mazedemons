#include "CoreMinimal.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDAITypes.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDPack.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDThreatIndicator.h"
#include "AI/MDTierRules.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "MDMapZone.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"

// V1 WP4 intelligence tier tests (MazeDemons.AI.Tier.*): the formula and its clamps are plain functions (MDTierRules.h) and run
// with no world; the late-joiner test uses a transient world with a real pack director.

namespace MDAITierTestsLocal
{
	// md.AI.ForceTier for the length of a test.
	struct FScopedTierCVar
	{
		IConsoleVariable* Var = nullptr;
		int32 Old = -1;

		explicit FScopedTierCVar(int32 Tier)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(TEXT("md.AI.ForceTier"));
			if (Var)
			{
				Old = Var->GetInt();
				Var->Set(Tier, ECVF_SetByCode);
			}
		}
		~FScopedTierCVar()
		{
			if (Var)
			{
				Var->Set(Old, ECVF_SetByCode);
			}
		}
	};

	struct FTierWorld
	{
		UWorld* World = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAITierTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FTierWorld()
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
}

// ITValue = 0.55 Depth + 0.30 Party + 0.15 Size, thresholds 0.15 / 0.35 / 0.60 / 0.85, zone depth 0.1 / 0.4 / 0.7 / 1.0, party
// 0.7 perks/8 + 0.3 upgrade/3, size (players - 1) / 3.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierFormulaBoundariesTest, "MazeDemons.AI.Tier.FormulaBoundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierFormulaBoundariesTest::RunTest(const FString& Parameters)
{
	// Depth: the fixed layout.
	TestEqual(TEXT("zone 1"), MDTier::DepthScore(1), 0.1f);
	TestEqual(TEXT("zone 2"), MDTier::DepthScore(2), 0.4f);
	TestEqual(TEXT("zone 3"), MDTier::DepthScore(3), 0.7f);
	TestEqual(TEXT("zone 4"), MDTier::DepthScore(4), 1.0f);
	TestEqual(TEXT("no zone"), MDTier::DepthScore(0), 0.0f);
	TestEqual(TEXT("bad zone"), MDTier::DepthScore(9), 0.0f);

	// Size: solo 0, four players 1, never outside 0..1.
	TestEqual(TEXT("no players"), MDTier::SizeScore(0), 0.0f);
	TestEqual(TEXT("solo"), MDTier::SizeScore(1), 0.0f);
	TestEqual(TEXT("two"), MDTier::SizeScore(2), 1.0f / 3.0f);
	TestEqual(TEXT("four"), MDTier::SizeScore(4), 1.0f);
	TestEqual(TEXT("six clamps"), MDTier::SizeScore(6), 1.0f);

	// Party: 0.7 perks (8 = full) + 0.3 gear (3 = full), each clamped.
	TestEqual(TEXT("nothing"), MDTier::PartyScore(0.0f, 0.0f), 0.0f);
	TestEqual(TEXT("perks maxed"), MDTier::PartyScore(8.0f, 0.0f), 0.7f);
	TestEqual(TEXT("gear maxed"), MDTier::PartyScore(0.0f, 3.0f), 0.3f);
	TestEqual(TEXT("both maxed"), MDTier::PartyScore(8.0f, 3.0f), 1.0f);
	TestEqual(TEXT("clamped above"), MDTier::PartyScore(80.0f, 30.0f), 1.0f);
	TestEqual(TEXT("half perks"), MDTier::PartyScore(4.0f, 0.0f), 0.35f);

	// The weights: everything maxed is exactly 1, zones alone give 0.55 * depth.
	TestEqual(TEXT("all max"), MDTier::ITValue(1.0f, 1.0f, 1.0f, 0.0f), 1.0f);
	TestEqual(TEXT("depth only"), MDTier::ITValue(0.4f, 0.0f, 0.0f, 0.0f), 0.55f * 0.4f);
	TestEqual(TEXT("party only"), MDTier::ITValue(0.0f, 1.0f, 0.0f, 0.0f), 0.30f);
	TestEqual(TEXT("size only"), MDTier::ITValue(0.0f, 0.0f, 1.0f, 0.0f), 0.15f);
	TestEqual(TEXT("modifier is added"), MDTier::ITValue(0.0f, 0.0f, 0.0f, 0.2f), 0.2f);

	// Thresholds: the boundary value belongs to the higher tier.
	TestEqual(TEXT("0 -> T0"), MDTier::TierFromValue(0.0f), EMDIntelligenceTier::T0);
	TestEqual(TEXT("just under 0.15 -> T0"), MDTier::TierFromValue(0.1499f), EMDIntelligenceTier::T0);
	TestEqual(TEXT("0.15 -> T1"), MDTier::TierFromValue(0.15f), EMDIntelligenceTier::T1);
	TestEqual(TEXT("just under 0.35 -> T1"), MDTier::TierFromValue(0.3499f), EMDIntelligenceTier::T1);
	TestEqual(TEXT("0.35 -> T2"), MDTier::TierFromValue(0.35f), EMDIntelligenceTier::T2);
	TestEqual(TEXT("just under 0.60 -> T2"), MDTier::TierFromValue(0.5999f), EMDIntelligenceTier::T2);
	TestEqual(TEXT("0.60 -> T3"), MDTier::TierFromValue(0.60f), EMDIntelligenceTier::T3);
	TestEqual(TEXT("just under 0.85 -> T3"), MDTier::TierFromValue(0.8499f), EMDIntelligenceTier::T3);
	TestEqual(TEXT("0.85 -> T4"), MDTier::TierFromValue(0.85f), EMDIntelligenceTier::T4);
	TestEqual(TEXT("1.0 -> T4"), MDTier::TierFromValue(1.0f), EMDIntelligenceTier::T4);
	TestEqual(TEXT("below zero -> T0"), MDTier::TierFromValue(-0.3f), EMDIntelligenceTier::T0);

	// The whole pipeline: the extremes land on T0 and T4 whatever the jitter does.
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		MDTier::FInputs Low;
		Low.RunSeed = Seed;
		Low.PackId = Seed;
		TestEqual(TEXT("solo outside any zone, no progress -> T0"), MDTier::Compute(Low).Tier, EMDIntelligenceTier::T0);

		MDTier::FInputs High;
		High.RunSeed = Seed;
		High.PackId = Seed;
		High.ZoneIndex = 4;
		High.NumPlayers = 4;
		High.AvgPerksSpent = 8.0f;
		High.AvgUpgradeLevel = 3.0f;
		TestEqual(TEXT("four maxed players in zone 4 -> T4"), MDTier::Compute(High).Tier, EMDIntelligenceTier::T4);
	}

	// Zone only (solo, no progress): zone 1 = 0.055 -> T0; zone 2 = 0.22 -> T1; zone 3 = 0.385 -> T2; zone 4 = 0.55 -> T2.
	// The jitter is at most 0.05, so zone 2 and 3 each stay within one tier of this.
	const EMDIntelligenceTier ZoneTier[4] = { EMDIntelligenceTier::T0, EMDIntelligenceTier::T1, EMDIntelligenceTier::T2, EMDIntelligenceTier::T2 };
	for (int32 Zone = 1; Zone <= 4; ++Zone)
	{
		MDTier::FInputs In;
		In.ZoneIndex = Zone;
		In.RunSeed = 5;
		In.PackId = Zone;
		const MDTier::FResult R = MDTier::Compute(In);
		TestEqual(TEXT("zone depth in the result"), R.Depth, MDTier::DepthScore(Zone));
		const int32 Diff = FMath::Abs(static_cast<int32>(R.Tier) - static_cast<int32>(ZoneTier[Zone - 1]));
		TestTrue(*FString::Printf(TEXT("zone %d solo is within a tier of the table value (got T%d)"), Zone, static_cast<int32>(R.Tier)), Diff <= 1);
	}
	return true;
}

// The archetype's MinTier / MaxTier clamp the computed tier (a Ghoul never exceeds T3, the Revenant never drops below T3).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierArchetypeClampTest, "MazeDemons.AI.Tier.ArchetypeClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierArchetypeClampTest::RunTest(const FString& Parameters)
{
	const EMDIntelligenceTier T[5] = { EMDIntelligenceTier::T0, EMDIntelligenceTier::T1, EMDIntelligenceTier::T2, EMDIntelligenceTier::T3, EMDIntelligenceTier::T4 };

	TestEqual(TEXT("T4 clamped to a T3 maximum"), MDTier::ClampTier(T[4], T[0], T[3]), T[3]);
	TestEqual(TEXT("T0 clamped to a T3 minimum"), MDTier::ClampTier(T[0], T[3], T[4]), T[3]);
	TestEqual(TEXT("inside the range is unchanged"), MDTier::ClampTier(T[2], T[1], T[3]), T[2]);
	TestEqual(TEXT("a swapped range is the range between"), MDTier::ClampTier(T[4], T[3], T[1]), T[3]);

	// Through Compute: the strongest party in zone 4 is T4 unclamped, T3 for a Ghoul-like archetype.
	MDTier::FInputs Strong;
	Strong.RunSeed = 11;
	Strong.PackId = 2;
	Strong.ZoneIndex = 4;
	Strong.NumPlayers = 4;
	Strong.AvgPerksSpent = 8.0f;
	Strong.AvgUpgradeLevel = 3.0f;
	TestEqual(TEXT("unclamped"), MDTier::Compute(Strong).Tier, T[4]);
	Strong.MaxTier = T[3];
	TestEqual(TEXT("ghoul-like max T3"), MDTier::Compute(Strong).Tier, T[3]);

	// And the weakest never drops below a Revenant-like minimum of T3.
	MDTier::FInputs Weak;
	Weak.RunSeed = 11;
	Weak.PackId = 2;
	Weak.MinTier = T[3];
	Weak.MaxTier = T[4];
	TestEqual(TEXT("revenant-like min T3"), MDTier::Compute(Weak).Tier, T[3]);

	// The archetype asset's own range agrees with the clamp.
	UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>();
	Archetype->MinTier = T[1];
	Archetype->MaxTier = T[3];
	for (int32 Raw = 0; Raw < 5; ++Raw)
	{
		const EMDIntelligenceTier Clamped = MDTier::ClampTier(T[Raw], Archetype->MinTier, Archetype->MaxTier);
		TestTrue(*FString::Printf(TEXT("T%d clamps into the archetype's range"), Raw), Archetype->AllowsTier(Clamped));
	}
	return true;
}

// The jitter is a pure function of (run seed, pack id), stays within +/- 0.05 and differs between packs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierSeedJitterDeterministicTest, "MazeDemons.AI.Tier.SeedJitterDeterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierSeedJitterDeterministicTest::RunTest(const FString& Parameters)
{
	TSet<int32> Distinct;
	bool bBothSigns[2] = { false, false };
	for (int32 Pack = 1; Pack <= 64; ++Pack)
	{
		const float A = MDTier::Jitter(1234, Pack);
		const float B = MDTier::Jitter(1234, Pack);
		TestEqual(TEXT("same seed and pack give the same jitter"), A, B);
		TestTrue(TEXT("within the amplitude"), FMath::Abs(A) <= MDTier::JitterAmplitude + KINDA_SMALL_NUMBER);
		Distinct.Add(FMath::RoundToInt(A * 100000.0f));
		bBothSigns[A < 0.0f ? 0 : 1] = true;
	}
	TestTrue(TEXT("packs get different jitter"), Distinct.Num() > 40);
	TestTrue(TEXT("both signs occur"), bBothSigns[0] && bBothSigns[1]);
	TestNotEqual(TEXT("another run seed shifts a pack"), MDTier::Jitter(1234, 3), MDTier::Jitter(4321, 3));

	// Compute is deterministic too, and its ITValue is the raw value plus exactly that jitter.
	MDTier::FInputs In;
	In.RunSeed = 77;
	In.PackId = 5;
	In.ZoneIndex = 3;
	In.NumPlayers = 2;
	In.AvgPerksSpent = 2.0f;
	const MDTier::FResult R1 = MDTier::Compute(In);
	const MDTier::FResult R2 = MDTier::Compute(In);
	TestEqual(TEXT("same inputs, same ITValue"), R1.Value, R2.Value);
	TestEqual(TEXT("same inputs, same tier"), R1.Tier, R2.Tier);
	TestEqual(TEXT("ITValue = formula + jitter"), R1.Value, MDTier::ITValue(R1.Depth, R1.Party, R1.Size, 0.0f) + MDTier::Jitter(77, 5));

	// Two packs in one room (same inputs, different ids) can land on different values, and over many packs on different tiers.
	TSet<int32> Tiers;
	In.ZoneIndex = 2;
	In.NumPlayers = 1;
	In.AvgPerksSpent = 4.0f; // 0.22 + 0.105 = 0.325: 0.025 under the T2 line, inside the jitter
	for (int32 Pack = 1; Pack <= 64; ++Pack)
	{
		In.PackId = Pack;
		Tiers.Add(static_cast<int32>(MDTier::Compute(In).Tier));
	}
	TestTrue(TEXT("a value near a threshold gives two packs different tiers"), Tiers.Num() >= 2);
	return true;
}

// TellTime = authored * tier multiplier (1.0, 0.9, 0.8, 0.7, 0.6), never below 0.35 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierTellFloorTest, "MazeDemons.AI.Tier.TellFloor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierTellFloorTest::RunTest(const FString& Parameters)
{
	const float Multipliers[5] = { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f };
	float Previous = 100.0f;
	for (int32 Tier = 0; Tier < 5; ++Tier)
	{
		const FMDTierParams P = FMDTierParams::DefaultForTier(static_cast<EMDIntelligenceTier>(Tier));
		TestEqual(*FString::Printf(TEXT("T%d tell multiplier"), Tier), P.TellTimeMultiplier, Multipliers[Tier]);
		const float Tell = P.ResolveTellTime(0.8f);
		TestEqual(*FString::Printf(TEXT("T%d tell of an 0.8 s attack"), Tier), Tell, 0.8f * Multipliers[Tier]);
		TestTrue(TEXT("tells shrink with the tier"), Tell <= Previous);
		Previous = Tell;
		for (const float Authored : { 0.0f, 0.1f, 0.3f, 0.5f, 0.8f, 1.5f })
		{
			TestTrue(*FString::Printf(TEXT("T%d tell of %.2f s is at least 0.35 s"), Tier, Authored), P.ResolveTellTime(Authored) >= 0.35f - KINDA_SMALL_NUMBER);
		}
	}
	const FMDTierParams T4 = FMDTierParams::DefaultForTier(EMDIntelligenceTier::T4);
	TestEqual(TEXT("an authored tell that would end under the floor is lifted to it"), T4.ResolveTellTime(0.5f), 0.35f);
	TestEqual(TEXT("the floor constant"), FMDTierParams::TellTimeFloor, 0.35f);

	// A designer cannot tune the floor away through the table either: a 0.1 multiplier still resolves to 0.35 s.
	FMDTierParams Crazy = T4;
	Crazy.TellTimeMultiplier = 0.1f;
	TestEqual(TEXT("table multiplier of 0.1"), Crazy.ResolveTellTime(0.8f), 0.35f);

	// DT_IntelligenceTiers rows override the code defaults for ForTier, and clearing the table restores the defaults.
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FMDIntelligenceTier::StaticStruct();
	FMDIntelligenceTier Row;
	Row.ReactionTime = 0.123f;
	Row.TellTimeMultiplier = 0.5f;
	Table->AddRow(FName(TEXT("T0")), Row);
	FMDTierParams::SetTierTable(Table);
	TestEqual(TEXT("row T0 overrides the default"), FMDTierParams::ForTier(EMDIntelligenceTier::T0).ReactionTime, 0.123f);
	TestEqual(TEXT("a tier without a row keeps the default"), FMDTierParams::ForTier(EMDIntelligenceTier::T3).ReactionTime, 0.4f);
	TestEqual(TEXT("the default is still reachable"), FMDTierParams::DefaultForTier(EMDIntelligenceTier::T0).ReactionTime, 0.9f);
	FMDTierParams::SetTierTable(nullptr);
	TestEqual(TEXT("no table, defaults again"), FMDTierParams::ForTier(EMDIntelligenceTier::T0).ReactionTime, 0.9f);
	return true;
}

// The tier is computed when the pack spawns and never changes: a player who joins later only raises the size score of packs
// that spawn after them. md.AI.ForceTier overrides the read without touching the stored tier.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierLateJoinerTest, "MazeDemons.AI.Tier.LateJoinerAffectsLaterPacksOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierLateJoinerTest::RunTest(const FString& Parameters)
{
	MDAITierTestsLocal::FTierWorld Holder;
	if (!Holder.Init(*this))
	{
		return false;
	}
	UWorld* World = Holder.World;
	UMDAISubsystem* AI = UMDAISubsystem::Get(World);
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(World);
	if (!TestNotNull(TEXT("AI subsystem"), AI) || !TestNotNull(TEXT("pack director"), Director))
	{
		return false;
	}
	AI->SetRunSeed(4242);

	// A level with one zone 3 around the origin.
	AMDMapZone* Zone = World->SpawnActor<AMDMapZone>();
	if (TestNotNull(TEXT("zone"), Zone))
	{
		Zone->ZoneIndex = 3;
		Zone->ZoneBox->SetBoxExtent(FVector(20000.0, 20000.0, 2000.0));
	}

	// One player is in the game when the first pack spawns.
	APawn* First = World->SpawnActor<APawn>();
	AI->TrackExtraPlayer(First);
	AMDEnemyCharacter* A = World->SpawnActor<AMDEnemyCharacter>();
	if (!A)
	{
		AddError(TEXT("Could not spawn enemy A"));
		return false;
	}
	const int32 PackA = Director->RegisterEnemy(A);
	const FMDPack* PackAData = Director->FindPack(PackA);
	if (!TestNotNull(TEXT("pack A"), PackAData))
	{
		return false;
	}
	TestTrue(TEXT("pack A has a tier"), PackAData->bTierSet);
	TestEqual(TEXT("pack A saw one player"), PackAData->PlayersAtSpawn, 1);
	TestEqual(TEXT("the enemy stands in zone 3"), Director->GatherTierInputs(A, PackA).ZoneIndex, 3);
	const float ValueA = PackAData->ITValue;
	const EMDIntelligenceTier TierA = PackAData->Tier;
	const float Expected = MDTier::ITValue(MDTier::DepthScore(3), 0.0f, MDTier::SizeScore(1), 0.0f) + MDTier::Jitter(4242, PackA);
	TestEqual(TEXT("pack A's ITValue is the formula plus its jitter"), ValueA, Expected);

	// Three more players join.
	for (int32 i = 0; i < 3; ++i)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APawn* Joiner = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(500.0 * (i + 1), 0.0, 0.0)), Params);
		if (!TestNotNull(TEXT("late joiner pawn"), Joiner))
		{
			return false;
		}
		AI->TrackExtraPlayer(Joiner);
	}
	// An enemy joins a pack as it spawns (its controller possesses it), so B spawns only now, with four players in the game.
	AMDEnemyCharacter* B = World->SpawnActor<AMDEnemyCharacter>();
	if (!B)
	{
		AddError(TEXT("Could not spawn enemy B"));
		return false;
	}
	const int32 PackB = Director->RegisterEnemy(B);
	const FMDPack* PackBData = Director->FindPack(PackB);
	if (!TestNotNull(TEXT("pack B"), PackBData))
	{
		return false;
	}
	TestEqual(TEXT("pack B saw four players"), PackBData->PlayersAtSpawn, 4);
	const float ExpectedB = MDTier::ITValue(MDTier::DepthScore(3), 0.0f, MDTier::SizeScore(4), 0.0f) + MDTier::Jitter(4242, PackB);
	TestEqual(TEXT("pack B's ITValue counts all four"), PackBData->ITValue, ExpectedB);

	// Pack A did not move, however long the players stay and however often the director steps.
	PackAData = Director->FindPack(PackA);
	TestEqual(TEXT("pack A's ITValue is unchanged by the late joiners"), PackAData->ITValue, ValueA);
	TestEqual(TEXT("pack A's tier is unchanged"), PackAData->Tier, TierA);
	Director->StepPacks(1.0);
	Director->StepPacks(10.0);
	PackAData = Director->FindPack(PackA);
	TestEqual(TEXT("still unchanged after stepping"), PackAData->ITValue, ValueA);
	TestEqual(TEXT("its players at spawn stay 1"), PackAData->PlayersAtSpawn, 1);

	// A third enemy joining pack A later does not recompute it either.
	AMDEnemyCharacter* C = World->SpawnActor<AMDEnemyCharacter>();
	TArray<AActor*> Joining = { C };
	Director->RegisterSpawnGroup(Joining, PackA, NAME_None); // moves C into pack A (it may already have a pack of its own)
	TestEqual(TEXT("a new member keeps the pack's value"), Director->FindPack(PackA)->ITValue, ValueA);

	// md.AI.ForceTier overrides what the pack reports, not what it stored.
	{
		MDAITierTestsLocal::FScopedTierCVar Force(4);
		TestEqual(TEXT("forced tier"), Director->GetTierOf(A), EMDIntelligenceTier::T4);
		TestEqual(TEXT("the stored tier is untouched"), Director->FindPack(PackA)->Tier, TierA);
	}
	TestEqual(TEXT("no force, the pack's own tier"), Director->GetTierOf(A), TierA);
	TestEqual(TEXT("pack mates share the tier"), Director->GetTierOf(C), TierA);
	return true;
}

// The off-screen arrow sits on the screen edge in the enemy's direction: ahead = top, right = right, behind = bottom.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAITierThreatArrowTest, "MazeDemons.AI.Tier.ThreatArrowPlacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAITierThreatArrowTest::RunTest(const FString& Parameters)
{
	const FVector2D View(1920.0, 1080.0);
	const float Margin = 50.0f;
	FVector2D Pos;
	float Rotation = 0.0f;

	MDThreat::ArrowForYaw(180.0f, View, Margin, Pos, Rotation);
	TestEqual(TEXT("behind: bottom edge"), static_cast<float>(Pos.Y), static_cast<float>(View.Y - Margin), 0.5f);
	TestEqual(TEXT("behind: centred"), static_cast<float>(Pos.X), 960.0f, 0.5f);
	TestEqual(TEXT("behind: points down"), Rotation, static_cast<float>(UE_HALF_PI), 0.01f);

	MDThreat::ArrowForYaw(90.0f, View, Margin, Pos, Rotation);
	TestEqual(TEXT("right: right edge"), static_cast<float>(Pos.X), static_cast<float>(View.X - Margin), 0.5f);
	TestEqual(TEXT("right: vertically centred"), static_cast<float>(Pos.Y), 540.0f, 0.5f);
	TestEqual(TEXT("right: points right"), Rotation, 0.0f, 0.01f);

	MDThreat::ArrowForYaw(-90.0f, View, Margin, Pos, Rotation);
	TestEqual(TEXT("left: left edge"), static_cast<float>(Pos.X), Margin, 0.5f);

	MDThreat::ArrowForYaw(0.0f, View, Margin, Pos, Rotation);
	TestEqual(TEXT("ahead: top edge"), static_cast<float>(Pos.Y), Margin, 0.5f);

	for (float Yaw = -180.0f; Yaw <= 180.0f; Yaw += 15.0f)
	{
		MDThreat::ArrowForYaw(Yaw, View, Margin, Pos, Rotation);
		TestTrue(TEXT("always inside the margin box"), Pos.X >= Margin - 0.5 && Pos.X <= View.X - Margin + 0.5 && Pos.Y >= Margin - 0.5 && Pos.Y <= View.Y - Margin + 0.5);
	}
	return true;
}
