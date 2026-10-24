#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDCombatStatics.h"
#include "MDGameplayStatics.h"
#include "MDPlayerController.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace MDCombatGateTestsLocal
{
	// TestEqual on a scoped enum has no string form; compare the underlying value.
	static int32 Value(EMDAttackBlock Block)
	{
		return static_cast<int32>(Block);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAttackGateTest, "MazeDemons.Combat.AttackGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAttackGateTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatGateTestsLocal;
	const int32 None = Value(EMDAttackBlock::None);

	// bHasPawn, bDead, bLocal, bMenuOpen, bHasCombat
	TestEqual(TEXT("all clear is allowed"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, true)), None);
	TestEqual(TEXT("no pawn"), Value(UMDGameplayStatics::EvaluateAttackGate(false, false, true, false, true)), Value(EMDAttackBlock::NoPawn));
	TestEqual(TEXT("dead"), Value(UMDGameplayStatics::EvaluateAttackGate(true, true, true, false, true)), Value(EMDAttackBlock::Dead));
	TestEqual(TEXT("local with a menu open is blocked"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, true, true, true)), Value(EMDAttackBlock::MenuOpen));
	TestEqual(TEXT("remote pawn ignores the menu flag"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, false, true, true)), None);
	TestEqual(TEXT("remote without a menu is allowed"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, false, false, true)), None);
	TestEqual(TEXT("no combat component"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, false)), Value(EMDAttackBlock::NoCombatComponent));
	TestEqual(TEXT("remote without combat component"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, false, false, false)), Value(EMDAttackBlock::NoCombatComponent));

	// Precedence: no pawn beats everything, dead beats menu, menu beats missing combat.
	TestEqual(TEXT("no pawn wins"), Value(UMDGameplayStatics::EvaluateAttackGate(false, true, true, true, false)), Value(EMDAttackBlock::NoPawn));
	TestEqual(TEXT("dead beats menu"), Value(UMDGameplayStatics::EvaluateAttackGate(true, true, true, true, true)), Value(EMDAttackBlock::Dead));
	TestEqual(TEXT("menu beats missing combat"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, true, true, false)), Value(EMDAttackBlock::MenuOpen));
	TestEqual(TEXT("dead remote pawn is blocked"), Value(UMDGameplayStatics::EvaluateAttackGate(true, true, false, false, true)), Value(EMDAttackBlock::Dead));

	// The pawn-based entry point reports a missing pawn.
	EMDAttackBlock Reason = EMDAttackBlock::None;
	TestFalse(TEXT("null pawn cannot attack"), UMDGameplayStatics::CanPawnAttack(nullptr, Reason));
	TestEqual(TEXT("null pawn reason"), Value(Reason), Value(EMDAttackBlock::NoPawn));
	return true;
}

// Dead pawns are refused before any menu or combat-component check, on the server and the owning client alike. The gate is
// pure, so this covers the CanActivateAbility decision without a world; the full GA_PlayerMelee path is a 2P check (#26).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMeleeBlockedWhenDeadTest, "MazeDemons.Combat.MeleeBlockedWhenDead", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMeleeBlockedWhenDeadTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatGateTestsLocal;
	for (int32 Mask = 0; Mask < 8; ++Mask)
	{
		const bool bLocal = (Mask & 1) != 0;
		const bool bMenu = (Mask & 2) != 0;
		const bool bCombat = (Mask & 4) != 0;
		TestEqual(*FString::Printf(TEXT("dead is always blocked (local %d, menu %d, combat %d)"), bLocal ? 1 : 0, bMenu ? 1 : 0, bCombat ? 1 : 0),
			Value(UMDGameplayStatics::EvaluateAttackGate(true, true, bLocal, bMenu, bCombat)), Value(EMDAttackBlock::Dead));
	}
	TestEqual(TEXT("the same pawn alive is allowed"), Value(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, true)), Value(EMDAttackBlock::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAnyMenuOpenTest, "MazeDemons.UI.AnyMenuOpen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAnyMenuOpenTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAnyMenuTestWorld")));
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	AMDPlayerController* PC = World->SpawnActor<AMDPlayerController>();
	if (TestNotNull(TEXT("player controller"), PC))
	{
		TestFalse(TEXT("no menu by default"), PC->IsAnyMenuOpen());
		PC->bShowMouseCursor = true;
		TestTrue(TEXT("a cursor-showing widget counts as a menu"), PC->IsAnyMenuOpen());
		PC->bShowMouseCursor = false;
		TestFalse(TEXT("closed again"), PC->IsAnyMenuOpen());
		TestFalse(TEXT("game menu closed"), PC->IsGameMenuOpen());
		TestFalse(TEXT("pause menu closed"), PC->IsPauseMenuOpen());
	}

	World->DestroyWorld(false);
	if (World->IsRooted())
	{
		World->RemoveFromRoot();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDeltaTextRoundingTest, "MazeDemons.Stats.DeltaTextRounding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDeltaTextRoundingTest::RunTest(const FString& Parameters)
{
	const FString Minus = FString(TEXT("−"));

	TestEqual(TEXT("percent gain"), UMDGameplayStatics::FormatDeltaText(10.0f, 11.7f, true).ToString(), FString(TEXT("+1.7%")));
	TestEqual(TEXT("percent loss"), UMDGameplayStatics::FormatDeltaText(11.7f, 10.0f, true).ToString(), Minus + TEXT("1.7%"));
	TestEqual(TEXT("whole gain"), UMDGameplayStatics::FormatDeltaText(10.0f, 15.0f, false).ToString(), FString(TEXT("+5")));
	TestEqual(TEXT("whole loss"), UMDGameplayStatics::FormatDeltaText(10.0f, 8.0f, false).ToString(), Minus + TEXT("2"));

	// Equal as shown: empty, even when the raw values differ slightly.
	TestTrue(TEXT("identical is empty"), UMDGameplayStatics::FormatDeltaText(12.0f, 12.0f, false).IsEmpty());
	TestTrue(TEXT("sub-display whole difference is empty"), UMDGameplayStatics::FormatDeltaText(5.1f, 5.3f, false).IsEmpty());
	TestTrue(TEXT("sub-display percent difference is empty"), UMDGameplayStatics::FormatDeltaText(16.91f, 16.93f, true).IsEmpty());

	// Rounding follows the shown values, not the raw difference: 5.4 shows 5 and 5.6 shows 6.
	TestEqual(TEXT("rounding crosses a whole number"), UMDGameplayStatics::FormatDeltaText(5.4f, 5.6f, false).ToString(), FString(TEXT("+1")));
	TestEqual(TEXT("percent rounds to a tenth"), UMDGameplayStatics::FormatDeltaText(0.0f, 0.26f, true).ToString(), FString(TEXT("+0.3%")));
	TestTrue(TEXT("no negative zero"), !UMDGameplayStatics::FormatDeltaText(3.0f, 2.97f, true).ToString().Contains(TEXT("0.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDTraceHandsTest, "MazeDemons.Combat.TraceHands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDTraceHandsTest::RunTest(const FString& Parameters)
{
	const FName MainHand(TEXT("MainHand"));
	const FName OffHand(TEXT("OffHand"));
	const FName TwoHanded(TEXT("TwoHanded"));

	const TArray<FName> Main = UMDCombatStatics::TraceSocketsForHand(EMDWeaponHand::Main);
	TestEqual(TEXT("main: two sockets"), Main.Num(), 2);
	TestTrue(TEXT("main: MainHand"), Main.Contains(MainHand));
	TestTrue(TEXT("main: TwoHanded"), Main.Contains(TwoHanded));
	TestFalse(TEXT("main: not OffHand"), Main.Contains(OffHand));

	const TArray<FName> Off = UMDCombatStatics::TraceSocketsForHand(EMDWeaponHand::Off);
	TestEqual(TEXT("off: one socket"), Off.Num(), 1);
	TestTrue(TEXT("off: OffHand"), Off.Contains(OffHand));

	const TArray<FName> Both = UMDCombatStatics::TraceSocketsForHand(EMDWeaponHand::Both);
	TestEqual(TEXT("both: three sockets"), Both.Num(), 3);
	TestTrue(TEXT("both: all present"), Both.Contains(MainHand) && Both.Contains(OffHand) && Both.Contains(TwoHanded));

	// Resolution rule: (override, hasCombo, stepHand, dualPairAttached, setDualWield).
	using EOv = EMDTraceHandOverride;
	using EH = EMDWeaponHand;
	auto Resolve = [](EOv Ov, bool bCombo, EH Step, bool bPair, bool bSet) { return static_cast<int32>(UMDCombatStatics::ResolveTraceHand(Ov, bCombo, Step, bPair, bSet)); };
	TestEqual(TEXT("override Main wins over an Off step"), Resolve(EOv::Main, true, EH::Off, true, true), static_cast<int32>(EH::Main));
	TestEqual(TEXT("override Off wins over a Main step on a dual pair"), Resolve(EOv::Off, true, EH::Main, true, true), static_cast<int32>(EH::Off));
	TestEqual(TEXT("override Both wins with no combo"), Resolve(EOv::Both, false, EH::Main, false, false), static_cast<int32>(EH::Both));
	TestEqual(TEXT("auto: Main step + dual pair + dual set -> Both"), Resolve(EOv::Auto, true, EH::Main, true, true), static_cast<int32>(EH::Both));
	TestEqual(TEXT("auto: Main step, dual pair but set not dual -> Main"), Resolve(EOv::Auto, true, EH::Main, true, false), static_cast<int32>(EH::Main));
	TestEqual(TEXT("auto: Main step, dual set but only one weapon -> Main"), Resolve(EOv::Auto, true, EH::Main, false, true), static_cast<int32>(EH::Main));
	TestEqual(TEXT("auto: Off step -> Off"), Resolve(EOv::Auto, true, EH::Off, true, true), static_cast<int32>(EH::Off));
	TestEqual(TEXT("auto: Both step -> Both"), Resolve(EOv::Auto, true, EH::Both, true, true), static_cast<int32>(EH::Both));
	TestEqual(TEXT("auto: no combo component -> Both (all found weapons)"), Resolve(EOv::Auto, false, EH::Main, false, false), static_cast<int32>(EH::Both));
	const EH TwoHander = UMDCombatStatics::ResolveTraceHand(EOv::Auto, true, EH::Main, false, false);
	TestTrue(TEXT("two-hander: resolves to Main, whose sockets include TwoHanded"), TwoHander == EH::Main && UMDCombatStatics::TraceSocketsForHand(TwoHander).Contains(TwoHanded));
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
