#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDGameplayTags.h"
#include "MDPerkCatalog.h"
#include "MDQuestTypes.h"

#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"

// Pure quest rules (UMDQuestRules); no world, no game state.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestApplyProgressClampsTest, "MazeDemons.Quest.ApplyProgressClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestApplyProgressClampsTest::RunTest(const FString& Parameters)
{
	FMDQuestProgress Quest;
	Quest.ZoneIndex = 2;
	Quest.State = EMDQuestState::Active;
	Quest.Required = 6;

	TestFalse(TEXT("zero delta is ignored"), UMDQuestRules::ApplyProgress(Quest, 0));
	TestFalse(TEXT("negative delta is ignored"), UMDQuestRules::ApplyProgress(Quest, -3));
	TestEqual(TEXT("count unchanged by ignored deltas"), Quest.Count, 0);

	TestTrue(TEXT("positive delta applies"), UMDQuestRules::ApplyProgress(Quest, 2));
	TestEqual(TEXT("count 2"), Quest.Count, 2);
	TestTrue(TEXT("state stays Active below Required"), Quest.State == EMDQuestState::Active);

	TestTrue(TEXT("overshoot applies"), UMDQuestRules::ApplyProgress(Quest, 100));
	TestEqual(TEXT("count clamps to Required"), Quest.Count, 6);

	FMDQuestProgress Locked;
	Locked.State = EMDQuestState::Locked;
	Locked.Required = 3;
	TestFalse(TEXT("a Locked quest ignores progress"), UMDQuestRules::ApplyProgress(Locked, 1));
	TestEqual(TEXT("Locked count stays 0"), Locked.Count, 0);

	FMDQuestProgress Done = Quest;
	TestFalse(TEXT("a Completed quest ignores progress"), UMDQuestRules::ApplyProgress(Done, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestCompletesAtRequiredTest, "MazeDemons.Quest.CompletesAtRequired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestCompletesAtRequiredTest::RunTest(const FString& Parameters)
{
	FMDQuestProgress Quest;
	Quest.State = EMDQuestState::Active;
	Quest.Required = 3;

	UMDQuestRules::ApplyProgress(Quest, 1);
	UMDQuestRules::ApplyProgress(Quest, 1);
	TestTrue(TEXT("2 of 3 is still Active"), Quest.State == EMDQuestState::Active);
	UMDQuestRules::ApplyProgress(Quest, 1);
	TestTrue(TEXT("3 of 3 completes"), Quest.State == EMDQuestState::Completed);
	TestEqual(TEXT("count equals Required"), Quest.Count, 3);

	FMDQuestProgress Single;
	Single.State = EMDQuestState::Active;
	Single.Required = 1;
	TestTrue(TEXT("Required 1 completes on the first delta"), UMDQuestRules::ApplyProgress(Single, 1) && Single.State == EMDQuestState::Completed);

	FMDQuestProgress Bad;
	Bad.State = EMDQuestState::Active;
	Bad.Required = 0; // treated as 1
	UMDQuestRules::ApplyProgress(Bad, 1);
	TestTrue(TEXT("Required 0 behaves like 1"), Bad.State == EMDQuestState::Completed && Bad.Count == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestUnlockNextInOrderTest, "MazeDemons.Quest.UnlockNextInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestUnlockNextInOrderTest::RunTest(const FString& Parameters)
{
	TArray<FMDQuestProgress> Quests;
	for (int32 Zone = 1; Zone <= 4; ++Zone)
	{
		FMDQuestProgress Quest;
		Quest.ZoneIndex = Zone;
		Quest.State = EMDQuestState::Locked;
		Quests.Add(Quest);
	}
	TestTrue(TEXT("with nothing Active the first Locked quest unlocks"), UMDQuestRules::UnlockNext(Quests));
	TestTrue(TEXT("zone 1 Active"), Quests[0].State == EMDQuestState::Active);
	TestTrue(TEXT("zone 2 still Locked"), Quests[1].State == EMDQuestState::Locked);

	TestFalse(TEXT("an Active quest blocks unlocking"), UMDQuestRules::UnlockNext(Quests));
	TestTrue(TEXT("zone 2 still Locked while zone 1 is Active"), Quests[1].State == EMDQuestState::Locked);

	Quests[0].State = EMDQuestState::Completed;
	TestTrue(TEXT("zone 1 done unlocks zone 2"), UMDQuestRules::UnlockNext(Quests));
	TestTrue(TEXT("zone 2 Active"), Quests[1].State == EMDQuestState::Active);
	TestTrue(TEXT("zone 3 still Locked"), Quests[2].State == EMDQuestState::Locked);

	Quests[1].State = EMDQuestState::Completed;
	Quests[2].State = EMDQuestState::Completed;
	Quests[3].State = EMDQuestState::Completed;
	TestFalse(TEXT("nothing left to unlock"), UMDQuestRules::UnlockNext(Quests));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestCountZonesCompletedTest, "MazeDemons.Quest.CountZonesCompleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestCountZonesCompletedTest::RunTest(const FString& Parameters)
{
	TArray<FMDQuestProgress> Quests;
	Quests.SetNum(4);
	TestEqual(TEXT("none completed"), UMDQuestRules::CountZonesCompleted(Quests), 0);

	int32 LastPoints = UMDPerkCatalog::ComputePointsEarned(0, 0, 0);
	for (int32 Done = 1; Done <= 4; ++Done)
	{
		Quests[Done - 1].State = EMDQuestState::Completed;
		const int32 Count = UMDQuestRules::CountZonesCompleted(Quests);
		TestEqual(*FString::Printf(TEXT("%d completed"), Done), Count, Done);
		const int32 Points = UMDPerkCatalog::ComputePointsEarned(Count, 0, 0);
		TestTrue(*FString::Printf(TEXT("perk points never fall as zones complete (%d)"), Done), Points >= LastPoints);
		LastPoints = Points;
	}

	TArray<FMDQuestProgress> TooMany;
	TooMany.SetNum(6);
	for (FMDQuestProgress& Quest : TooMany)
	{
		Quest.State = EMDQuestState::Completed;
	}
	TestEqual(TEXT("count is clamped to 4"), UMDQuestRules::CountZonesCompleted(TooMany), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestMatchesObjectiveByTagTest, "MazeDemons.Quest.MatchesObjectiveByTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestMatchesObjectiveByTagTest::RunTest(const FString& Parameters)
{
	FMDObjectiveDef Def;
	Def.Type = EMDObjectiveType::Kill;
	Def.TargetTag = TAG_Enemy_Ghoul;

	FGameplayTagContainer Ghoul;
	Ghoul.AddTag(TAG_Enemy_Ghoul);
	FGameplayTagContainer Other;
	Other.AddTag(TAG_Objective_Zone3_Key);

	TestTrue(TEXT("same type and tag match"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Kill, Ghoul));
	TestFalse(TEXT("wrong type does not match"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Collect, Ghoul));
	TestFalse(TEXT("wrong tag does not match"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Kill, Other));
	TestFalse(TEXT("no tags do not match a tagged objective"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Kill, FGameplayTagContainer()));

	FMDObjectiveDef AnyTag;
	AnyTag.Type = EMDObjectiveType::Kill;
	TestTrue(TEXT("an empty TargetTag matches any event of the type"), UMDQuestRules::MatchesObjective(AnyTag, EMDObjectiveType::Kill, Other));
	TestTrue(TEXT("an empty TargetTag matches an event without tags"), UMDQuestRules::MatchesObjective(AnyTag, EMDObjectiveType::Kill, FGameplayTagContainer()));
	TestFalse(TEXT("an empty TargetTag still needs the type"), UMDQuestRules::MatchesObjective(AnyTag, EMDObjectiveType::Reach, Other));

	// Hierarchy: an event carrying a child tag satisfies a parent TargetTag, not the other way round.
	const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(FName(TEXT("Enemy")), false);
	if (TestTrue(TEXT("parent tag Enemy resolves"), Parent.IsValid()))
	{
		FMDObjectiveDef ParentDef;
		ParentDef.Type = EMDObjectiveType::Kill;
		ParentDef.TargetTag = Parent;
		TestTrue(TEXT("parent TargetTag matches a child event tag"), UMDQuestRules::MatchesObjective(ParentDef, EMDObjectiveType::Kill, Ghoul));

		FGameplayTagContainer ParentOnly;
		ParentOnly.AddTag(Parent);
		TestFalse(TEXT("child TargetTag does not match a parent event tag"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Kill, ParentOnly));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestFormatObjectiveTest, "MazeDemons.Quest.FormatObjective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestFormatObjectiveTest::RunTest(const FString& Parameters)
{
	FMDObjectiveDef Def;
	Def.Text = FText::FromString(TEXT("Slay ghouls {0}/{1}"));
	FMDQuestProgress Progress;
	Progress.Count = 3;
	Progress.Required = 6;
	TestEqual(TEXT("placeholders are filled"), UMDQuestRules::FormatObjective(Def, Progress).ToString(), FString(TEXT("Slay ghouls 3/6")));

	FMDObjectiveDef Plain;
	Plain.Text = FText::FromString(TEXT("Find the shrine"));
	TestEqual(TEXT("text without placeholders is unchanged"), UMDQuestRules::FormatObjective(Plain, Progress).ToString(), FString(TEXT("Find the shrine")));

	FMDObjectiveDef Empty;
	TestEqual(TEXT("empty text falls back to count/required"), UMDQuestRules::FormatObjective(Empty, Progress).ToString(), FString(TEXT("3/6")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestSurviveTypeAcceptedTest, "MazeDemons.Quest.SurviveTypeAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestSurviveTypeAcceptedTest::RunTest(const FString& Parameters)
{
	FMDObjectiveDef Def;
	Def.Type = EMDObjectiveType::Survive;
	Def.SurviveSeconds = 30.0f;
	Def.Required = 30;

	TestTrue(TEXT("a Survive event matches a Survive objective"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Survive, FGameplayTagContainer()));
	TestFalse(TEXT("a Kill event does not match a Survive objective"), UMDQuestRules::MatchesObjective(Def, EMDObjectiveType::Kill, FGameplayTagContainer()));

	FMDQuestProgress Quest;
	Quest.State = EMDQuestState::Active;
	Quest.Required = Def.Required;
	TestTrue(TEXT("Survive progress applies like any other"), UMDQuestRules::ApplyProgress(Quest, 10));
	TestEqual(TEXT("count 10"), Quest.Count, 10);
	TestEqual(TEXT("enum values are stable"), static_cast<int32>(EMDObjectiveType::Reach), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestProHUDTaskStateMapTest, "MazeDemons.Quest.ProHUDTaskStateMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestProHUDTaskStateMapTest::RunTest(const FString& Parameters)
{
	const uint8 Locked = UMDQuestRules::ToProHUDTaskState(EMDQuestState::Locked);
	const uint8 Active = UMDQuestRules::ToProHUDTaskState(EMDQuestState::Active);
	const uint8 Completed = UMDQuestRules::ToProHUDTaskState(EMDQuestState::Completed);
	TestEqual(TEXT("Locked index"), static_cast<int32>(Locked), 0);
	TestEqual(TEXT("Active index"), static_cast<int32>(Active), 1);
	TestEqual(TEXT("Completed index"), static_cast<int32>(Completed), 2);
	TestTrue(TEXT("the three states map to distinct indices"), Locked != Active && Active != Completed && Locked != Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestKillCountsOnlyInZoneTest, "MazeDemons.Quest.KillCountsOnlyInZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDQuestKillCountsOnlyInZoneTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("victim in the quest zone counts"), UMDQuestRules::KillCountsForZone(2, 2));
	TestFalse(TEXT("victim in another zone does not count"), UMDQuestRules::KillCountsForZone(1, 2));
	TestFalse(TEXT("victim outside every zone (0) does not count"), UMDQuestRules::KillCountsForZone(0, 2));
	TestFalse(TEXT("an invalid victim zone does not count"), UMDQuestRules::KillCountsForZone(-1, 2));
	TestFalse(TEXT("no zone on either side does not count"), UMDQuestRules::KillCountsForZone(0, 0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
