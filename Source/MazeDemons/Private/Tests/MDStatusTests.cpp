// V1 WP6 status effect tests: DoT leaves durability alone and never crits, Bleed stacks to 3, Frost slows and drains stamina, Poison stops
// stamina regen, and the active set replicates to a client (late join included).
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDCombatTypes.h"
#include "MDDamageNumbers.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"
#include "MDStatusEffectComponent.h"
#include "MDStatusEffects.h"
#include "MDTestDummy.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// Friend of UMDStatusEffectComponent and UMDInventoryComponent: the replicated array and the durability state.
struct FMDStatusTestAccess
{
	static TArray<FMDStatusRep>& Statuses(UMDStatusEffectComponent* C) { return C->Statuses; }
	static void OnRepStatuses(UMDStatusEffectComponent* C) { C->OnRep_Statuses(); }

	static void SetEquipped(UMDInventoryComponent* C, uint8 Slot, FName Row, int32 Durability)
	{
		FMDEquippedSlot& Entry = C->Equipped.AddDefaulted_GetRef();
		Entry.Slot = Slot;
		Entry.ItemRowName = Row;
		C->SetConditionDurability(Slot, Durability);
	}
	static int32 Condition(const UMDInventoryComponent* C, uint8 Slot) { return C->GetConditionDurability(Slot); }
	static void Bind(UMDInventoryComponent* C) { C->BindToASC(); }
};

namespace MDStatusTestsLocal
{
	const FName ChestRow(TEXT("StatusTestChest"));

	// Transient world with a player (PlayerState + ASC + pawn + status component), tuned like the stamina fixture.
	struct FStatusFixture
	{
		UWorld* World = nullptr;
		AMDPlayerState* PS = nullptr;
		APawn* Pawn = nullptr;
		UMDAbilitySystemComponent* ASC = nullptr;
		UMDStatusEffectComponent* Status = nullptr;
		UMDStaminaComponent* Stamina = nullptr;

		bool Init(FAutomationTestBase& Test, const TCHAR* WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(WorldName));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			PS = World->SpawnActor<AMDPlayerState>();
			Pawn = World->SpawnActor<APawn>();
			if (!PS || !Pawn)
			{
				Test.AddError(TEXT("Could not spawn AMDPlayerState / APawn"));
				return false;
			}
			PS->SetRole(ROLE_Authority);
			Pawn->SetPlayerState(PS);
			ASC = PS->GetMDAbilitySystemComponent();
			Status = PS->StatusEffects;
			Stamina = PS->StaminaComponent;
			if (!ASC || !Status || !Stamina)
			{
				Test.AddError(TEXT("PlayerState is missing its ASC, status or stamina component"));
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

		~FStatusFixture()
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusDoTSkipsDurabilityTest, "MazeDemons.Status.DoTSkipsDurability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusDoTSkipsDurabilityTest::RunTest(const FString& Parameters)
{
	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusDurabilityWorld")))
	{
		return true;
	}

	UDataTable* Stats = NewObject<UDataTable>(GetTransientPackage());
	Stats->RowStruct = FMDItemStatsRow::StaticStruct();
	Stats->AddToRoot();
	FMDItemStatsRow Row;
	Row.ItemValue = 40;
	Row.MaxDurability = 100;
	Row.bUpgradeable = true;
	Stats->AddRow(ChestRow, Row);

	UMDInventoryComponent* Inv = NewObject<UMDInventoryComponent>(Fx.PS);
	Inv->ItemStatsTable = TSoftObjectPtr<UDataTable>(Stats);
	Inv->RegisterComponent();
	FMDStatusTestAccess::SetEquipped(Inv, MDItemType::Chest, ChestRow, 50);
	FMDStatusTestAccess::Bind(Inv);

	// Control: a normal hit wears the chest, so the rest of this test can actually see wear.
	Fx.ASC->ApplyDamage(5.0f, nullptr, nullptr, false);
	TestEqual(TEXT("control: a normal hit wears the chest"), FMDStatusTestAccess::Condition(Inv, MDItemType::Chest), 49);

	// Bleed for its full length: 5 ticks of 2% of 100 max health.
	const float HealthBefore = Fx.ASC->GetHealth();
	TestTrue(TEXT("bleed applied"), Fx.Status->ApplyStatus(EMDStatusType::Bleed));
	Fx.Status->AdvanceStatuses(5.0f);
	TestTrue(TEXT("the bleed took health"), Fx.ASC->GetHealth() < HealthBefore);
	TestEqual(TEXT("5 ticks x 2 health"), HealthBefore - Fx.ASC->GetHealth(), 10.0f);
	TestEqual(TEXT("DoT ticks wear no armor"), FMDStatusTestAccess::Condition(Inv, MDItemType::Chest), 49);

	// The typed-damage entry point (traps, spells) is the same path.
	FMDDamageResult Result;
	TestTrue(TEXT("typed damage lands"), Fx.ASC->ApplyDoTDamage(3.0f, nullptr, TAG_Damage_Type_Poison.GetTag(), Result));
	TestEqual(TEXT("applied amount"), Result.Applied, 3.0f);
	TestTrue(TEXT("damage type is kept"), Result.DamageType == TAG_Damage_Type_Poison.GetTag());
	TestEqual(TEXT("typed damage wears nothing either"), FMDStatusTestAccess::Condition(Inv, MDItemType::Chest), 49);

	// A normal hit still wears afterwards (the DoT path did not break the listener).
	Fx.ASC->ApplyDamage(5.0f, nullptr, nullptr, false);
	TestEqual(TEXT("a normal hit still wears"), FMDStatusTestAccess::Condition(Inv, MDItemType::Chest), 48);

	Stats->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusDoTNeverCritsTest, "MazeDemons.Status.DoTNeverCrits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusDoTNeverCritsTest::RunTest(const FString& Parameters)
{
	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusCritWorld")))
	{
		return true;
	}

	// The exec calc rule itself: no flag, no damage; the flag passes the raw amount through untouched.
	TestEqual(TEXT("exec calc ignores damage without the DoT flag"), UMDDoTExecCalc::ComputeDamage(0.0f, 10.0f), 0.0f);
	TestEqual(TEXT("exec calc passes DoT damage unchanged"), UMDDoTExecCalc::ComputeDamage(1.0f, 10.0f), 10.0f);
	TestEqual(TEXT("exec calc never goes negative"), UMDDoTExecCalc::ComputeDamage(1.0f, -4.0f), 0.0f);

	// Attacker: a player with the maximum crit chance. Victim: a dummy on the demon team.
	AMDTestDummy* Dummy = Fx.World->SpawnActor<AMDTestDummy>();
	if (!Dummy || !Dummy->AbilitySystem || !Dummy->Attributes)
	{
		AddWarning(TEXT("Could not spawn the test dummy; skipping"));
		return true;
	}
	UMDAbilitySystemComponent* VictimASC = Dummy->AbilitySystem;
	VictimASC->InitAbilityActorInfo(Dummy, Dummy);
	VictimASC->RegisterAttributeSet(Dummy->Attributes);
	VictimASC->InitializeStats(1000000.0f, 0.0f, 0.0f);
	Fx.ASC->GetMazeDemonsAttributes()->SetCritChance(0.25f);
	TestTrue(TEXT("attacker has a crit chance"), Fx.ASC->GetCritChance() > 0.0f);

	// Control: ordinary hits do crit with this attacker, so the DoT run below is a real test.
	int32 HitCrits = 0;
	for (int32 Index = 0; Index < 400; ++Index)
	{
		FMDHitContext Context;
		Context.Instigator = Fx.PS;
		Context.Causer = Fx.PS;
		Context.BaseDamage = 10.0f;
		FMDDamageResult Hit;
		VictimASC->ApplyDamage(Context, Hit);
		HitCrits += Hit.bCrit ? 1 : 0;
	}
	TestTrue(TEXT("control: ordinary hits crit for this attacker"), HitCrits > 0);

	int32 TickCrits = 0;
	int32 TickChanged = 0;
	for (int32 Index = 0; Index < 400; ++Index)
	{
		FMDDamageResult Tick;
		VictimASC->ApplyDoTDamage(10.0f, Fx.PS, TAG_Damage_Type_Bleed.GetTag(), Tick);
		TickCrits += Tick.bCrit ? 1 : 0;
		TickChanged += (!FMath::IsNearlyEqual(Tick.Damage, 10.0f) || !FMath::IsNearlyEqual(Tick.Applied, 10.0f)) ? 1 : 0;
	}
	TestEqual(TEXT("no DoT tick crits"), TickCrits, 0);
	TestEqual(TEXT("no DoT tick is multiplied"), TickChanged, 0);

	// Never the gold crit number: no status damage type resolves to the crit color.
	for (const EMDStatusType Type : { EMDStatusType::Bleed, EMDStatusType::Frost, EMDStatusType::Poison, EMDStatusType::Burn })
	{
		const FLinearColor Color = MDDamageNumbers::ResolveColor(FMDStatusRules::GetDamageType(Type), false);
		TestFalse(*FString::Printf(TEXT("%s number is not the crit color"), *FMDStatusRules::GetDisplayName(Type).ToString()), Color.Equals(MDDamageNumbers::DefaultCritColor()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusBleedStacksTo3Test, "MazeDemons.Status.BleedStacksTo3", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusBleedStacksTo3Test::RunTest(const FString& Parameters)
{
	// The rules on their own.
	TArray<FMDStatusRuntime> Active;
	TestTrue(TEXT("first bleed creates the entry"), FMDStatusRules::Apply(Active, EMDStatusType::Bleed, nullptr));
	TestEqual(TEXT("one stack"), Active[0].Stacks, 1);
	for (int32 Expected = 2; Expected <= 3; ++Expected)
	{
		TestFalse(TEXT("a repeat only stacks"), FMDStatusRules::Apply(Active, EMDStatusType::Bleed, nullptr));
		TestEqual(*FString::Printf(TEXT("stack %d"), Expected), Active[0].Stacks, Expected);
	}
	FMDStatusRules::Apply(Active, EMDStatusType::Bleed, nullptr);
	FMDStatusRules::Apply(Active, EMDStatusType::Bleed, nullptr);
	TestEqual(TEXT("stacks stop at 3"), Active[0].Stacks, 3);
	TestEqual(TEXT("one entry"), Active.Num(), 1);

	TestEqual(TEXT("tick damage scales with stacks"), FMDStatusRules::TickDamage(100.0f, EMDStatusType::Bleed, 3), 6.0f);
	TestEqual(TEXT("one stack ticks 2%"), FMDStatusRules::TickDamage(100.0f, EMDStatusType::Bleed, 1), 2.0f);

	// A reapply refreshes the clock.
	TArray<FMDStatusTick> Ticks;
	FMDStatusRules::Advance(Active, 3.0f, Ticks);
	TestEqual(TEXT("3 s in: 3 ticks"), Ticks.Num(), 3);
	FMDStatusRules::Apply(Active, EMDStatusType::Bleed, nullptr);
	TestEqual(TEXT("duration refreshed"), Active[0].Remaining, 5.0f);
	TestEqual(TEXT("tick budget refreshed"), Active[0].TicksLeft, 5);

	// The other effects do not stack.
	TArray<FMDStatusRuntime> Other;
	FMDStatusRules::Apply(Other, EMDStatusType::Poison, nullptr);
	FMDStatusRules::Apply(Other, EMDStatusType::Poison, nullptr);
	TestEqual(TEXT("poison stays at one stack"), Other[0].Stacks, 1);

	// On a real component: damage follows the stacks and the whole thing ends after the duration.
	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusBleedWorld")))
	{
		return true;
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Fx.Status->ApplyStatus(EMDStatusType::Bleed);
	}
	TestEqual(TEXT("component stacks to 3"), Fx.Status->GetStacks(EMDStatusType::Bleed), 3);
	TestTrue(TEXT("the Status.Bleed tag is granted"), Fx.ASC->HasMatchingGameplayTag(TAG_Status_Bleed.GetTag()));
	const float Before = Fx.ASC->GetHealth();
	Fx.Status->AdvanceStatuses(1.0f);
	TestEqual(TEXT("one tick at 3 stacks takes 6% of 100"), Before - Fx.ASC->GetHealth(), 6.0f);
	Fx.Status->AdvanceStatuses(4.0f);
	TestEqual(TEXT("5 ticks x 6 in total"), Before - Fx.ASC->GetHealth(), 30.0f);
	TestFalse(TEXT("bleed ended"), Fx.Status->HasStatus(EMDStatusType::Bleed));
	TestFalse(TEXT("the Status.Bleed tag is gone"), Fx.ASC->HasMatchingGameplayTag(TAG_Status_Bleed.GetTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusFrostModifiersTest, "MazeDemons.Status.FrostModifiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusFrostModifiersTest::RunTest(const FString& Parameters)
{
	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusFrostWorld")))
	{
		return true;
	}
	TestEqual(TEXT("no slow without frost"), Fx.Status->GetMoveSpeedScale(), 1.0f);
	TestEqual(TEXT("normal sprint drain"), Fx.Stamina->GetEffectiveSprintDrain(), 15.0f);

	TestTrue(TEXT("frost applied"), Fx.Status->ApplyStatus(EMDStatusType::Frost));
	TestEqual(TEXT("frost slows to 75%"), Fx.Status->GetMoveSpeedScale(), 0.75f);
	TestEqual(TEXT("frost drains stamina 20% faster"), Fx.Status->GetStaminaDrainScale(), 1.2f);
	TestEqual(TEXT("sprint drain 15 -> 18 per second"), Fx.Stamina->GetEffectiveSprintDrain(), 18.0f);
	TestFalse(TEXT("frost does not block regen"), Fx.Stamina->IsRegenBlocked());
	TestTrue(TEXT("Status.Frost granted"), Fx.ASC->HasMatchingGameplayTag(TAG_Status_Frost.GetTag()));

	// The same gate charges jumps more: 10 -> 12.
	Fx.Stamina->NotifyJumped();
	TestEqual(TEXT("a jump costs 12 under frost"), Fx.ASC->GetStamina(), 88.0f);

	// A sprint step under frost uses the faster drain.
	FMDStaminaStepInput In;
	In.Stamina = 100.0f;
	In.MaxStamina = 100.0f;
	In.SprintDrainPerSec = Fx.Stamina->GetEffectiveSprintDrain();
	In.DeltaTime = 1.0f;
	In.bSprinting = true;
	TestEqual(TEXT("one second of frosted sprint"), UMDStaminaComponent::Step(In).Stamina, 82.0f);

	// Frost refreshes (no stack) and ends after 4 s.
	Fx.Status->AdvanceStatuses(3.0f);
	Fx.Status->ApplyStatus(EMDStatusType::Frost);
	TestEqual(TEXT("frost does not stack"), Fx.Status->GetStacks(EMDStatusType::Frost), 1);
	Fx.Status->AdvanceStatuses(3.0f);
	TestTrue(TEXT("a refresh keeps frost going"), Fx.Status->HasStatus(EMDStatusType::Frost));
	Fx.Status->AdvanceStatuses(1.5f);
	TestFalse(TEXT("frost ends 4 s after the last hit"), Fx.Status->HasStatus(EMDStatusType::Frost));
	TestEqual(TEXT("speed back to normal"), Fx.Status->GetMoveSpeedScale(), 1.0f);
	TestEqual(TEXT("drain back to normal"), Fx.Stamina->GetEffectiveSprintDrain(), 15.0f);
	TestFalse(TEXT("Status.Frost removed"), Fx.ASC->HasMatchingGameplayTag(TAG_Status_Frost.GetTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusPoisonBlocksRegenTest, "MazeDemons.Status.PoisonBlocksStaminaRegen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusPoisonBlocksRegenTest::RunTest(const FString& Parameters)
{
	// Pure step: regen blocked means nothing comes back, sprint drain still applies.
	FMDStaminaStepInput In;
	In.Stamina = 50.0f;
	In.MaxStamina = 100.0f;
	In.RegenPerSec = 20.0f;
	In.RegenDelay = 1.0f;
	In.DeltaTime = 1.0f;
	In.TimeSinceSpend = 10.0f;
	TestEqual(TEXT("control: stamina regenerates"), UMDStaminaComponent::Step(In).Stamina, 70.0f);
	In.bRegenBlocked = true;
	TestEqual(TEXT("blocked: no regen"), UMDStaminaComponent::Step(In).Stamina, 50.0f);
	In.bSprinting = true;
	In.SprintDrainPerSec = 15.0f;
	TestEqual(TEXT("blocked: sprint still drains"), UMDStaminaComponent::Step(In).Stamina, 35.0f);

	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusPoisonWorld")))
	{
		return true;
	}
	TestFalse(TEXT("no block without poison"), Fx.Stamina->IsRegenBlocked());
	TestTrue(TEXT("poison applied"), Fx.Status->ApplyStatus(EMDStatusType::Poison));
	TestTrue(TEXT("poison blocks stamina regen"), Fx.Stamina->IsRegenBlocked());
	TestTrue(TEXT("Status.Poison granted"), Fx.ASC->HasMatchingGameplayTag(TAG_Status_Poison.GetTag()));

	// The server tick feeds the component's answer into the step: run it the way TickComponent does.
	In = FMDStaminaStepInput();
	In.Stamina = 50.0f;
	In.MaxStamina = 100.0f;
	In.DeltaTime = 1.0f;
	In.TimeSinceSpend = 10.0f;
	In.bRegenBlocked = Fx.Stamina->IsRegenBlocked();
	TestEqual(TEXT("stamina stays put while poisoned"), UMDStaminaComponent::Step(In).Stamina, 50.0f);

	// Poison: 8 ticks of 1.5% over 8 s, no stacking, ends and regen returns.
	const float Before = Fx.ASC->GetHealth();
	Fx.Status->AdvanceStatuses(8.0f);
	TestEqual(TEXT("8 ticks x 1.5"), Before - Fx.ASC->GetHealth(), 12.0f);
	TestFalse(TEXT("poison ended"), Fx.Status->HasStatus(EMDStatusType::Poison));
	TestFalse(TEXT("regen is back"), Fx.Stamina->IsRegenBlocked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStatusReplicatesToClientTest, "MazeDemons.Status.ReplicatesToClient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStatusReplicatesToClientTest::RunTest(const FString& Parameters)
{
	using namespace MDStatusTestsLocal;
	FStatusFixture Fx;
	if (!Fx.Init(*this, TEXT("MDStatusReplicationWorld")))
	{
		return true;
	}

	// The active set is a replicated property with a RepNotify.
	const FProperty* StatusProp = UMDStatusEffectComponent::StaticClass()->FindPropertyByName(TEXT("Statuses"));
	TestTrue(TEXT("the status set is a replicated property"), StatusProp && StatusProp->HasAnyPropertyFlags(CPF_Net));

	// Server state: 2 bleed stacks and frost.
	Fx.Status->ApplyStatus(EMDStatusType::Bleed);
	Fx.Status->ApplyStatus(EMDStatusType::Bleed);
	Fx.Status->ApplyStatus(EMDStatusType::Frost);
	const TArray<FMDStatusRep>& ServerSet = Fx.Status->GetActiveStatuses();
	TestEqual(TEXT("server holds two statuses"), ServerSet.Num(), 2);

	// A client (here a second player state that is not the authority) receives the array: initial replication is what a late joiner gets.
	AMDPlayerState* ClientPS = Fx.World->SpawnActor<AMDPlayerState>();
	if (!ClientPS || !ClientPS->StatusEffects)
	{
		AddError(TEXT("Could not spawn the client player state"));
		return true;
	}
	ClientPS->SetRole(ROLE_SimulatedProxy);
	UMDStatusEffectComponent* Client = ClientPS->StatusEffects;
	TestFalse(TEXT("the client copy has no authority"), ClientPS->HasAuthority());
	TestFalse(TEXT("the client sees nothing before replication"), Client->HasStatus(EMDStatusType::Bleed));

	FMDStatusTestAccess::Statuses(Client) = ServerSet;
	FMDStatusTestAccess::OnRepStatuses(Client);

	TestTrue(TEXT("client sees bleed"), Client->HasStatus(EMDStatusType::Bleed));
	TestEqual(TEXT("client sees both bleed stacks"), Client->GetStacks(EMDStatusType::Bleed), 2);
	TestTrue(TEXT("client sees frost"), Client->HasStatus(EMDStatusType::Frost));
	TestEqual(TEXT("client frost slow"), Client->GetMoveSpeedScale(), 0.75f);
	TestEqual(TEXT("client frost drain"), Client->GetStaminaDrainScale(), 1.2f);
	TestFalse(TEXT("client does not see poison"), Client->HasStatus(EMDStatusType::Poison));
	TestTrue(TEXT("the countdown is derived from the server clock"), Client->GetRemainingSeconds(EMDStatusType::Bleed) > 0.0f);

	// Later changes replicate again: poison joins, frost ends.
	Fx.Status->ApplyStatus(EMDStatusType::Poison);
	Fx.Status->ClearStatus(EMDStatusType::Frost);
	FMDStatusTestAccess::Statuses(Client) = Fx.Status->GetActiveStatuses();
	FMDStatusTestAccess::OnRepStatuses(Client);
	TestTrue(TEXT("client now sees poison"), Client->BlocksStaminaRegen());
	TestFalse(TEXT("client no longer sees frost"), Client->HasStatus(EMDStatusType::Frost));

	// Death ends everything and the client follows.
	Fx.ASC->DebugKill(nullptr);
	TestEqual(TEXT("death clears the server set"), Fx.Status->GetActiveStatuses().Num(), 0);
	FMDStatusTestAccess::Statuses(Client) = Fx.Status->GetActiveStatuses();
	FMDStatusTestAccess::OnRepStatuses(Client);
	TestEqual(TEXT("the client set is empty too"), Client->GetActiveStatuses().Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
