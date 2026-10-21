#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDBalanceModel.h"
#include "MDPerkCatalog.h"
#include "MDPerkTypes.h"
#include "MDStatCalculator.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Pure balance-model tests (BALANCE section 10). No world, no actors, no assets.

namespace
{
	// BALANCE section 8 ceilings, as multiples of a bare character (sprint uptime is absolute).
	constexpr float MDBalCeilEHPPerks = 1.35f;
	constexpr float MDBalCeilDPSPerks = 1.35f;
	constexpr float MDBalCeilEHPGear = 2.0f;
	constexpr float MDBalCeilDPSGear = 1.6f;
	constexpr float MDBalCeilSprintUptime = 0.75f;

	// "With upgrades and gear": derived from FMDEconomyTuning (starter cloth set with every piece at the max upgrade level,
	// main weapon at the same level), see UMDBalanceModel::GetGearScenario.
	float BalGearDR()
	{
		float DR = 0.0f, AP = 1.0f;
		UMDBalanceModel::GetGearScenario(FMDEconomyTuning(), DR, AP);
		return DR;
	}

	float BalGearAP()
	{
		float DR = 0.0f, AP = 1.0f;
		UMDBalanceModel::GetGearScenario(FMDEconomyTuning(), DR, AP);
		return AP;
	}

	struct FMDBalBuild
	{
		TArray<uint8> Ranks;
		int32 Points = 0;
		int32 PathsUsed = 0;
		FMDPerkModifiers Mods;
		FMDBuildMetrics Perks;  // perks alone
		FMDBuildMetrics Geared; // perks + upgrades and gear
	};

	const UMDPerkTree& BalTree()
	{
		return *GetDefault<UMDPerkTree>();
	}

	// Every legal build with at most MaxPoints points. Legality is the real commit rule (ValidateCommit), checked per path
	// (prerequisites and the per-path cap are per path), then paths are combined under the total budget.
	void BalEnumerateBuilds(int32 MaxPoints, TFunctionRef<void(const TArray<uint8>&, int32)> Visit)
	{
		const UMDPerkTree& Tree = BalTree();
		TArray<uint8> Zeros;
		Zeros.Init(0, MD_NumPerkNodes);

		struct FPathCombo { uint8 Ranks[MD_NodesPerPerkPath]; int32 Points; };
		TArray<FPathCombo> Combos[4];
		for (int32 Path = 0; Path < 4; ++Path)
		{
			const int32 First = Path * MD_NodesPerPerkPath;
			for (int32 A = 0; A <= Tree.Nodes[First + 0].MaxRank; ++A)
			for (int32 B = 0; B <= Tree.Nodes[First + 1].MaxRank; ++B)
			for (int32 C = 0; C <= Tree.Nodes[First + 2].MaxRank; ++C)
			for (int32 D = 0; D <= Tree.Nodes[First + 3].MaxRank; ++D)
			{
				TArray<uint8> Desired = Zeros;
				Desired[First + 0] = static_cast<uint8>(A);
				Desired[First + 1] = static_cast<uint8>(B);
				Desired[First + 2] = static_cast<uint8>(C);
				Desired[First + 3] = static_cast<uint8>(D);
				if (UMDPerkCatalog::ValidateCommit(Zeros, Desired, 1000, 1000.0, false, Tree) != EMDPerkReject::None)
				{
					continue;
				}
				FPathCombo Combo;
				Combo.Ranks[0] = static_cast<uint8>(A);
				Combo.Ranks[1] = static_cast<uint8>(B);
				Combo.Ranks[2] = static_cast<uint8>(C);
				Combo.Ranks[3] = static_cast<uint8>(D);
				Combo.Points = A + B + C + D;
				Combos[Path].Add(Combo);
			}
		}

		TArray<uint8> Build = Zeros;
		for (const FPathCombo& P0 : Combos[0])
		for (const FPathCombo& P1 : Combos[1])
		{
			if (P0.Points + P1.Points > MaxPoints)
			{
				continue;
			}
			for (const FPathCombo& P2 : Combos[2])
			{
				if (P0.Points + P1.Points + P2.Points > MaxPoints)
				{
					continue;
				}
				for (const FPathCombo& P3 : Combos[3])
				{
					const int32 Total = P0.Points + P1.Points + P2.Points + P3.Points;
					if (Total > MaxPoints)
					{
						continue;
					}
					for (int32 i = 0; i < MD_NodesPerPerkPath; ++i)
					{
						Build[0 * MD_NodesPerPerkPath + i] = P0.Ranks[i];
						Build[1 * MD_NodesPerPerkPath + i] = P1.Ranks[i];
						Build[2 * MD_NodesPerPerkPath + i] = P2.Ranks[i];
						Build[3 * MD_NodesPerPerkPath + i] = P3.Ranks[i];
					}
					Visit(Build, Total);
				}
			}
		}
	}

	FString BalDescribe(const TArray<uint8>& Ranks)
	{
		FString Out;
		for (int32 i = 0; i < Ranks.Num() && i < BalTree().Nodes.Num(); ++i)
		{
			if (Ranks[i] > 0)
			{
				Out += FString::Printf(TEXT("%s%s %d"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *BalTree().Nodes[i].Id.ToString(), static_cast<int32>(Ranks[i]));
			}
		}
		return Out.IsEmpty() ? FString(TEXT("(no perks)")) : Out;
	}

	FMDBuildMetrics BalMetrics(const FMDPerkModifiers& Mods, float ExtraDR = 0.0f, float ExtraAP = 1.0f)
	{
		return UMDBalanceModel::Evaluate(Mods, FMDBalanceBase(), FMDBalanceReference(), FMDPerkTuning(), ExtraDR, ExtraAP);
	}

	// All builds with the full-run budget, metrics included.
	void BalCollect(TArray<FMDBalBuild>& Out)
	{
		const FMDBalanceReference Ref;
		BalEnumerateBuilds(Ref.FullRunPoints, [&Out](const TArray<uint8>& Ranks, int32 Points)
		{
			FMDBalBuild& Build = Out.AddDefaulted_GetRef();
			Build.Ranks = Ranks;
			Build.Points = Points;
			for (int32 Path = 0; Path < 4; ++Path)
			{
				Build.PathsUsed += UMDPerkCatalog::PointsInPath(Ranks, static_cast<EMDPerkPath>(Path + 1)) > 0 ? 1 : 0;
			}
			UMDPerkCatalog::ComputeModifiers(Ranks, BalTree(), Build.Mods);
			Build.Perks = BalMetrics(Build.Mods);
			Build.Geared = BalMetrics(Build.Mods, BalGearDR(), BalGearAP());
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBalanceExhaustiveBuildsTest, "MazeDemons.Balance.ExhaustiveBuilds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBalanceExhaustiveBuildsTest::RunTest(const FString& Parameters)
{
	TArray<FMDBalBuild> Builds;
	BalCollect(Builds);
	if (!TestTrue(TEXT("there are legal builds"), Builds.Num() > 1))
	{
		return false;
	}

	// The empty build is in the set and is the baseline.
	const FMDBuildMetrics Baseline = BalMetrics(FMDPerkModifiers());
	const FMDBuildMetrics BaselineGear = BalMetrics(FMDPerkModifiers(), BalGearDR(), BalGearAP());
	TestNearlyEqual(TEXT("baseline effective HP"), Baseline.EffectiveHP, 100.0f, 0.01f);
	TestNearlyEqual(TEXT("baseline effective HP vs werewolf (informational)"), Baseline.EffectiveHPWerewolf, 100.0f, 0.01f);
	TestNearlyEqual(TEXT("baseline sprint uptime ~53% (BALANCE section 8)"), Baseline.SprintUptime, 0.526f, 0.01f);
	TestNearlyEqual(TEXT("baseline DPS"), Baseline.DPS, 10.0f, 0.01f);

	// No legal build exceeds the budget or a per-path cap, and none is worse than a bare character.
	const FMDPerkCaps Caps;
	const FMDBalanceReference Ref;
	int32 MaxPointsSeen = 0;
	const FMDBalBuild* WorstEHP = nullptr;
	const FMDBalBuild* WorstDPS = nullptr;
	const FMDBalBuild* WorstUptime = nullptr;
	const FMDBalBuild* WorstEHPGear = nullptr;
	const FMDBalBuild* WorstDPSGear = nullptr;
	const FMDBalBuild* WorstEHPThick = nullptr;
	for (const FMDBalBuild& Build : Builds)
	{
		MaxPointsSeen = FMath::Max(MaxPointsSeen, Build.Points);
		for (int32 Path = 1; Path <= 4; ++Path)
		{
			if (UMDPerkCatalog::PointsInPath(Build.Ranks, static_cast<EMDPerkPath>(Path)) > Caps.PathPointCap)
			{
				AddError(FString::Printf(TEXT("build exceeds the path cap: %s"), *BalDescribe(Build.Ranks)));
			}
		}
		if (Build.Perks.EffectiveHP < Baseline.EffectiveHP - 0.01f || Build.Perks.DPS < Baseline.DPS - 0.01f)
		{
			AddError(FString::Printf(TEXT("build is weaker than a bare character: %s"), *BalDescribe(Build.Ranks)));
		}
		if (!FMath::IsFinite(Build.Perks.EffectiveHP) || !FMath::IsFinite(Build.Perks.EffectiveHPBoss) || !FMath::IsFinite(Build.Perks.DPS))
		{
			AddError(FString::Printf(TEXT("non-finite metrics: %s"), *BalDescribe(Build.Ranks)));
		}

		if (!WorstEHP || Build.Perks.EffectiveHP > WorstEHP->Perks.EffectiveHP) { WorstEHP = &Build; }
		if (!WorstDPS || Build.Perks.DPS > WorstDPS->Perks.DPS) { WorstDPS = &Build; }
		if (!WorstUptime || Build.Perks.SprintUptime > WorstUptime->Perks.SprintUptime) { WorstUptime = &Build; }
		if (!WorstEHPGear || Build.Geared.EffectiveHP > WorstEHPGear->Geared.EffectiveHP) { WorstEHPGear = &Build; }
		if (!WorstEHPThick || Build.Perks.EffectiveHPWerewolf > WorstEHPThick->Perks.EffectiveHPWerewolf) { WorstEHPThick = &Build; }
		if (!WorstDPSGear || Build.Geared.DPS > WorstDPSGear->Geared.DPS) { WorstDPSGear = &Build; }
	}
	TestEqual(TEXT("the budget is spent in full by some build"), MaxPointsSeen, Ref.FullRunPoints);

	// BALANCE section 8 ceilings. Hard errors where the model is faithful to BALANCE.md; a ceiling that a legal build still
	// exceeds is a design finding, so it stays a warning (bHard = false) and prints the build.
	auto Ceiling = [this](const TCHAR* What, float Value, float Ceil, const FMDBalBuild* Build, bool bHard)
	{
		if (Build && Value > Ceil)
		{
			const FString Msg = FString::Printf(TEXT("BALANCE ceiling exceeded: %s %.3f > %.3f by build: %s"), What, Value, Ceil, *BalDescribe(Build->Ranks));
			if (bHard) { AddError(Msg); } else { AddWarning(Msg); }
		}
	};
	Ceiling(TEXT("effective HP, perks alone"), WorstEHP->Perks.EffectiveHP / Baseline.EffectiveHP, MDBalCeilEHPPerks, WorstEHP, true);
	Ceiling(TEXT("DPS, perks alone"), WorstDPS->Perks.DPS / Baseline.DPS, MDBalCeilDPSPerks, WorstDPS, true);
	Ceiling(TEXT("sprint uptime"), WorstUptime->Perks.SprintUptime, MDBalCeilSprintUptime, WorstUptime, true);
	Ceiling(TEXT("DPS, with upgrades and gear"), WorstDPSGear->Geared.DPS / Baseline.DPS, MDBalCeilDPSGear, WorstDPSGear, true);
	// Warning, not error: the gear alone (cloth set at level 3, 44.09% DR) is already 1.79x, so a Defense + Vitality build lands
	// above 2.0x. Robert decides whether to tune the perks, the upgrade DR or this ceiling.
	Ceiling(TEXT("effective HP, with upgrades and gear"), WorstEHPGear->Geared.EffectiveHP / Baseline.EffectiveHP, MDBalCeilEHPGear, WorstEHPGear, false);
	AddInfo(FString::Printf(TEXT("Balance worst cases: EHP perks %.3fx, EHP gear %.3fx (gear alone %.3fx), DPS perks %.3fx, DPS gear %.3fx, sprint uptime %.3f (baseline %.3f), Thick Hide EHP vs werewolf hit %.3fx"),
		WorstEHP->Perks.EffectiveHP / Baseline.EffectiveHP, WorstEHPGear->Geared.EffectiveHP / Baseline.EffectiveHP, BaselineGear.EffectiveHP / Baseline.EffectiveHP,
		WorstDPS->Perks.DPS / Baseline.DPS, WorstDPSGear->Geared.DPS / Baseline.DPS, WorstUptime->Perks.SprintUptime, Baseline.SprintUptime,
		WorstEHPThick ? WorstEHPThick->Perks.EffectiveHPWerewolf / Baseline.EffectiveHPWerewolf : 0.0f));

	// Saved/Balance/perk_builds.csv: one row per build.
	FString Csv = TEXT("points,paths");
	for (const FMDPerkNode& Node : BalTree().Nodes)
	{
		Csv += TEXT(",") + Node.Id.ToString();
	}
	Csv += TEXT(",ehp,ehp_werewolf_thickhide,dps,sprint_uptime,ttk_werewolf,ttk_boss,ehp_geared,dps_geared\n");
	for (const FMDBalBuild& Build : Builds)
	{
		Csv += FString::Printf(TEXT("%d,%d"), Build.Points, Build.PathsUsed);
		for (const uint8 Rank : Build.Ranks)
		{
			Csv += FString::Printf(TEXT(",%d"), static_cast<int32>(Rank));
		}
		Csv += FString::Printf(TEXT(",%.3f,%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f\n"), Build.Perks.EffectiveHP, Build.Perks.EffectiveHPWerewolf,
			Build.Perks.DPS, Build.Perks.SprintUptime, Build.Perks.TTKWerewolf, Build.Perks.TTKBoss, Build.Geared.EffectiveHP, Build.Geared.DPS);
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Balance"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	if (!FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Dir, TEXT("perk_builds.csv"))))
	{
		AddWarning(TEXT("could not write Saved/Balance/perk_builds.csv"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBalanceSprintUptimeTest, "MazeDemons.Balance.SprintUptimeFinite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBalanceSprintUptimeTest::RunTest(const FString& Parameters)
{
	// Reference cycle (BALANCE section 8): bare character sprints 100 / 15 = 6.67 s, recovers 1 + 100 / 20 = 6 s -> 52.6%.
	TestNearlyEqual(TEXT("baseline uptime ~53%"), BalMetrics(FMDPerkModifiers()).SprintUptime, 0.5263f, 0.001f);
	// BALANCE's worked check for max Movement: 150 stamina, drain 15 x 0.7, regen 20 x 1.45, delay 0.5 -> ~14.3 s vs ~5.2 s, ~72%.
	TestNearlyEqual(TEXT("max Movement worked check ~72%"), UMDBalanceModel::SprintUptime(150.0f, 29.0f, 0.5f, 10.5f, 1.0f), 0.716f, 0.005f);

	TestEqual(TEXT("no drain, no model"), UMDBalanceModel::SprintUptime(100.0f, 20.0f, 1.0f, 0.0f, 1.0f), 0.0f);
	TestEqual(TEXT("no regen, no model"), UMDBalanceModel::SprintUptime(100.0f, 0.0f, 1.0f, 15.0f, 1.0f), 0.0f);
	TestEqual(TEXT("no stamina, no model"), UMDBalanceModel::SprintUptime(0.0f, 20.0f, 1.0f, 15.0f, 1.0f), 0.0f);

	TArray<FMDBalBuild> Builds;
	BalCollect(Builds);
	for (const FMDBalBuild& Build : Builds)
	{
		const float Uptime = Build.Perks.SprintUptime;
		if (!FMath::IsFinite(Uptime) || Uptime <= 0.0f || Uptime >= 1.0f)
		{
			AddError(FString::Printf(TEXT("sprint uptime %.4f is not in (0, 1) for build: %s"), Uptime, *BalDescribe(Build.Ranks)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBalanceTTKTest, "MazeDemons.Balance.TTKFiniteVsReference", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBalanceTTKTest::RunTest(const FString& Parameters)
{
	const FMDBalanceReference Ref;
	const FMDBuildMetrics Baseline = BalMetrics(FMDPerkModifiers());
	TestNearlyEqual(TEXT("baseline werewolf TTK = 120 / 10"), Baseline.TTKWerewolf, 12.0f, 0.01f);
	TestNearlyEqual(TEXT("baseline boss TTK = 1500 / 10"), Baseline.TTKBoss, 150.0f, 0.01f);
	TestEqual(TEXT("no damage never kills"), UMDBalanceModel::TimeToKill(Ref.WerewolfHealth, 0.0f), MAX_FLT);

	TArray<FMDBalBuild> Builds;
	BalCollect(Builds);
	for (const FMDBalBuild& Build : Builds)
	{
		const FMDBuildMetrics& M = Build.Perks;
		const bool bOk = FMath::IsFinite(M.TTKWerewolf) && FMath::IsFinite(M.TTKBoss) && M.TTKWerewolf > 0.0f && M.TTKBoss > 0.0f
			&& M.TTKWerewolf < 1000.0f && M.TTKBoss < 10000.0f
			&& M.TTKWerewolf <= Baseline.TTKWerewolf + 0.01f && M.TTKBoss <= Baseline.TTKBoss + 0.01f;
		if (!bOk)
		{
			AddError(FString::Printf(TEXT("time to kill is not finite / not better than a bare character for build: %s"), *BalDescribe(Build.Ranks)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBalancePureVsMixedTest, "MazeDemons.Balance.PureBranchVsMixed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBalancePureVsMixedTest::RunTest(const FString& Parameters)
{
	TArray<FMDBalBuild> Builds;
	BalCollect(Builds);

	float BestPureEHP = 0.0f, BestMixedEHP = 0.0f;
	float BestPureDPS = 0.0f, BestMixedDPS = 0.0f;
	int32 PureCount = 0;
	int32 MixedCount = 0;
	for (const FMDBalBuild& Build : Builds)
	{
		if (Build.PathsUsed <= 1)
		{
			++PureCount;
			BestPureEHP = FMath::Max(BestPureEHP, Build.Perks.EffectiveHP);
			BestPureDPS = FMath::Max(BestPureDPS, Build.Perks.DPS);
		}
		else
		{
			++MixedCount;
			BestMixedEHP = FMath::Max(BestMixedEHP, Build.Perks.EffectiveHP);
			BestMixedDPS = FMath::Max(BestMixedDPS, Build.Perks.DPS);
		}
	}
	TestTrue(TEXT("there are pure-branch builds"), PureCount > 0);
	TestTrue(TEXT("there are mixed builds"), MixedCount > 0);

	// A pure branch is capped at 5 points, a mixed build spends all 8: the best mixed build is at least as good in each metric
	// (no pure branch dominates every mixed build).
	TestTrue(TEXT("best mixed effective HP >= best pure branch"), BestMixedEHP >= BestPureEHP - 0.01f);
	TestTrue(TEXT("best mixed DPS >= best pure branch"), BestMixedDPS >= BestPureDPS - 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBalanceGearScenarioTest, "MazeDemons.Balance.GearScenarioAndStalwart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDBalanceGearScenarioTest::RunTest(const FString& Parameters)
{
	// Cloth set 4 / 8 / 4 / 2 with +3 pp per level at level 3 -> 13 / 17 / 13 / 11: 1 - .87 * .83 * .87 * .89 = 44.09%; weapon x1.18.
	float DR = 0.0f, AP = 0.0f;
	UMDBalanceModel::GetGearScenario(FMDEconomyTuning(), DR, AP);
	TestNearlyEqual(TEXT("gear damage reduction"), DR, 44.09f, 0.05f);
	TestNearlyEqual(TEXT("gear attack power multiplier"), AP, 1.18f, 0.001f);

	FMDEconomyTuning NoUpgrades;
	NoUpgrades.MaxUpgradeLevel = 0;
	UMDBalanceModel::GetGearScenario(NoUpgrades, DR, AP);
	TestNearlyEqual(TEXT("level 0 cloth set"), DR, 16.91f, 0.01f);
	TestNearlyEqual(TEXT("level 0 weapon"), AP, 1.0f, 0.001f);

	// Stalwart: half the bar at 6 % DR, half at 16 % (6 + 10 combined): 100 * (0.5 / 0.94 + 0.5 / 0.846) = 112.2.
	const float Plain = UMDBalanceModel::EffectiveHP(100.0f, 6.0f, 0.0f, 0.0f, false);
	TestNearlyEqual(TEXT("plain effective HP = Max / (1 - DR)"), Plain, 100.0f / 0.94f, 0.01f);
	const float Weighted = UMDBalanceModel::EffectiveHPStalwart(100.0f, 6.0f, 15.4f, 0.5f, 0.0f, 0.0f, false);
	TestNearlyEqual(TEXT("Stalwart weighted effective HP"), Weighted, 100.0f * (0.5f / 0.94f + 0.5f / 0.846f), 0.05f);
	TestNearlyEqual(TEXT("no Stalwart gain = plain"), UMDBalanceModel::EffectiveHPStalwart(100.0f, 6.0f, 6.0f, 0.5f, 0.0f, 0.0f, false), Plain, 0.01f);

	// Bulwark only bites on a hit above 30% of max health: 30 damage on 100 HP is not capped, 60 is (60 -> 30 halves the damage).
	TestNearlyEqual(TEXT("bulwark does not touch a 30 hit"), UMDBalanceModel::EffectiveHP(100.0f, 0.0f, 30.0f, 0.0f, true), 100.0f, 0.01f);
	TestNearlyEqual(TEXT("bulwark caps a 60 hit"), UMDBalanceModel::EffectiveHP(100.0f, 0.0f, 60.0f, 0.0f, true), 200.0f, 0.01f);

	// The ceilinged measure ignores Thick Hide; the informational one does not (15 hit, 4 flat: 15 -> 11).
	FMDPerkModifiers Mods;
	Mods.FlatDamageReduction = 4.0f;
	const FMDBuildMetrics M = BalMetrics(Mods);
	TestNearlyEqual(TEXT("Thick Hide is not in the ceilinged EHP"), M.EffectiveHP, 100.0f, 0.01f);
	TestNearlyEqual(TEXT("Thick Hide vs the werewolf hit"), M.EffectiveHPWerewolf, 100.0f * 15.0f / 11.0f, 0.05f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
