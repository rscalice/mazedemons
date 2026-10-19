#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDPerkCatalog.h"
#include "MDPerkComponent.h"
#include "MDPerkTypes.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"
#include "MDStatCalculator.h"
#include "MDUITypes.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// Friend of UMDPerkComponent (see header): reaches the server state without a game session or a gameplay effect.
struct FMDPerkTestAccess
{
	static void GrantSecondWind(UMDPerkComponent* C)
	{
		C->AppliedMods.bSecondWind = true;
		C->PushCombatState();
	}
	static void TickSecondWind(UMDPerkComponent* C) { C->TickSecondWind(); }
};

namespace
{
	const UMDPerkTree& PerkTestTree()
	{
		return *GetDefault<UMDPerkTree>();
	}

	TArray<uint8> PerkTestZeros()
	{
		TArray<uint8> Ranks;
		Ranks.Init(0, MD_NumPerkNodes);
		return Ranks;
	}

	void PerkTestSet(TArray<uint8>& Ranks, const TCHAR* Id, int32 Rank)
	{
		const int32 Index = PerkTestTree().FindNodeIndex(FName(Id));
		check(Index != INDEX_NONE);
		Ranks[Index] = static_cast<uint8>(Rank);
	}

	FMDPerkModifiers PerkTestMods(const TArray<uint8>& Ranks)
	{
		FMDPerkModifiers Mods;
		UMDPerkCatalog::ComputeModifiers(Ranks, PerkTestTree(), Mods);
		return Mods;
	}

	FMDPerkModifiers PerkTestOneNode(const TCHAR* Id, int32 Rank)
	{
		TArray<uint8> Ranks = PerkTestZeros();
		PerkTestSet(Ranks, Id, Rank);
		return PerkTestMods(Ranks);
	}

	// Transient world with an authoritative AMDPlayerState (ASC, stamina and perk component), like the stamina fixture.
	struct FMDPerkFixture
	{
		UWorld* World = nullptr;
		AMDPlayerState* PS = nullptr;
		UMDAbilitySystemComponent* ASC = nullptr;
		UMDStaminaComponent* Stamina = nullptr;
		UMDPerkComponent* Perk = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDPerkTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			PS = World->SpawnActor<AMDPlayerState>();
			APawn* Pawn = World->SpawnActor<APawn>();
			if (!PS || !Pawn)
			{
				Test.AddError(TEXT("Could not spawn AMDPlayerState / APawn"));
				return false;
			}
			PS->SetRole(ROLE_Authority);
			Pawn->SetPlayerState(PS);
			ASC = PS->GetMDAbilitySystemComponent();
			Stamina = PS->StaminaComponent;
			Perk = PS->PerkComponent;
			if (!ASC || !Stamina || !Perk)
			{
				Test.AddError(TEXT("PlayerState is missing its ASC, stamina or perk component"));
				return false;
			}
			ASC->InitAbilityActorInfo(PS, Pawn);
			ASC->RegisterAttributeSet(PS->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			ASC->InitializeStamina(Stamina->StartingMaxStamina, Stamina->RegenPerSec);
			if (!ASC->GetMazeDemonsAttributes())
			{
				Test.AddWarning(TEXT("ASC has no attribute set in the test world; skipping"));
				return false;
			}
			return true;
		}

		~FMDPerkFixture()
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

// ---- Catalog ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkCatalogShapeTest, "MazeDemons.Perk.CatalogShape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkCatalogShapeTest::RunTest(const FString& Parameters)
{
	const UMDPerkTree& Tree = PerkTestTree();
	if (!TestEqual(TEXT("16 nodes"), Tree.Nodes.Num(), MD_NumPerkNodes))
	{
		return false;
	}

	TSet<FName> Ids;
	for (const FMDPerkNode& Node : Tree.Nodes)
	{
		TestFalse(*FString::Printf(TEXT("id %s is unique"), *Node.Id.ToString()), Ids.Contains(Node.Id));
		Ids.Add(Node.Id);
	}

	const int32 ExpectedMaxRank[4] = { 2, 2, 2, 1 };
	const int32 ExpectedRequires[4] = { 0, 0, 2, 4 };
	for (int32 PathIndex = 1; PathIndex <= 4; ++PathIndex)
	{
		for (int32 TierIndex = 1; TierIndex <= 4; ++TierIndex)
		{
			const int32 Index = UMDPerkTree::GetNodeIndex(static_cast<EMDPerkPath>(PathIndex), static_cast<EMDPerkTier>(TierIndex));
			TestEqual(TEXT("node index = (path - 1) * 4 + (tier - 1)"), Index, (PathIndex - 1) * 4 + (TierIndex - 1));
			const FMDPerkNode& Node = Tree.Nodes[Index];
			TestEqual(*FString::Printf(TEXT("%s path"), *Node.Id.ToString()), static_cast<int32>(Node.Path), PathIndex);
			TestEqual(*FString::Printf(TEXT("%s tier"), *Node.Id.ToString()), static_cast<int32>(Node.Tier), TierIndex);
			TestEqual(*FString::Printf(TEXT("%s max rank"), *Node.Id.ToString()), Node.MaxRank, ExpectedMaxRank[TierIndex - 1]);
			TestEqual(*FString::Printf(TEXT("%s requires"), *Node.Id.ToString()), Node.RequiresPathPoints, ExpectedRequires[TierIndex - 1]);
		}
	}
	TestEqual(TEXT("None has no node"), UMDPerkTree::GetNodeIndex(EMDPerkPath::None, EMDPerkTier::Tier1a), static_cast<int32>(INDEX_NONE));

	// BALANCE section 5 per-rank values.
	struct FExpect { const TCHAR* Id; float PerRank; };
	const FExpect Expected[] = {
		{ TEXT("Hardened"), 3.0f }, { TEXT("ThickHide"), 2.0f }, { TEXT("Stalwart"), 5.0f }, { TEXT("Bulwark"), 0.30f },
		{ TEXT("Sharpened"), 0.06f }, { TEXT("Precision"), 0.05f }, { TEXT("Ferocity"), 0.15f }, { TEXT("Executioner"), 0.20f },
		{ TEXT("Robust"), 10.0f }, { TEXT("Recovery"), 0.5f }, { TEXT("PotentDraughts"), 0.20f }, { TEXT("SecondWind"), 0.25f },
		{ TEXT("Endurance"), 10.0f }, { TEXT("QuickBreath"), 0.15f }, { TEXT("LightStep"), 0.10f }, { TEXT("Fleet"), 0.05f },
	};
	for (const FExpect& E : Expected)
	{
		const int32 Index = Tree.FindNodeIndex(FName(E.Id));
		if (TestTrue(*FString::Printf(TEXT("node %s exists"), E.Id), Index != INDEX_NONE))
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s per rank"), E.Id), Tree.Nodes[Index].ValuePerRank, E.PerRank, 0.0001f);
		}
	}

	// BALANCE section 7 caps.
	TestEqual(TEXT("max health from perks"), Tree.Caps.MaxHealthFromPerks, 30.0f);
	TestEqual(TEXT("health regen cap"), Tree.Caps.HealthRegenCap, 1.5f);
	TestEqual(TEXT("crit chance cap"), Tree.Caps.CritChanceCap, 0.25f);
	TestEqual(TEXT("crit damage cap"), Tree.Caps.CritDamageCap, 2.0f);
	TestEqual(TEXT("stamina cost floor"), Tree.Caps.StaminaCostScaleMin, 0.7f);
	TestEqual(TEXT("stamina regen cap"), Tree.Caps.StaminaRegenBonusCap, 0.45f);
	TestEqual(TEXT("max stamina cap"), Tree.Caps.MaxStaminaCap, 150.0f);
	TestEqual(TEXT("sprint speed cap"), Tree.Caps.SprintSpeedBonusCap, 0.05f);
	TestEqual(TEXT("path cap"), Tree.Caps.PathPointCap, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkCommitValidatesTest, "MazeDemons.Perk.CommitValidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkCommitValidatesTest::RunTest(const FString& Parameters)
{
	const UMDPerkTree& Tree = PerkTestTree();
	const TArray<uint8> None = PerkTestZeros();

	auto Check = [&](const TCHAR* What, const TArray<uint8>& Current, const TArray<uint8>& Desired, int32 Earned, double TimeSince, bool bDead, EMDPerkReject Expected)
	{
		TestEqual(What, UMDPerkCatalog::ValidateCommit(Current, Desired, Earned, TimeSince, bDead, Tree), Expected);
	};

	// Points.
	TArray<uint8> Hardened1 = PerkTestZeros();
	PerkTestSet(Hardened1, TEXT("Hardened"), 1);
	Check(TEXT("no change is fine"), None, None, 0, 10.0, false, EMDPerkReject::None);
	Check(TEXT("no points"), None, Hardened1, 0, 10.0, false, EMDPerkReject::NoPoints);
	Check(TEXT("one point"), None, Hardened1, 1, 10.0, false, EMDPerkReject::None);

	// Tier 2 needs 2 points from OTHER nodes of the path.
	TArray<uint8> StalwartOnly = PerkTestZeros();
	PerkTestSet(StalwartOnly, TEXT("Stalwart"), 1);
	Check(TEXT("tier 2 with no other points is locked"), None, StalwartOnly, 5, 10.0, false, EMDPerkReject::PathLocked);
	TArray<uint8> StalwartOneOther = StalwartOnly;
	PerkTestSet(StalwartOneOther, TEXT("Hardened"), 1);
	Check(TEXT("tier 2 with 1 other point is locked"), None, StalwartOneOther, 5, 10.0, false, EMDPerkReject::PathLocked);
	TArray<uint8> StalwartTwoOthers = StalwartOneOther;
	PerkTestSet(StalwartTwoOthers, TEXT("ThickHide"), 1);
	Check(TEXT("tier 2 with 2 other points is open"), None, StalwartTwoOthers, 5, 10.0, false, EMDPerkReject::None);
	TArray<uint8> StalwartRank2 = PerkTestZeros();
	PerkTestSet(StalwartRank2, TEXT("Stalwart"), 2);
	PerkTestSet(StalwartRank2, TEXT("Hardened"), 2);
	Check(TEXT("its own ranks do not count towards its prerequisite"), None, StalwartRank2, 5, 10.0, false, EMDPerkReject::None);
	TArray<uint8> StalwartOnlyRank2 = PerkTestZeros();
	PerkTestSet(StalwartOnlyRank2, TEXT("Stalwart"), 2);
	Check(TEXT("rank 2 of tier 2 alone is still locked"), None, StalwartOnlyRank2, 5, 10.0, false, EMDPerkReject::PathLocked);

	// Capstone needs 4 other points: 3 fails, 4 passes.
	TArray<uint8> BulwarkThree = PerkTestZeros();
	PerkTestSet(BulwarkThree, TEXT("Hardened"), 2);
	PerkTestSet(BulwarkThree, TEXT("ThickHide"), 1);
	PerkTestSet(BulwarkThree, TEXT("Bulwark"), 1);
	Check(TEXT("capstone with 3 other points is locked"), None, BulwarkThree, 5, 10.0, false, EMDPerkReject::PathLocked);
	TArray<uint8> BulwarkFour = BulwarkThree;
	PerkTestSet(BulwarkFour, TEXT("ThickHide"), 2);
	Check(TEXT("capstone with 4 other points is open"), None, BulwarkFour, 5, 10.0, false, EMDPerkReject::None);

	// Path cap: a 6th point in one path is refused, a point in another path is fine.
	TArray<uint8> SixDefense = PerkTestZeros();
	PerkTestSet(SixDefense, TEXT("Hardened"), 2);
	PerkTestSet(SixDefense, TEXT("ThickHide"), 2);
	PerkTestSet(SixDefense, TEXT("Stalwart"), 2);
	Check(TEXT("6th point in one path"), None, SixDefense, 8, 10.0, false, EMDPerkReject::PathCap);
	TArray<uint8> FiveDefense = SixDefense;
	PerkTestSet(FiveDefense, TEXT("Stalwart"), 1);
	Check(TEXT("5 points in one path"), None, FiveDefense, 8, 10.0, false, EMDPerkReject::None);
	TArray<uint8> FiveAndOne = FiveDefense;
	PerkTestSet(FiveAndOne, TEXT("Sharpened"), 1);
	Check(TEXT("a point in another path is accepted"), None, FiveAndOne, 6, 10.0, false, EMDPerkReject::None);

	// Reducing a rank needs a station respec.
	TArray<uint8> Hardened2 = PerkTestZeros();
	PerkTestSet(Hardened2, TEXT("Hardened"), 2);
	Check(TEXT("rank reduction is refused"), Hardened2, Hardened1, 5, 10.0, false, EMDPerkReject::WouldReduceRank);
	Check(TEXT("adding on top of owned ranks is fine"), Hardened1, Hardened2, 2, 10.0, false, EMDPerkReject::None);

	// Bad arrays.
	TArray<uint8> Short = PerkTestZeros();
	Short.RemoveAt(0);
	Check(TEXT("bad array length"), None, Short, 5, 10.0, false, EMDPerkReject::InvalidRanks);
	TArray<uint8> TooHigh = PerkTestZeros();
	PerkTestSet(TooHigh, TEXT("Hardened"), 3);
	Check(TEXT("rank above the node maximum"), None, TooHigh, 9, 10.0, false, EMDPerkReject::InvalidRanks);
	TArray<uint8> CapstoneTwo = PerkTestZeros();
	PerkTestSet(CapstoneTwo, TEXT("Bulwark"), 2);
	Check(TEXT("capstone rank 2"), None, CapstoneTwo, 9, 10.0, false, EMDPerkReject::InvalidRanks);

	// Dead and in combat.
	Check(TEXT("dead"), None, Hardened1, 1, 10.0, true, EMDPerkReject::Dead);
	Check(TEXT("4.9 s since damage"), None, Hardened1, 1, 4.9, false, EMDPerkReject::InCombat);
	Check(TEXT("5.0 s since damage"), None, Hardened1, 1, 5.0, false, EMDPerkReject::None);

	// All or nothing: one valid change plus one invalid change rejects the whole array; each valid part passes alone.
	TArray<uint8> Mixed = Hardened1;
	PerkTestSet(Mixed, TEXT("Stalwart"), 1);
	Check(TEXT("mixed valid + locked is rejected"), None, Mixed, 5, 10.0, false, EMDPerkReject::PathLocked);
	Check(TEXT("the valid part alone is accepted"), None, Hardened1, 5, 10.0, false, EMDPerkReject::None);

	// Detail for the toasts.
	int32 Arg = 0;
	EMDPerkPath Path = EMDPerkPath::None;
	TestEqual(TEXT("combat detail"), UMDPerkCatalog::ValidateCommitEx(None, Hardened1, 1, 4.9, false, Tree, FMDPerkTuning(), Arg, Path), EMDPerkReject::InCombat);
	TestEqual(TEXT("combat seconds left"), Arg, 1);
	TestEqual(TEXT("locked detail"), UMDPerkCatalog::ValidateCommitEx(None, StalwartOnly, 5, 10.0, false, Tree, FMDPerkTuning(), Arg, Path), EMDPerkReject::PathLocked);
	TestEqual(TEXT("locked needs 2"), Arg, 2);
	TestEqual(TEXT("locked path"), Path, EMDPerkPath::Defense);
	TestEqual(TEXT("cap detail"), UMDPerkCatalog::ValidateCommitEx(None, SixDefense, 8, 10.0, false, Tree, FMDPerkTuning(), Arg, Path), EMDPerkReject::PathCap);
	TestEqual(TEXT("cap is 5"), Arg, 5);

	for (int32 Reason = 1; Reason <= static_cast<int32>(EMDPerkReject::NotAtStation); ++Reason)
	{
		TestFalse(*FString::Printf(TEXT("text for reject %d"), Reason), UMDPerkCatalog::GetPerkRejectText(static_cast<EMDPerkReject>(Reason), 3, EMDPerkPath::Offense).IsEmpty());
	}
	TestTrue(TEXT("None has no text"), UMDPerkCatalog::GetPerkRejectText(EMDPerkReject::None).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkPointsTest, "MazeDemons.Perk.PointsFromProgressLateJoin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkPointsTest::RunTest(const FString& Parameters)
{
	const FMDPerkTuning Tuning;
	TestEqual(TEXT("fresh run"), UMDPerkCatalog::ComputePointsEarned(0, 0, 0, Tuning), 1);
	TestEqual(TEXT("full run"), UMDPerkCatalog::ComputePointsEarned(4, 15, 0, Tuning), 8);
	TestEqual(TEXT("souls 4"), UMDPerkCatalog::ComputePointsEarned(0, 4, 0, Tuning), 1);
	TestEqual(TEXT("souls 5 adds one"), UMDPerkCatalog::ComputePointsEarned(0, 5, 0, Tuning), 2);
	TestEqual(TEXT("souls 9"), UMDPerkCatalog::ComputePointsEarned(0, 9, 0, Tuning), 2);
	TestEqual(TEXT("souls 10 adds another"), UMDPerkCatalog::ComputePointsEarned(0, 10, 0, Tuning), 3);
	TestEqual(TEXT("two zones"), UMDPerkCatalog::ComputePointsEarned(2, 0, 0, Tuning), 3);
	TestEqual(TEXT("debug points add on top"), UMDPerkCatalog::ComputePointsEarned(4, 15, 3, Tuning), 11);
	TestEqual(TEXT("negative debug points are ignored"), UMDPerkCatalog::ComputePointsEarned(0, 0, -5, Tuning), 1);

	// Never above 8 without debug points, whatever the counters say.
	int32 Highest = 0;
	for (int32 Zones = -2; Zones <= 12; ++Zones)
	{
		for (int32 Souls = -2; Souls <= 40; ++Souls)
		{
			Highest = FMath::Max(Highest, UMDPerkCatalog::ComputePointsEarned(Zones, Souls, 0, Tuning));
		}
	}
	TestEqual(TEXT("budget is 8"), Highest, 8);

	// A late joiner sees the same replicated counters and computes the same total.
	TestEqual(TEXT("late joiner matches"), UMDPerkCatalog::ComputePointsEarned(2, 7, 0, Tuning), UMDPerkCatalog::ComputePointsEarned(2, 7, 0, Tuning));

	// Through the component and the replicated counters.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	AMDGameState* State = Fx.World->SpawnActor<AMDGameState>();
	if (!State || Fx.World->GetGameState<AMDGameState>() != State)
	{
		AddWarning(TEXT("AMDGameState did not register as the world game state in the test world; component part skipped"));
		return true;
	}
	State->SetRole(ROLE_Authority);
	TestEqual(TEXT("component: fresh"), Fx.Perk->GetPointsEarned(), 1);
	State->SetZonesCompleted(9);
	TestEqual(TEXT("zones clamp at 4"), State->ZonesCompleted, 4);
	State->SetSoulsSaved(15);
	State->AddSoulsSaved(-100);
	TestEqual(TEXT("souls never negative"), State->SoulsSaved, 0);
	State->SetSoulsSaved(15);
	TestEqual(TEXT("component: full run"), Fx.Perk->GetPointsEarned(), 8);
	Fx.Perk->DebugAddPoints(2);
	TestEqual(TEXT("component: with debug points"), Fx.Perk->GetPointsEarned(), 10);
	TestEqual(TEXT("component: nothing spent"), Fx.Perk->GetPointsAvailable(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkAppliesAttributesTest, "MazeDemons.Perk.AppliesAttributes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkAppliesAttributesTest::RunTest(const FString& Parameters)
{
	const float Tol = 0.0001f;
	TestNearlyEqual(TEXT("Hardened 1"), PerkTestOneNode(TEXT("Hardened"), 1).DamageReductionPP, 3.0f, Tol);
	TestNearlyEqual(TEXT("Hardened 2"), PerkTestOneNode(TEXT("Hardened"), 2).DamageReductionPP, 6.0f, Tol);
	TestNearlyEqual(TEXT("Thick Hide 1"), PerkTestOneNode(TEXT("ThickHide"), 1).FlatDamageReduction, 2.0f, Tol);
	TestNearlyEqual(TEXT("Thick Hide 2"), PerkTestOneNode(TEXT("ThickHide"), 2).FlatDamageReduction, 4.0f, Tol);
	TestNearlyEqual(TEXT("Stalwart 1"), PerkTestOneNode(TEXT("Stalwart"), 1).StalwartPP, 5.0f, Tol);
	TestNearlyEqual(TEXT("Stalwart 2"), PerkTestOneNode(TEXT("Stalwart"), 2).StalwartPP, 10.0f, Tol);
	TestTrue(TEXT("Bulwark"), PerkTestOneNode(TEXT("Bulwark"), 1).bBulwark);
	TestNearlyEqual(TEXT("Sharpened 1"), PerkTestOneNode(TEXT("Sharpened"), 1).AttackPowerPct, 0.06f, Tol);
	TestNearlyEqual(TEXT("Sharpened 2"), PerkTestOneNode(TEXT("Sharpened"), 2).AttackPowerPct, 0.12f, Tol);
	TestNearlyEqual(TEXT("Precision 1"), PerkTestOneNode(TEXT("Precision"), 1).CritChance, 0.05f, Tol);
	TestNearlyEqual(TEXT("Precision 2"), PerkTestOneNode(TEXT("Precision"), 2).CritChance, 0.10f, Tol);
	TestNearlyEqual(TEXT("Ferocity 1 adds 0.15 to 1.5"), PerkTestOneNode(TEXT("Ferocity"), 1).CritDamage, 1.65f, Tol);
	TestNearlyEqual(TEXT("Ferocity 2 adds 0.30"), PerkTestOneNode(TEXT("Ferocity"), 2).CritDamage, 1.80f, Tol);
	TestNearlyEqual(TEXT("Executioner"), PerkTestOneNode(TEXT("Executioner"), 1).ExecutionerPct, 0.20f, Tol);
	TestNearlyEqual(TEXT("Robust 1"), PerkTestOneNode(TEXT("Robust"), 1).MaxHealthBonus, 10.0f, Tol);
	TestNearlyEqual(TEXT("Robust 2"), PerkTestOneNode(TEXT("Robust"), 2).MaxHealthBonus, 20.0f, Tol);
	TestNearlyEqual(TEXT("Recovery 1"), PerkTestOneNode(TEXT("Recovery"), 1).HealthRegen, 0.5f, Tol);
	TestNearlyEqual(TEXT("Recovery 2"), PerkTestOneNode(TEXT("Recovery"), 2).HealthRegen, 1.0f, Tol);
	TestNearlyEqual(TEXT("Potent Draughts 1"), PerkTestOneNode(TEXT("PotentDraughts"), 1).PotionHealPct, 0.20f, Tol);
	TestNearlyEqual(TEXT("Potent Draughts 2"), PerkTestOneNode(TEXT("PotentDraughts"), 2).PotionHealPct, 0.40f, Tol);
	TestTrue(TEXT("Second Wind"), PerkTestOneNode(TEXT("SecondWind"), 1).bSecondWind);
	TestNearlyEqual(TEXT("Endurance 1"), PerkTestOneNode(TEXT("Endurance"), 1).MaxStaminaBonus, 10.0f, Tol);
	TestNearlyEqual(TEXT("Endurance 2"), PerkTestOneNode(TEXT("Endurance"), 2).MaxStaminaBonus, 20.0f, Tol);
	TestNearlyEqual(TEXT("Quick Breath 1"), PerkTestOneNode(TEXT("QuickBreath"), 1).StaminaRegenPct, 0.15f, Tol);
	TestNearlyEqual(TEXT("Quick Breath 2"), PerkTestOneNode(TEXT("QuickBreath"), 2).StaminaRegenPct, 0.30f, Tol);
	TestNearlyEqual(TEXT("Light Step 1"), PerkTestOneNode(TEXT("LightStep"), 1).StaminaCostScale, 0.90f, Tol);
	TestNearlyEqual(TEXT("Light Step 2"), PerkTestOneNode(TEXT("LightStep"), 2).StaminaCostScale, 0.80f, Tol);
	TestNearlyEqual(TEXT("Fleet sprint speed"), PerkTestOneNode(TEXT("Fleet"), 1).SprintSpeedScale, 1.05f, Tol);
	TestNearlyEqual(TEXT("Fleet regen delay"), PerkTestOneNode(TEXT("Fleet"), 1).RegenDelayReduction, 0.5f, Tol);

	// Unrelated stats stay at their defaults.
	const FMDPerkModifiers Hardened = PerkTestOneNode(TEXT("Hardened"), 2);
	TestEqual(TEXT("crit damage default"), Hardened.CritDamage, 1.5f);
	TestEqual(TEXT("stamina cost default"), Hardened.StaminaCostScale, 1.0f);
	TestFalse(TEXT("no bulwark"), Hardened.bBulwark);

	// BALANCE section 7 caps, with lowered caps on a local tree.
	UMDPerkTree* Capped = NewObject<UMDPerkTree>(GetTransientPackage());
	Capped->AddToRoot();
	Capped->Caps.CritChanceCap = 0.05f;
	Capped->Caps.CritDamageCap = 1.6f;
	Capped->Caps.MaxHealthFromPerks = 15.0f;
	Capped->Caps.HealthRegenCap = 0.75f;
	Capped->Caps.StaminaCostScaleMin = 0.85f;
	Capped->Caps.StaminaRegenBonusCap = 0.2f;
	Capped->Caps.SprintSpeedBonusCap = 0.01f;
	TArray<uint8> All = PerkTestZeros();
	PerkTestSet(All, TEXT("Precision"), 2);
	PerkTestSet(All, TEXT("Ferocity"), 2);
	PerkTestSet(All, TEXT("Robust"), 2);
	PerkTestSet(All, TEXT("Recovery"), 2);
	PerkTestSet(All, TEXT("LightStep"), 2);
	PerkTestSet(All, TEXT("QuickBreath"), 2);
	PerkTestSet(All, TEXT("Fleet"), 1);
	FMDPerkModifiers Mods;
	UMDPerkCatalog::ComputeModifiers(All, *Capped, Mods);
	TestNearlyEqual(TEXT("crit chance cap"), Mods.CritChance, 0.05f, Tol);
	TestNearlyEqual(TEXT("crit damage cap"), Mods.CritDamage, 1.6f, Tol);
	TestNearlyEqual(TEXT("max health cap"), Mods.MaxHealthBonus, 15.0f, Tol);
	TestNearlyEqual(TEXT("health regen cap"), Mods.HealthRegen, 0.75f, Tol);
	TestNearlyEqual(TEXT("stamina cost floor"), Mods.StaminaCostScale, 0.85f, Tol);
	TestNearlyEqual(TEXT("stamina regen cap"), Mods.StaminaRegenPct, 0.2f, Tol);
	TestNearlyEqual(TEXT("sprint speed cap"), Mods.SprintSpeedScale, 1.01f, Tol);
	Capped->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkReapplyTest, "MazeDemons.Perk.ReapplyDeterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkReapplyTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Forward = PerkTestZeros();
	PerkTestSet(Forward, TEXT("Hardened"), 2);
	PerkTestSet(Forward, TEXT("Sharpened"), 1);
	PerkTestSet(Forward, TEXT("Robust"), 2);
	PerkTestSet(Forward, TEXT("LightStep"), 1);

	TArray<uint8> Reverse = PerkTestZeros();
	PerkTestSet(Reverse, TEXT("LightStep"), 1);
	PerkTestSet(Reverse, TEXT("Robust"), 2);
	PerkTestSet(Reverse, TEXT("Sharpened"), 1);
	PerkTestSet(Reverse, TEXT("Hardened"), 2);

	const FMDPerkModifiers First = PerkTestMods(Forward);
	TestTrue(TEXT("same ranks twice give identical modifiers"), First == PerkTestMods(Forward));
	TestTrue(TEXT("a different spend order gives identical modifiers"), First == PerkTestMods(Reverse));

	bool bDrift = false;
	for (int32 i = 0; i < 200; ++i)
	{
		bDrift |= (PerkTestMods(Forward) != First);
	}
	TestFalse(TEXT("200 re-applications do not drift"), bDrift);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkRespecTest, "MazeDemons.Perk.RespecRestoresBaseline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkRespecTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Build = PerkTestZeros();
	PerkTestSet(Build, TEXT("Hardened"), 2);
	PerkTestSet(Build, TEXT("Precision"), 2);
	PerkTestSet(Build, TEXT("Endurance"), 2);
	PerkTestSet(Build, TEXT("Bulwark"), 0);
	TestTrue(TEXT("a build changes the modifiers"), PerkTestMods(Build) != FMDPerkModifiers());

	// Respec = all ranks 0: exactly the default modifier set (bit for bit), same as a fresh character.
	const FMDPerkModifiers Respecced = PerkTestMods(PerkTestZeros());
	TestTrue(TEXT("respec equals the empty set"), Respecced == FMDPerkModifiers());
	TestTrue(TEXT("and equals a character with no ranks array at all"), PerkTestMods(TArray<uint8>()) == FMDPerkModifiers());
	TestEqual(TEXT("crit damage is the base 1.5"), Respecced.CritDamage, 1.5f);
	TestEqual(TEXT("no points spent"), UMDPerkCatalog::TotalRanks(PerkTestZeros()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkMaxHealthDeltaTest, "MazeDemons.Perk.MaxHealthDeltaRule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkMaxHealthDeltaTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("rise adds the difference"), UMDPerkCatalog::CurrentAfterMaxChange(100.0f, 120.0f, 80.0f), 100.0f);
	TestEqual(TEXT("rise never overheals"), UMDPerkCatalog::CurrentAfterMaxChange(100.0f, 120.0f, 110.0f), 120.0f);
	TestEqual(TEXT("full health rises with the max"), UMDPerkCatalog::CurrentAfterMaxChange(100.0f, 120.0f, 100.0f), 120.0f);
	TestEqual(TEXT("fall clamps"), UMDPerkCatalog::CurrentAfterMaxChange(120.0f, 100.0f, 110.0f), 100.0f);
	TestEqual(TEXT("fall below current keeps current"), UMDPerkCatalog::CurrentAfterMaxChange(120.0f, 100.0f, 50.0f), 50.0f);
	TestEqual(TEXT("no change"), UMDPerkCatalog::CurrentAfterMaxChange(100.0f, 100.0f, 60.0f), 60.0f);
	TestEqual(TEXT("dead stays dead on a rise"), UMDPerkCatalog::CurrentAfterMaxChange(100.0f, 120.0f, 0.0f), 0.0f);
	TestEqual(TEXT("dead stays dead on a fall"), UMDPerkCatalog::CurrentAfterMaxChange(120.0f, 100.0f, 0.0f), 0.0f);
	TestEqual(TEXT("a fall never kills"), UMDPerkCatalog::CurrentAfterMaxChange(120.0f, 0.2f, 50.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkDRCapTest, "MazeDemons.Perk.DRCap65", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkDRCapTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	TArray<uint8> Build = PerkTestZeros();
	PerkTestSet(Build, TEXT("Hardened"), 2);
	PerkTestSet(Build, TEXT("ThickHide"), 2);
	PerkTestSet(Build, TEXT("Stalwart"), 2);
	const FMDPerkModifiers Mods = PerkTestMods(Build);
	const float PerkDR = Mods.DamageReductionPP + Mods.StalwartPP; // Stalwart active
	TestNearlyEqual(TEXT("Hardened 2 + Stalwart 2 = 16 pp"), PerkDR, 16.0f, 0.001f);

	// Five fully upgraded armor pieces (12% row DR, +3 levels) on top of the perk piece.
	TArray<float> Pieces;
	for (int32 i = 0; i < 5; ++i)
	{
		Pieces.Add(UMDStatCalculator::EffectivePieceDR(12.0f, 3, 1.0f, Tuning));
	}
	Pieces.Add(PerkDR);
	TestEqual(TEXT("perk DR + full upgraded armor stays at the player cap"), UMDStatCalculator::CombineDR(0.0f, Pieces, Tuning.PlayerDRCap), 65.0f);

	TArray<float> Perks;
	Perks.Add(PerkDR);
	TestTrue(TEXT("perks alone are far below the cap"), UMDStatCalculator::CombineDR(0.0f, Perks, Tuning.PlayerDRCap) < 20.0f);

	// Through the ASC: the "Perk" entry goes through RecalculateDefense, so the cap applies for free.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	Fx.ASC->DefenseCap = Tuning.PlayerDRCap;
	Fx.ASC->SetPerkDamageReduction(PerkDR);
	TestNearlyEqual(TEXT("ASC: perk DR alone"), Fx.ASC->GetDefense(), PerkDR, 0.001f);
	const FName Slots[] = { FName(TEXT("Helmet")), FName(TEXT("Chest")), FName(TEXT("Gloves")), FName(TEXT("Legs")), FName(TEXT("Boots")) };
	for (const FName& Slot : Slots)
	{
		Fx.ASC->SetArmorPieceDamageReduction(Slot, Pieces[0]);
	}
	TestEqual(TEXT("ASC: capped at 65"), Fx.ASC->GetDefense(), 65.0f);
	Fx.ASC->SetPerkDamageReduction(0.0f);
	TestTrue(TEXT("ASC: removing the perk piece lowers it again (or it stays capped)"), Fx.ASC->GetDefense() <= 65.0f);
	for (const FName& Slot : Slots)
	{
		Fx.ASC->RemoveArmorPieceDamageReduction(Slot);
	}
	TestEqual(TEXT("ASC: back to the base"), Fx.ASC->GetDefense(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkStaminaScaleTest, "MazeDemons.Perk.StaminaCostScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkStaminaScaleTest::RunTest(const FString& Parameters)
{
	// Pure scaling.
	TestNearlyEqual(TEXT("0.8 scale"), UMDPerkCatalog::ScaleStaminaCost(10.0f, 0.8f), 8.0f, 0.0001f);
	TestNearlyEqual(TEXT("floor at 0.7"), UMDPerkCatalog::ScaleStaminaCost(10.0f, 0.5f), 7.0f, 0.0001f);
	TestEqual(TEXT("no perks"), UMDPerkCatalog::ScaleStaminaCost(10.0f, 1.0f), 10.0f);
	TestEqual(TEXT("never above 1"), UMDPerkCatalog::ScaleStaminaCost(10.0f, 1.5f), 10.0f);
	TestNearlyEqual(TEXT("regen delay 1 - 0.5"), UMDPerkCatalog::EffectiveRegenDelay(1.0f, 0.5f), 0.5f, 0.0001f);
	TestEqual(TEXT("regen delay never negative"), UMDPerkCatalog::EffectiveRegenDelay(1.0f, 2.0f), 0.0f);

	// Step() is unchanged: the caller scales the sprint drain. 15 /s at 0.8 = 12 /s.
	FMDStaminaStepInput In;
	In.Stamina = 100.0f;
	In.MaxStamina = 100.0f;
	In.DeltaTime = 1.0f;
	In.TimeSinceSpend = 10.0f;
	In.bSprinting = true;
	In.SprintDrainPerSec = UMDPerkCatalog::ScaleStaminaCost(15.0f, 0.8f);
	TestNearlyEqual(TEXT("scaled sprint drain"), UMDStaminaComponent::Step(In).Stamina, 88.0f, 0.001f);
	In.SprintDrainPerSec = 15.0f;
	TestEqual(TEXT("unscaled sprint drain is as before"), UMDStaminaComponent::Step(In).Stamina, 85.0f);

	// Component: Consume, HasStaminaFor and the jump / vault gates use the attribute.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	UMDAttributeSet* Attrs = Fx.ASC->GetMazeDemonsAttributes();
	Fx.Stamina->Consume(10.0f);
	TestEqual(TEXT("no perks: a 10 cost takes 10"), Fx.ASC->GetStamina(), 90.0f);

	Fx.ASC->ResetStaminaToMax();
	Attrs->SetStaminaCostScale(0.8f);
	Fx.Stamina->Consume(10.0f);
	TestNearlyEqual(TEXT("0.8 scale: a 10 cost takes 8"), Fx.ASC->GetStamina(), 92.0f, 0.001f);

	Fx.ASC->SetStaminaClamped(8.05f);
	TestTrue(TEXT("HasStaminaFor(10) with just the scaled cost"), Fx.Stamina->HasStaminaFor(10.0f));
	Fx.ASC->SetStaminaClamped(7.9f);
	TestFalse(TEXT("HasStaminaFor(10) just below the scaled cost"), Fx.Stamina->HasStaminaFor(10.0f));
	Fx.ASC->SetStaminaClamped(8.05f);
	TestTrue(TEXT("CanJump uses the scaled cost (10 -> 8)"), Fx.Stamina->CanJump());
	TestFalse(TEXT("CanVault uses the scaled cost (15 -> 12)"), Fx.Stamina->CanVault());

	Fx.ASC->ResetStaminaToMax();
	Attrs->SetStaminaCostScale(0.1f); // the attribute clamps at 0.7
	Fx.Stamina->Consume(10.0f);
	TestNearlyEqual(TEXT("floor 0.7: a 10 cost takes 7"), Fx.ASC->GetStamina(), 93.0f, 0.001f);

	TestEqual(TEXT("regen delay without perks"), Fx.Stamina->GetEffectiveRegenDelay(), Fx.Stamina->RegenDelay);
	Fx.Stamina->SetRegenDelayModifier(0.5f);
	TestNearlyEqual(TEXT("Fleet: regen delay 0.5 s shorter"), Fx.Stamina->GetEffectiveRegenDelay(), Fx.Stamina->RegenDelay - 0.5f, 0.0001f);
	Fx.Stamina->SetRegenDelayModifier(5.0f);
	TestEqual(TEXT("regen delay is never negative"), Fx.Stamina->GetEffectiveRegenDelay(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkCritSeededTest, "MazeDemons.Perk.CritSeeded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkCritSeededTest::RunTest(const FString& Parameters)
{
	// Pure roll rules.
	TestFalse(TEXT("chance 0 never crits"), UMDPerkCatalog::IsCrit(0.0f, 0.0f));
	TestTrue(TEXT("roll below the chance crits"), UMDPerkCatalog::IsCrit(0.25f, 0.2499f));
	TestFalse(TEXT("roll at the chance does not"), UMDPerkCatalog::IsCrit(0.25f, 0.25f));
	TestEqual(TEXT("x1.5 multiplier"), UMDPerkCatalog::ApplyCrit(10.0f, 0.5f, 1.5f, 0.1f), 15.0f);
	TestEqual(TEXT("x1.8 multiplier"), UMDPerkCatalog::ApplyCrit(10.0f, 0.5f, 1.8f, 0.1f), 18.0f);
	TestEqual(TEXT("no crit leaves the damage"), UMDPerkCatalog::ApplyCrit(10.0f, 0.5f, 1.8f, 0.9f), 10.0f);
	TestEqual(TEXT("a multiplier below 1 never reduces damage"), UMDPerkCatalog::ApplyCrit(10.0f, 0.5f, 0.5f, 0.1f), 10.0f);

	// Executioner threshold.
	TestNearlyEqual(TEXT("29.9% health is executed"), UMDPerkCatalog::ApplyExecutioner(100.0f, 0.299f, 0.2f, 0.3f), 120.0f, 0.001f);
	TestEqual(TEXT("30% health is not"), UMDPerkCatalog::ApplyExecutioner(100.0f, 0.3f, 0.2f, 0.3f), 100.0f);
	TestEqual(TEXT("no perk, no bonus"), UMDPerkCatalog::ApplyExecutioner(100.0f, 0.1f, 0.0f, 0.3f), 100.0f);

	// Order: crit first, then Executioner (roll injected): 10 * 1.5 * 1.2 = 18.
	const float Crit = UMDPerkCatalog::ApplyCrit(10.0f, 0.1f, 1.5f, 0.05f);
	TestNearlyEqual(TEXT("crit then executioner"), UMDPerkCatalog::ApplyExecutioner(Crit, 0.2f, 0.2f, 0.3f), 18.0f, 0.001f);

	TestEqual(TEXT("seed = player id xor cvar"), UMDPerkComponent::ComputeCritSeed(5, 3), 5 ^ 3);

	// The seeded stream: the same seed gives the same sequence.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	UMDPerkComponent* A = NewObject<UMDPerkComponent>(Fx.PS);
	UMDPerkComponent* B = NewObject<UMDPerkComponent>(Fx.PS);
	bool bSame = true;
	int32 Crits = 0;
	const int32 Rolls = 4000;
	for (int32 i = 0; i < Rolls; ++i)
	{
		const bool bA = A->RollCrit(0.25f);
		bSame &= (bA == B->RollCrit(0.25f));
		Crits += bA ? 1 : 0;
	}
	TestTrue(TEXT("same seed, same sequence"), bSame);
	TestTrue(TEXT("a 25% chance crits about 1 in 4"), Crits > 850 && Crits < 1150);
	TestFalse(TEXT("chance 0 never crits"), A->RollCrit(0.0f));
	TestTrue(TEXT("chance 1 always crits"), A->RollCrit(1.0f));

	// Hard cap on the attribute.
	UMDAttributeSet* Attrs = Fx.ASC->GetMazeDemonsAttributes();
	Attrs->SetCritChance(0.9f);
	TestEqual(TEXT("crit chance capped at 25%"), Fx.ASC->GetCritChance(), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkThickHideBulwarkTest, "MazeDemons.Perk.ThickHideAndBulwark", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkThickHideBulwarkTest::RunTest(const FString& Parameters)
{
	const FMDPerkTuning Tuning;
	auto Rules = [&](float Damage, float MaxHealth, float Flat, bool bBulwark)
	{
		return UMDPerkCatalog::ApplyIncomingDamageRules(Damage, MaxHealth, Flat, bBulwark, Tuning);
	};

	// No perks: untouched, whatever the size.
	TestEqual(TEXT("no perks, big hit"), Rules(37.5f, 100.0f, 0.0f, false), 37.5f);
	TestEqual(TEXT("no perks, tiny hit"), Rules(0.3f, 100.0f, 0.0f, false), 0.3f);

	// Thick Hide.
	TestEqual(TEXT("flat 2 off a 10 hit"), Rules(10.0f, 100.0f, 2.0f, false), 8.0f);
	TestEqual(TEXT("never below 1"), Rules(1.5f, 100.0f, 2.0f, false), 1.0f);
	TestEqual(TEXT("flat 4 off a 3 hit leaves 1"), Rules(3.0f, 100.0f, 4.0f, false), 1.0f);
	TestEqual(TEXT("a hit already below 1 is not raised"), Rules(0.5f, 100.0f, 2.0f, false), 0.5f);

	// Bulwark.
	TestEqual(TEXT("bulwark caps a 50 hit at 30"), Rules(50.0f, 100.0f, 0.0f, true), 30.0f);
	TestEqual(TEXT("bulwark at exactly 30"), Rules(30.0f, 100.0f, 0.0f, true), 30.0f);
	TestEqual(TEXT("bulwark leaves a 20 hit"), Rules(20.0f, 100.0f, 0.0f, true), 20.0f);
	TestNearlyEqual(TEXT("bulwark scales with max health"), Rules(80.0f, 120.0f, 0.0f, true), 36.0f, 0.001f);

	// Order: flat reduction first, then the cap.
	TestEqual(TEXT("flat then cap, capped"), Rules(100.0f, 100.0f, 2.0f, true), 30.0f);
	TestEqual(TEXT("flat then cap, flat first (31 - 2 = 29 < 30)"), Rules(31.0f, 100.0f, 2.0f, true), 29.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkBulwarkCapsHitTest, "MazeDemons.Perk.BulwarkCapsHit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkBulwarkCapsHitTest::RunTest(const FString& Parameters)
{
	FMDPerkCombatState State;
	State.bBulwark = true;
	const FMDIncomingDamageResult Hit = UMDPerkCatalog::ResolveIncomingDamage(80.0f, 100.0f, 100.0f, State);
	TestEqual(TEXT("an 80 hit takes 30"), Hit.DamageTaken, 30.0f);
	TestEqual(TEXT("health 70 left"), Hit.NewHealth, 70.0f);
	TestFalse(TEXT("no second wind involved"), Hit.bSecondWindTriggered);

	// A hit that would kill from full health is survived by the cap alone.
	const FMDIncomingDamageResult Huge = UMDPerkCatalog::ResolveIncomingDamage(1000.0f, 100.0f, 100.0f, State);
	TestEqual(TEXT("1000 damage takes 30"), Huge.NewHealth, 70.0f);

	// ... but repeated capped hits still kill.
	float Health = 100.0f;
	for (int32 i = 0; i < 4; ++i)
	{
		Health = UMDPerkCatalog::ResolveIncomingDamage(1000.0f, Health, 100.0f, State).NewHealth;
	}
	TestEqual(TEXT("four capped hits kill"), Health, 0.0f);

	const FMDIncomingDamageResult Fresh = UMDPerkCatalog::ResolveIncomingDamage(40.0f, 100.0f, 100.0f, FMDPerkCombatState());
	TestEqual(TEXT("no perks: health - damage"), Fresh.NewHealth, 60.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkSecondWindTest, "MazeDemons.Perk.SecondWindOncePerLife", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkSecondWindTest::RunTest(const FString& Parameters)
{
	// Pure rule.
	FMDPerkCombatState State;
	State.bSecondWindAvailable = true;
	const FMDIncomingDamageResult Lethal = UMDPerkCatalog::ResolveIncomingDamage(50.0f, 20.0f, 100.0f, State);
	TestTrue(TEXT("a lethal hit triggers Second Wind"), Lethal.bSecondWindTriggered);
	TestEqual(TEXT("and leaves 1 HP"), Lethal.NewHealth, 1.0f);
	const FMDIncomingDamageResult Exact = UMDPerkCatalog::ResolveIncomingDamage(20.0f, 20.0f, 100.0f, State);
	TestTrue(TEXT("an exactly lethal hit triggers it too"), Exact.bSecondWindTriggered);
	const FMDIncomingDamageResult Light = UMDPerkCatalog::ResolveIncomingDamage(10.0f, 100.0f, 100.0f, State);
	TestFalse(TEXT("a survivable hit does not"), Light.bSecondWindTriggered);
	TestEqual(TEXT("survivable hit health"), Light.NewHealth, 90.0f);
	const FMDIncomingDamageResult Dead = UMDPerkCatalog::ResolveIncomingDamage(10.0f, 0.0f, 100.0f, State);
	TestFalse(TEXT("a dead character is not revived"), Dead.bSecondWindTriggered);

	State.bSecondWindAvailable = false;
	const FMDIncomingDamageResult Second = UMDPerkCatalog::ResolveIncomingDamage(50.0f, 20.0f, 100.0f, State);
	TestFalse(TEXT("second lethal hit: no Second Wind"), Second.bSecondWindTriggered);
	TestEqual(TEXT("second lethal hit kills"), Second.NewHealth, 0.0f);

	const FMDPerkTuning Tuning;
	TestEqual(TEXT("heals 25%"), Tuning.SecondWindHealFraction, 0.25f);
	TestEqual(TEXT("over 3 seconds"), Tuning.SecondWindHealSeconds, 3.0f);

	// Component: once per life, heal over 3 s, resets with the new life.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	UMDAttributeSet* Attrs = Fx.ASC->GetMazeDemonsAttributes();
	Fx.Perk->InitializeForRun();
	FMDPerkTestAccess::GrantSecondWind(Fx.Perk);
	TestTrue(TEXT("available at the start of the life"), Fx.ASC->GetPerkCombatState().bSecondWindAvailable);
	TestFalse(TEXT("not used yet"), Fx.Perk->bSecondWindUsed);

	Attrs->SetHealth(1.0f); // what the attribute set leaves after the saving hit
	Fx.ASC->ConsumeSecondWind();
	TestTrue(TEXT("marked used"), Fx.Perk->bSecondWindUsed);
	TestFalse(TEXT("no longer available this life"), Fx.ASC->GetPerkCombatState().bSecondWindAvailable);

	for (int32 i = 0; i < 12; ++i)
	{
		FMDPerkTestAccess::TickSecondWind(Fx.Perk);
	}
	TestNearlyEqual(TEXT("healed 25 of 100 over 3 s"), Fx.ASC->GetHealth(), 26.0f, 0.01f);
	FMDPerkTestAccess::TickSecondWind(Fx.Perk);
	TestNearlyEqual(TEXT("the heal stops after 3 s"), Fx.ASC->GetHealth(), 26.0f, 0.01f);

	Fx.Perk->ResetLifeFlags(); // respawn
	TestFalse(TEXT("respawn: not used"), Fx.Perk->bSecondWindUsed);
	TestTrue(TEXT("respawn: available again"), Fx.ASC->GetPerkCombatState().bSecondWindAvailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPerkAttributeDefaultsTest, "MazeDemons.Attributes.PerkDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPerkAttributeDefaultsTest::RunTest(const FString& Parameters)
{
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	UMDAttributeSet* Attrs = Fx.ASC->GetMazeDemonsAttributes();
	TestEqual(TEXT("crit chance default"), Fx.ASC->GetCritChance(), 0.0f);
	TestEqual(TEXT("crit damage default"), Fx.ASC->GetCritDamage(), 1.5f);
	TestEqual(TEXT("health regen default"), Fx.ASC->GetHealthRegen(), 0.0f);
	TestEqual(TEXT("stamina cost scale default"), Fx.ASC->GetStaminaCostScale(), 1.0f);
	TestEqual(TEXT("sprint speed scale default"), Fx.ASC->GetSprintSpeedScale(), 1.0f);

	Attrs->SetCritChance(5.0f);
	TestEqual(TEXT("crit chance <= 0.25"), Fx.ASC->GetCritChance(), 0.25f);
	Attrs->SetCritChance(-1.0f);
	TestEqual(TEXT("crit chance >= 0"), Fx.ASC->GetCritChance(), 0.0f);
	Attrs->SetCritDamage(9.0f);
	TestEqual(TEXT("crit damage <= 2"), Fx.ASC->GetCritDamage(), 2.0f);
	Attrs->SetCritDamage(0.2f);
	TestEqual(TEXT("crit damage >= 1"), Fx.ASC->GetCritDamage(), 1.0f);
	Attrs->SetHealthRegen(9.0f);
	TestEqual(TEXT("health regen <= 1.5"), Fx.ASC->GetHealthRegen(), 1.5f);
	Attrs->SetStaminaCostScale(0.1f);
	TestEqual(TEXT("stamina cost scale >= 0.7"), Fx.ASC->GetStaminaCostScale(), 0.7f);
	Attrs->SetStaminaCostScale(3.0f);
	TestEqual(TEXT("stamina cost scale <= 1"), Fx.ASC->GetStaminaCostScale(), 1.0f);
	Attrs->SetSprintSpeedScale(3.0f);
	TestEqual(TEXT("sprint speed scale <= 1.05"), Fx.ASC->GetSprintSpeedScale(), 1.05f);
	Attrs->SetSprintSpeedScale(0.2f);
	TestEqual(TEXT("sprint speed scale >= 1"), Fx.ASC->GetSprintSpeedScale(), 1.0f);

	Attrs->SetMaxStamina(500.0f);
	TestEqual(TEXT("max stamina <= 150"), Fx.ASC->GetMaxStamina(), 150.0f);
	Attrs->SetStaminaRegenRate(100.0f);
	TestNearlyEqual(TEXT("stamina regen <= 20 * 1.45"), Fx.ASC->GetStaminaRegenRate(), 29.0f, 0.001f);

	// A fresh PlayerState starts with no perks and no effect: ranks all zero, nothing to list, six stat lines as before.
	TestEqual(TEXT("fresh ranks length"), Fx.Perk->Ranks.Num(), MD_NumPerkNodes);
	TestEqual(TEXT("fresh points spent"), Fx.Perk->GetPointsSpent(), 0);
	TestEqual(TEXT("fresh potion scale"), Fx.Perk->GetPotionHealScale(), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDEffectLinesTest, "MazeDemons.Effects.Lines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDEffectLinesTest::RunTest(const FString& Parameters)
{
	const FMDEconomyTuning Tuning;
	struct FCase { int32 Durability; bool bExpectLine; const TCHAR* Detail; };
	const FCase Cases[] = {
		{ 0, true, TEXT("50% effectiveness") },
		{ 12, true, TEXT("74% effectiveness") },
		{ 50, false, TEXT("") },
		{ 100, false, TEXT("") },
	};
	for (const FCase& Case : Cases)
	{
		const float Mult = UMDStatCalculator::ConditionMultiplier(Case.Durability, 100, Tuning);
		FMDEffectLine Line;
		const bool bLine = UMDGameplayStatics::MakeConditionLine(FText::FromString(TEXT("Chest")), Mult, Line);
		TestTrue(*FString::Printf(TEXT("durability %d line"), Case.Durability), bLine == Case.bExpectLine);
		if (bLine && Case.bExpectLine)
		{
			TestEqual(TEXT("detail"), Line.Detail.ToString(), FString(Case.Detail));
			TestTrue(TEXT("name mentions the item"), Line.Name.ToString().Contains(TEXT("Chest")));
			TestTrue(TEXT("debuff"), Line.bDebuff);
			TestEqual(TEXT("danger tone"), Line.Tone, EMDColor::Danger);
		}
	}

	// A fresh character returns no effect lines and no perk stat lines.
	FMDPerkFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	TestEqual(TEXT("fresh character: no effect lines"), UMDGameplayStatics::GetActiveEffectLines(Fx.PS).Num(), 0);
	TestEqual(TEXT("fresh character: the six original stat lines"), UMDGameplayStatics::GetCharacterStatLines(Fx.PS, NAME_None).Num(), 6);
	TestEqual(TEXT("no player state: no lines"), UMDGameplayStatics::GetActiveEffectLines(nullptr).Num(), 0);

	// A ranked perk shows up, with its source.
	Fx.Perk->Ranks[0] = 2; // Hardened
	const TArray<FMDEffectLine> Lines = UMDGameplayStatics::GetActiveEffectLines(Fx.PS);
	if (TestEqual(TEXT("one perk line"), Lines.Num(), 1))
	{
		TestTrue(TEXT("names the node and rank"), Lines[0].Name.ToString().Contains(TEXT("Hardened")) && Lines[0].Name.ToString().Contains(TEXT("2")));
		TestTrue(TEXT("detail has the total"), Lines[0].Detail.ToString().Contains(TEXT("6")));
		TestFalse(TEXT("not a debuff"), Lines[0].bDebuff);
	}
	Fx.Perk->Ranks[0] = 0;

	// Perk stat lines appear only when they differ from the default.
	Fx.ASC->GetMazeDemonsAttributes()->SetCritChance(0.1f);
	TestEqual(TEXT("crit chance adds crit chance + crit damage lines"), UMDGameplayStatics::GetCharacterStatLines(Fx.PS, NAME_None).Num(), 8);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
