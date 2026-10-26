#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDBalanceModel.h"
#include "MDCombatRules.h"
#include "MDCombatTypes.h"
#include "MDComboSet.h"
#include "MDGameplayStatics.h"
#include "MDPerkTypes.h"
#include "MDPoiseProfile.h"

#include "Animation/AnimMontage.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

// C1 combat feel: pure rules and data checks (no world, no actors).

namespace MDCombatTestsLocal
{
	// TestEqual on a scoped enum has no string form; compare the underlying value.
	template <typename E>
	int32 V(E Value)
	{
		return static_cast<int32>(Value);
	}

	// FVector components and Size() are doubles in UE5; the float TestEqual overload needs floats.
	float F(double Value)
	{
		return static_cast<float>(Value);
	}

	FMDComboTuning Tuning3()
	{
		FMDComboTuning Tuning;
		Tuning.LightCount = 3;
		Tuning.InputBufferTime = 0.35f;
		Tuning.ComboResetTime = 0.8f;
		Tuning.ChargeTime = 0.6f;
		Tuning.DodgeAttackWindow = 0.6f;
		return Tuning;
	}

	EMDComboAction Press(FMDComboState& State, EMDComboInput Input, double Now, float Hold = 0.0f)
	{
		UMDCombatRules::BufferInput(State, Input, Now, Hold);
		return UMDCombatRules::Advance(State, Tuning3(), EMDComboEvent::Poll, Now);
	}

	EMDComboAction Poll(FMDComboState& State, double Now)
	{
		return UMDCombatRules::Advance(State, Tuning3(), EMDComboEvent::Poll, Now);
	}

	FMDReactionInput Reaction(EMDHitReaction Step, float Poise, float PoiseDamage)
	{
		FMDReactionInput In;
		In.StepReaction = Step;
		In.Poise = Poise;
		In.MaxPoise = 50.0f;
		In.PoiseDamage = PoiseDamage;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDComboChainTest, "MazeDemons.Combat.ComboChain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDComboChainTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState State;

	TestEqual(TEXT("first press plays step 0"), V(Press(State, EMDComboInput::Light, 0.0)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("step index"), State.LightStep, 0);
	TestTrue(TEXT("attacking"), State.bAttacking);

	// Mashing outside the window never skips a step: the press waits in the buffer.
	TestEqual(TEXT("press mid-swing waits"), V(Press(State, EMDComboInput::Light, 0.1)), V(EMDComboAction::None));
	TestEqual(TEXT("still step 0"), State.LightStep, 0);
	State.bInComboWindow = true;
	TestEqual(TEXT("window opens: buffered press chains"), V(Poll(State, 0.2)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("step 1"), State.LightStep, 1);
	TestFalse(TEXT("window closes with the new step"), State.bInComboWindow);

	State.bInComboWindow = true;
	TestEqual(TEXT("step 2"), V(Press(State, EMDComboInput::Light, 0.5)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("step index 2"), State.LightStep, 2);

	State.bInComboWindow = true;
	TestEqual(TEXT("wraps after the last step"), V(Press(State, EMDComboInput::Light, 0.8)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("back to step 0"), State.LightStep, 0);
	TestEqual(TEXT("nothing left in the buffer"), V(Poll(State, 0.81)), V(EMDComboAction::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDComboBufferExpiryTest, "MazeDemons.Combat.ComboBufferExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDComboBufferExpiryTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState State;
	Press(State, EMDComboInput::Light, 0.0);

	// A press 0.1 s before the window opens is kept; one older than the buffer time is dropped.
	TestEqual(TEXT("early press buffered"), V(Press(State, EMDComboInput::Light, 0.1)), V(EMDComboAction::None));
	TestTrue(TEXT("buffer holds it"), State.Buffered == EMDComboInput::Light);
	State.bInComboWindow = true;
	TestEqual(TEXT("expired buffer is dropped, not played"), V(Poll(State, 0.5)), V(EMDComboAction::None));
	TestTrue(TEXT("buffer cleared"), State.Buffered == EMDComboInput::None);
	TestEqual(TEXT("still step 0"), State.LightStep, 0);

	// A press just inside the buffer time is still valid.
	State.bInComboWindow = false;
	TestEqual(TEXT("press while the window is closed waits"), V(Press(State, EMDComboInput::Light, 1.0)), V(EMDComboAction::None));
	State.bInComboWindow = true;
	TestEqual(TEXT("window opens 0.3 s later: still buffered"), V(Poll(State, 1.3)), V(EMDComboAction::PlayLight));

	// None never enters the buffer.
	UMDCombatRules::BufferInput(State, EMDComboInput::None, 2.0);
	TestTrue(TEXT("None ignored"), State.Buffered == EMDComboInput::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDComboResetTest, "MazeDemons.Combat.ComboReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDComboResetTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState State;
	Press(State, EMDComboInput::Light, 0.0);
	UMDCombatRules::Advance(State, Tuning3(), EMDComboEvent::AttackEnded, 0.4);
	TestFalse(TEXT("idle after the swing"), State.bAttacking);

	TestEqual(TEXT("press 0.6 s after the swing continues the chain"), V(Press(State, EMDComboInput::Light, 1.0)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("step 1"), State.LightStep, 1);
	UMDCombatRules::Advance(State, Tuning3(), EMDComboEvent::AttackEnded, 1.4);

	TestEqual(TEXT("press 0.9 s after the swing starts over"), V(Press(State, EMDComboInput::Light, 2.3)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("back to step 0"), State.LightStep, 0);

	FMDComboState Fresh;
	TestEqual(TEXT("a fresh state starts at step 0"), V(Press(Fresh, EMDComboInput::Light, 100.0)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("fresh step"), Fresh.LightStep, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDodgeCancelWindowTest, "MazeDemons.Combat.DodgeCancelWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDodgeCancelWindowTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState State;
	Press(State, EMDComboInput::Light, 0.0);

	TestEqual(TEXT("dodge before the cancel window waits"), V(Press(State, EMDComboInput::Dodge, 0.1)), V(EMDComboAction::None));
	TestTrue(TEXT("still attacking"), State.bAttacking);
	State.bInCancelWindow = true;
	TestEqual(TEXT("dodge inside the cancel window fires"), V(Poll(State, 0.2)), V(EMDComboAction::Dodge));
	TestFalse(TEXT("the swing is over"), State.bAttacking);
	TestEqual(TEXT("the chain is over"), State.LightStep, -1);

	// A dodge while idle is always allowed.
	FMDComboState Idle;
	TestEqual(TEXT("idle dodge"), V(Press(Idle, EMDComboInput::Dodge, 5.0)), V(EMDComboAction::Dodge));

	// Light right after a dodge is the dodge attack; later it is a normal first step.
	TestEqual(TEXT("light 0.3 s after a dodge"), V(Press(Idle, EMDComboInput::Light, 5.3)), V(EMDComboAction::PlayDodgeAttack));
	UMDCombatRules::ApplyDodge(Idle, 10.0);
	TestEqual(TEXT("light 1 s after a dodge"), V(Press(Idle, EMDComboInput::Light, 11.0)), V(EMDComboAction::PlayLight));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHeavyVsChargedTest, "MazeDemons.Combat.HeavyVsCharged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDHeavyVsChargedTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState Tap;
	TestEqual(TEXT("tap is a heavy"), V(Press(Tap, EMDComboInput::Heavy, 0.0, 0.2f)), V(EMDComboAction::PlayHeavy));
	TestTrue(TEXT("heavy kind"), Tap.ActiveKind == EMDAttackKind::Heavy);

	FMDComboState Almost;
	TestEqual(TEXT("just under the charge time is a heavy"), V(Press(Almost, EMDComboInput::Heavy, 0.0, 0.59f)), V(EMDComboAction::PlayHeavy));

	FMDComboState Held;
	TestEqual(TEXT("held for the charge time is charged"), V(Press(Held, EMDComboInput::Heavy, 0.0, 0.6f)), V(EMDComboAction::PlayCharged));
	TestTrue(TEXT("charged kind"), Held.ActiveKind == EMDAttackKind::Charged);

	// A heavy ends the light chain and needs idle or the combo window.
	FMDComboState Chain;
	Press(Chain, EMDComboInput::Light, 0.0);
	TestEqual(TEXT("heavy mid-swing waits"), V(Press(Chain, EMDComboInput::Heavy, 0.1, 0.1f)), V(EMDComboAction::None));
	Chain.bInComboWindow = true;
	TestEqual(TEXT("heavy in the combo window"), V(Poll(Chain, 0.2)), V(EMDComboAction::PlayHeavy));
	TestEqual(TEXT("the light chain restarts after a heavy"), Chain.LightStep, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaggerResetsComboTest, "MazeDemons.Combat.StaggerResetsCombo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaggerResetsComboTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	FMDComboState State;
	Press(State, EMDComboInput::Light, 0.0);
	State.bInComboWindow = true;
	Press(State, EMDComboInput::Light, 0.2);
	TestEqual(TEXT("on step 1"), State.LightStep, 1);
	UMDCombatRules::BufferInput(State, EMDComboInput::Light, 0.3);

	TestEqual(TEXT("interrupt reports a reset"), V(UMDCombatRules::Advance(State, Tuning3(), EMDComboEvent::Interrupted, 0.31)), V(EMDComboAction::Reset));
	TestEqual(TEXT("chain cleared"), State.LightStep, -1);
	TestFalse(TEXT("not attacking"), State.bAttacking);
	TestFalse(TEXT("windows cleared"), State.bInComboWindow || State.bInCancelWindow);
	TestTrue(TEXT("buffer cleared"), State.Buffered == EMDComboInput::None);

	// Even right after the stagger (inside the reset time) the combo starts at step 0.
	TestEqual(TEXT("next light plays"), V(Press(State, EMDComboInput::Light, 0.4)), V(EMDComboAction::PlayLight));
	TestEqual(TEXT("from step 0"), State.LightStep, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPoiseRegenTest, "MazeDemons.Combat.PoiseRegen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDPoiseRegenTest::RunTest(const FString& Parameters)
{
	FMDPoiseTickInput In;
	In.Poise = 10.0f;
	In.MaxPoise = 50.0f;
	In.RegenDelay = 2.0f;
	In.RegenPerSec = 25.0f;

	In.TimeSinceHit = 0.0f;
	In.DeltaTime = 1.0f;
	TestEqual(TEXT("no regen inside the delay"), UMDCombatRules::TickPoise(In), 10.0f, 0.001f);

	In.TimeSinceHit = 1.5f;
	TestEqual(TEXT("only the part after the delay regenerates"), UMDCombatRules::TickPoise(In), 22.5f, 0.001f);

	In.TimeSinceHit = 3.0f;
	TestEqual(TEXT("full rate after the delay"), UMDCombatRules::TickPoise(In), 35.0f, 0.001f);

	In.Poise = 45.0f;
	TestEqual(TEXT("clamped to max"), UMDCombatRules::TickPoise(In), 50.0f, 0.001f);

	In.Poise = 10.0f;
	In.RegenPerSec = 0.0f;
	TestEqual(TEXT("zero rate never regenerates"), UMDCombatRules::TickPoise(In), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDReactionResolveTest, "MazeDemons.Combat.ReactionResolve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDReactionResolveTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;

	// A light hit with poise left flinches and drains.
	FMDReactionResult R = UMDCombatRules::ResolveReaction(Reaction(EMDHitReaction::Flinch, 50.0f, 7.0f));
	TestEqual(TEXT("light hit flinches"), V(R.Reaction), V(EMDHitReaction::Flinch));
	TestEqual(TEXT("poise drains"), R.PoiseAfter, 43.0f, 0.001f);
	TestFalse(TEXT("no stun"), R.bStartStun);

	// Poise reaching 0 staggers, stuns and resets.
	R = UMDCombatRules::ResolveReaction(Reaction(EMDHitReaction::Flinch, 5.0f, 7.0f));
	TestEqual(TEXT("poise break staggers"), V(R.Reaction), V(EMDHitReaction::Stagger));
	TestTrue(TEXT("stun starts"), R.bStartStun);
	TestEqual(TEXT("poise resets to max"), R.PoiseAfter, 50.0f, 0.001f);

	// A heavy step authored as Stagger staggers with poise to spare; a Knockback step stays a knockback.
	R = UMDCombatRules::ResolveReaction(Reaction(EMDHitReaction::Stagger, 50.0f, 40.0f));
	TestEqual(TEXT("heavy staggers"), V(R.Reaction), V(EMDHitReaction::Stagger));
	TestTrue(TEXT("heavy stuns"), R.bStartStun);
	R = UMDCombatRules::ResolveReaction(Reaction(EMDHitReaction::Knockback, 5.0f, 40.0f));
	TestEqual(TEXT("knockback step keeps its strength on a break"), V(R.Reaction), V(EMDHitReaction::Knockback));

	// Hyper-armor: flinch at most, poise still drains, no stun even at 0.
	FMDReactionInput Armor = Reaction(EMDHitReaction::Stagger, 5.0f, 40.0f);
	Armor.bHyperArmor = true;
	R = UMDCombatRules::ResolveReaction(Armor);
	TestEqual(TEXT("hyper-armor flinches"), V(R.Reaction), V(EMDHitReaction::Flinch));
	TestFalse(TEXT("hyper-armor never stuns"), R.bStartStun);
	TestEqual(TEXT("poise still drains"), R.PoiseAfter, 0.0f, 0.001f);

	// Immunity window and an already stunned target only flinch.
	FMDReactionInput Immune = Reaction(EMDHitReaction::Stagger, 5.0f, 40.0f);
	Immune.bImmune = true;
	R = UMDCombatRules::ResolveReaction(Immune);
	TestEqual(TEXT("immune target flinches"), V(R.Reaction), V(EMDHitReaction::Flinch));
	TestFalse(TEXT("no second stun"), R.bStartStun);
	FMDReactionInput Stunned = Reaction(EMDHitReaction::Knockback, 50.0f, 7.0f);
	Stunned.bStunned = true;
	R = UMDCombatRules::ResolveReaction(Stunned);
	TestEqual(TEXT("stunned target flinches"), V(R.Reaction), V(EMDHitReaction::Flinch));

	// Dead targets do nothing.
	FMDReactionInput Dead = Reaction(EMDHitReaction::Stagger, 50.0f, 40.0f);
	Dead.bDead = true;
	R = UMDCombatRules::ResolveReaction(Dead);
	TestEqual(TEXT("dead: no reaction"), V(R.Reaction), V(EMDHitReaction::None));
	TestEqual(TEXT("dead: poise untouched"), R.PoiseAfter, 50.0f, 0.001f);

	// FlinchOnly (bosses): everything flinches except a poise break.
	FMDReactionInput Boss = Reaction(EMDHitReaction::Stagger, 50.0f, 40.0f);
	Boss.bFlinchOnly = true;
	R = UMDCombatRules::ResolveReaction(Boss);
	TestEqual(TEXT("boss heavy flinches"), V(R.Reaction), V(EMDHitReaction::Flinch));
	TestFalse(TEXT("boss heavy does not stun"), R.bStartStun);
	Boss.Poise = 5.0f;
	R = UMDCombatRules::ResolveReaction(Boss);
	TestEqual(TEXT("boss poise break staggers"), V(R.Reaction), V(EMDHitReaction::Stagger));
	TestTrue(TEXT("boss poise break stuns"), R.bStartStun);

	// Five dual-sword lights (7 poise each) against the 50 poise player do not break it; a tuned 12-poise hit does in five.
	float Poise = 50.0f;
	bool bStunned = false;
	for (int32 Hit = 0; Hit < 5; ++Hit)
	{
		R = UMDCombatRules::ResolveReaction(Reaction(EMDHitReaction::Flinch, Poise, 12.0f));
		Poise = R.PoiseAfter;
		bStunned = bStunned || R.bStartStun;
	}
	TestTrue(TEXT("five 12-poise lights stun a 50 poise target"), bStunned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHitDirectionTest, "MazeDemons.Combat.HitDirection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDHitDirectionTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	const FVector Victim = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;

	TestEqual(TEXT("attacker ahead"), V(UMDCombatRules::HitDirection(Forward, Victim, FVector(100, 0, 0))), V(EMDHitDirection::Front));
	TestEqual(TEXT("attacker behind"), V(UMDCombatRules::HitDirection(Forward, Victim, FVector(-100, 0, 0))), V(EMDHitDirection::Back));
	TestEqual(TEXT("attacker to the right"), V(UMDCombatRules::HitDirection(Forward, Victim, FVector(0, 100, 0))), V(EMDHitDirection::Right));
	TestEqual(TEXT("attacker to the left"), V(UMDCombatRules::HitDirection(Forward, Victim, FVector(0, -100, 0))), V(EMDHitDirection::Left));

	// Boundaries: 44 degrees is still front, 46 is a side; 134 is a side, 136 is back.
	auto At = [](float Degrees) { return FVector(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees)), 0.0f) * 100.0f; };
	TestEqual(TEXT("44 degrees"), V(UMDCombatRules::HitDirection(Forward, Victim, At(44.0f))), V(EMDHitDirection::Front));
	TestEqual(TEXT("46 degrees"), V(UMDCombatRules::HitDirection(Forward, Victim, At(46.0f))), V(EMDHitDirection::Right));
	TestEqual(TEXT("-46 degrees"), V(UMDCombatRules::HitDirection(Forward, Victim, At(-46.0f))), V(EMDHitDirection::Left));
	TestEqual(TEXT("134 degrees"), V(UMDCombatRules::HitDirection(Forward, Victim, At(134.0f))), V(EMDHitDirection::Right));
	TestEqual(TEXT("136 degrees"), V(UMDCombatRules::HitDirection(Forward, Victim, At(136.0f))), V(EMDHitDirection::Back));

	// Height is ignored; a rotated victim is relative to its own forward; degenerate input is Front.
	TestEqual(TEXT("height ignored"), V(UMDCombatRules::HitDirection(Forward, Victim, FVector(100, 0, 500))), V(EMDHitDirection::Front));
	TestEqual(TEXT("victim facing +Y, attacker at +X is on the left"), V(UMDCombatRules::HitDirection(FVector(0, 1, 0), Victim, FVector(100, 0, 0))), V(EMDHitDirection::Left));
	TestEqual(TEXT("same spot"), V(UMDCombatRules::HitDirection(Forward, Victim, Victim)), V(EMDHitDirection::Front));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDKnockbackVelocityTest, "MazeDemons.Combat.KnockbackVelocity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDKnockbackVelocityTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	const FVector Attacker(-100.0f, 0.0f, 50.0f);
	const FVector Victim(0.0f, 0.0f, 0.0f);

	// Enemies are launched: away from the attacker, with an upward component.
	const FVector Enemy = UMDCombatRules::KnockbackVelocity(500.0f, Attacker, Victim, false);
	TestEqual(TEXT("enemy pushed away on X"), F(Enemy.X), 500.0f, 0.01f);
	TestEqual(TEXT("enemy has no sideways push"), F(Enemy.Y), 0.0f, 0.01f);
	TestEqual(TEXT("enemy is launched upward"), F(Enemy.Z), 200.0f, 0.01f);

	// Players slide: horizontal only, whatever the height difference.
	const FVector Player = UMDCombatRules::KnockbackVelocity(500.0f, Attacker, Victim, true);
	TestEqual(TEXT("player pushed away on X"), F(Player.X), 500.0f, 0.01f);
	TestEqual(TEXT("player slide has no vertical component"), F(Player.Z), 0.0f, 0.0001f);

	TestEqual(TEXT("resist scales the strength"), F(UMDCombatRules::KnockbackVelocity(500.0f, Attacker, Victim, true, 0.5f).X), 250.0f, 0.01f);
	TestEqual(TEXT("full resist"), F(UMDCombatRules::KnockbackVelocity(500.0f, Attacker, Victim, false, 1.0f).Size()), 0.0f, 0.0001f);
	TestEqual(TEXT("no strength"), F(UMDCombatRules::KnockbackVelocity(0.0f, Attacker, Victim, false).Size()), 0.0f, 0.0001f);

	// Attacker straight above: still a defined horizontal direction.
	const FVector Same = UMDCombatRules::KnockbackVelocity(300.0f, FVector(0, 0, 100), Victim, true);
	TestEqual(TEXT("coincident in the plane keeps strength"), F(Same.Size()), 300.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCritPoiseBonusTest, "MazeDemons.Combat.CritPoiseBonus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDCritPoiseBonusTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("no crit"), UMDCombatRules::CritPoiseBonus(18.0f, false), 18.0f, 0.0001f);
	TestEqual(TEXT("crit is 1.5x"), UMDCombatRules::CritPoiseBonus(18.0f, true), 27.0f, 0.0001f);
	TestEqual(TEXT("zero stays zero"), UMDCombatRules::CritPoiseBonus(0.0f, true), 0.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAttackGateStaggeredTest, "MazeDemons.Combat.AttackGateStaggered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAttackGateStaggeredTest::RunTest(const FString& Parameters)
{
	using namespace MDCombatTestsLocal;
	// bHasPawn, bDead, bLocal, bMenuOpen, bHasCombat, bStaggered
	TestEqual(TEXT("staggered is blocked"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, true, true)), V(EMDAttackBlock::Staggered));
	TestEqual(TEXT("staggered remote pawn is blocked"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, false, false, true, true)), V(EMDAttackBlock::Staggered));
	TestEqual(TEXT("not staggered is allowed"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, true, false)), V(EMDAttackBlock::None));
	TestEqual(TEXT("the old five-argument call is unchanged"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, true)), V(EMDAttackBlock::None));

	// Precedence: no pawn and dead beat staggered; staggered beats the menu and a missing combat component.
	TestEqual(TEXT("no pawn wins"), V(UMDGameplayStatics::EvaluateAttackGate(false, true, true, true, false, true)), V(EMDAttackBlock::NoPawn));
	TestEqual(TEXT("dead beats staggered"), V(UMDGameplayStatics::EvaluateAttackGate(true, true, true, false, true, true)), V(EMDAttackBlock::Dead));
	TestEqual(TEXT("staggered beats menu"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, true, true, true, true)), V(EMDAttackBlock::Staggered));
	TestEqual(TEXT("staggered beats missing combat"), V(UMDGameplayStatics::EvaluateAttackGate(true, false, true, false, false, true)), V(EMDAttackBlock::Staggered));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHitStopClampTest, "MazeDemons.Combat.HitStopClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDHitStopClampTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("5 frames at 60 fps"), UMDCombatRules::HitStopSeconds(5), 5.0f / 60.0f, 0.0001f);
	TestEqual(TEXT("8 frames is clamped to 120 ms"), UMDCombatRules::HitStopSeconds(8), 0.12f, 0.0001f);
	TestEqual(TEXT("scale applies before the clamp"), UMDCombatRules::HitStopSeconds(3, 2.0f), 0.1f, 0.0001f);
	TestEqual(TEXT("huge scale still clamps"), UMDCombatRules::HitStopSeconds(5, 100.0f), 0.12f, 0.0001f);
	TestEqual(TEXT("scale 0 turns it off"), UMDCombatRules::HitStopSeconds(5, 0.0f), 0.0f, 0.0001f);
	TestEqual(TEXT("negative frames"), UMDCombatRules::HitStopSeconds(-4), 0.0f, 0.0001f);
	TestEqual(TEXT("custom ceiling"), UMDCombatRules::HitStopSeconds(8, 1.0f, 0.05f), 0.05f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDComboSetValidationTest, "MazeDemons.Combat.ComboSetValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDComboSetValidationTest::RunTest(const FString& Parameters)
{
	// Part 1: the validator itself, on transient data.
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	auto MakeStep = [Montage]()
	{
		FMDAttackStep Step;
		Step.Montage = Montage;
		return Step;
	};

	UMDComboSet* Good = NewObject<UMDComboSet>(GetTransientPackage());
	Good->LightChain = { MakeStep(), MakeStep(), MakeStep() };
	Good->Heavy = MakeStep();
	TArray<FString> Errors;
	TestTrue(TEXT("a complete set validates"), Good->Validate(Errors));
	TestEqual(TEXT("no errors"), Errors.Num(), 0);

	UMDComboSet* Empty = NewObject<UMDComboSet>(GetTransientPackage());
	Errors.Reset();
	TestFalse(TEXT("an empty set is rejected"), Empty->Validate(Errors));
	TestTrue(TEXT("with errors"), Errors.Num() > 0);

	UMDComboSet* Bad = NewObject<UMDComboSet>(GetTransientPackage());
	Bad->LightChain = { MakeStep() };
	Bad->Heavy = MakeStep();
	Bad->LightChain[0].HitStopFrames = 40;
	Bad->DodgeMontages.SetNum(3);
	Errors.Reset();
	TestFalse(TEXT("bad hit-stop and dodge montage count are rejected"), Bad->Validate(Errors));
	TestEqual(TEXT("two errors"), Errors.Num(), 2);

	// A Both / Off step is only valid in a dual set.
	UMDComboSet* Dual = NewObject<UMDComboSet>(GetTransientPackage());
	Dual->LightChain = { MakeStep(), MakeStep() };
	Dual->LightChain[1].Hand = EMDWeaponHand::Off;
	Dual->Heavy = MakeStep();
	Dual->Heavy.Hand = EMDWeaponHand::Both;
	Errors.Reset();
	TestFalse(TEXT("off/both steps in a single-weapon set are rejected"), Dual->Validate(Errors));
	TestEqual(TEXT("two errors for the two steps"), Errors.Num(), 2);
	Dual->bDualWield = true;
	Errors.Reset();
	TestTrue(TEXT("the same steps validate in a dual set"), Dual->Validate(Errors));

	// Loadout rule: two-hander wins, a matching dual pair uses the dual set, a lone sword keeps its own set.
	UMDComboSet* SwordSet = NewObject<UMDComboSet>(GetTransientPackage());
	UMDComboSet* ClaySet = NewObject<UMDComboSet>(GetTransientPackage());
	UMDComboSet* DualSet = NewObject<UMDComboSet>(GetTransientPackage());
	UMDComboSet* OtherDual = NewObject<UMDComboSet>(GetTransientPackage());
	TestTrue(TEXT("two-hander wins"), UMDComboSet::ChooseForLoadout(ClaySet, true, SwordSet, DualSet, DualSet) == ClaySet);
	TestTrue(TEXT("sword in both hands: dual set"), UMDComboSet::ChooseForLoadout(nullptr, false, SwordSet, DualSet, DualSet) == DualSet);
	TestTrue(TEXT("lone sword: its own set"), UMDComboSet::ChooseForLoadout(nullptr, false, SwordSet, DualSet, nullptr) == SwordSet);
	TestTrue(TEXT("mismatched off hand: main set"), UMDComboSet::ChooseForLoadout(nullptr, false, SwordSet, DualSet, OtherDual) == SwordSet);
	TestTrue(TEXT("no dual configured: main set"), UMDComboSet::ChooseForLoadout(nullptr, false, SwordSet, nullptr, nullptr) == SwordSet);
	TestTrue(TEXT("empty hands: none"), UMDComboSet::ChooseForLoadout(nullptr, false, nullptr, nullptr, nullptr) == nullptr);

	FMDAttackStep Fallback;
	TestFalse(TEXT("no dodge attack and no chain: no step"), Empty->GetStep(EMDAttackKind::DodgeAttack, 0, Fallback));
	TestEqual(TEXT("tuning follows the chain length"), Good->MakeTuning().LightCount, 3);

	// Part 2: every DA_Combo_* asset under /Game/Data/Combat. The assets are made in the editor (WP4); until then there are none
	// and this part passes vacuously.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/Data/Combat"));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UMDComboSet::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	int32 Checked = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (!Asset.AssetName.ToString().StartsWith(TEXT("DA_Combo_")))
		{
			continue;
		}
		const UMDComboSet* Set = Cast<UMDComboSet>(Asset.GetAsset());
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Asset.AssetName.ToString()), Set))
		{
			continue;
		}
		TArray<FString> AssetErrors;
		Set->Validate(AssetErrors);
		for (const FString& Error : AssetErrors)
		{
			AddError(Error);
		}
		++Checked;
	}
	if (Checked == 0)
	{
		AddInfo(TEXT("No DA_Combo_* assets under /Game/Data/Combat yet: the asset part passes vacuously."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDComboDPSCeilingTest, "MazeDemons.Balance.ComboDPSCeiling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDComboDPSCeilingTest::RunTest(const FString& Parameters)
{
	// BALANCE section 8: a build's DPS may not exceed 1.35x a bare character with perks alone, 1.6x with gear. A weapon's chain
	// is measured against the bare character's reference swing (FMDBalanceReference::PlayerAttackInterval) at equal modifiers, so
	// the ratio is independent of the perk set. The step times are the design's assumed montage lengths (C1 section 2.1); WP4
	// re-checks them against the real montages.
	const float AttackPower = 10.0f;
	const FMDPerkModifiers NoPerks;
	const FMDBalanceReference Reference;
	const float BareDPS = UMDBalanceModel::ExpectedDPS(AttackPower, NoPerks, 0.0f, Reference.PlayerAttackInterval);
	TestTrue(TEXT("bare DPS is positive"), BareDPS > 0.0f);

	struct FChain
	{
		const TCHAR* Name;
		TArray<float> Mults;
		TArray<float> Intervals;
	};
	const FChain Chains[] = {
		{ TEXT("Greatsword"), { 1.2f, 1.3f, 1.6f }, { 1.1f, 1.2f, 1.5f } },
		{ TEXT("DualSword"), { 0.7f, 0.7f, 0.8f, 0.9f, 1.1f }, { 0.6f, 0.6f, 0.65f, 0.7f, 0.9f } },
	};
	for (const FChain& Chain : Chains)
	{
		const float ChainDPS = UMDBalanceModel::ChainAverageDPS(AttackPower, NoPerks, 0.0f, Chain.Mults, Chain.Intervals);
		TestTrue(*FString::Printf(TEXT("%s chain has DPS"), Chain.Name), ChainDPS > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s chain DPS %.2f is within 1.35x of bare %.2f"), Chain.Name, ChainDPS, BareDPS), ChainDPS <= BareDPS * 1.35f);
	}

	// The helper itself: a one-step chain equals ExpectedDPS; empty or zero-time chains are 0; perks scale it like ExpectedDPS.
	TestEqual(TEXT("one step equals ExpectedDPS"), UMDBalanceModel::ChainAverageDPS(AttackPower, NoPerks, 0.0f, { 2.0f }, { 1.0f }),
		UMDBalanceModel::ExpectedDPS(AttackPower, NoPerks, 0.0f, 0.5f), 0.0001f);
	TestEqual(TEXT("empty chain"), UMDBalanceModel::ChainAverageDPS(AttackPower, NoPerks, 0.0f, {}, {}), 0.0f, 0.0001f);
	TestEqual(TEXT("zero time"), UMDBalanceModel::ChainAverageDPS(AttackPower, NoPerks, 0.0f, { 1.0f }, { 0.0f }), 0.0f, 0.0001f);

	FMDPerkModifiers Perks;
	Perks.AttackPowerPct = 0.12f;
	const TArray<float> Mults = { 1.0f, 1.0f };
	const TArray<float> Intervals = { 1.0f, 1.0f };
	TestEqual(TEXT("attack power perks scale the chain"), UMDBalanceModel::ChainAverageDPS(AttackPower, Perks, 0.0f, Mults, Intervals),
		UMDBalanceModel::ChainAverageDPS(AttackPower, NoPerks, 0.0f, Mults, Intervals) * 1.12f, 0.001f);
	return true;
}

// C1 WP6: hit-stop floors and the hit flash envelope.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDFeelHitStopFloorsTest, "MazeDemons.Combat.Feel.HitStopFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDFeelHitStopFloorsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("light raised to the floor"), UMDCombatRules::EffectiveHitStopFrames(2, EMDAttackKind::Light, 4, 7), 4);
	TestEqual(TEXT("heavy raised to the floor"), UMDCombatRules::EffectiveHitStopFrames(3, EMDAttackKind::Heavy, 4, 7), 7);
	TestEqual(TEXT("charged uses the heavy floor"), UMDCombatRules::EffectiveHitStopFrames(1, EMDAttackKind::Charged, 4, 7), 7);
	TestEqual(TEXT("longer step keeps its frames"), UMDCombatRules::EffectiveHitStopFrames(9, EMDAttackKind::Light, 4, 7), 9);
	TestEqual(TEXT("zero opts out"), UMDCombatRules::EffectiveHitStopFrames(0, EMDAttackKind::Heavy, 4, 7), 0);
	TestTrue(TEXT("7 frames fits under the 120 ms cap"), UMDCombatRules::HitStopSeconds(7) < UMDCombatRules::MaxHitStopSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDFeelFlashEnvelopeTest, "MazeDemons.Combat.Feel.FlashEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDFeelFlashEnvelopeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("full at the start"), UMDCombatRules::HitFlashEnvelope(0.0f), 1.0f, 0.0001f);
	TestEqual(TEXT("quarter at the middle"), UMDCombatRules::HitFlashEnvelope(0.5f), 0.25f, 0.0001f);
	TestEqual(TEXT("gone at the end"), UMDCombatRules::HitFlashEnvelope(1.0f), 0.0f, 0.0001f);
	TestEqual(TEXT("zero before the start"), UMDCombatRules::HitFlashEnvelope(-0.2f), 0.0f, 0.0001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
