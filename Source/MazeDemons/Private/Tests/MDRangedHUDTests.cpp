// R7 tests: the ranged HUD. Ammo counter binding, reticle spread mapping, heat / charge meter states, hit marker colours, the gun compare
// rows, the station Ammo tab bookkeeping and the options clamps. The widgets are created in a transient game world and refreshed by hand
// (the test world does not tick).
#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "MDAbilitySystemComponent.h"
#include "MDDamageNumbers.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryComponent.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDStationAmmoPanel.h"
#include "MDUISettings.h"
#include "Ranged/MDAmmoCounterWidget.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDHUDOptions.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedReticleWidget.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedUIRules.h"
#include "Ranged/MDRangedWeaponData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MDRangedHUDTestsLocal
{
	const FName Light(TEXT("AmmoLight"));

	UMDRangedWeaponData* MakeGun(int32 MagSize = 8)
	{
		UMDRangedWeaponData* Data = NewObject<UMDRangedWeaponData>(GetTransientPackage());
		Data->FireMode = EMDFireMode::Semi;
		Data->HitModel = EMDRangedHitModel::Projectile;
		Data->RoundsPerSecond = 3.0f;
		Data->DamagePerShot = 10.0f;
		Data->SpreadHip = 3.0f;
		Data->SpreadAimed = 1.0f;
		Data->SpreadBloom = 0.0f;
		Data->MagSize = MagSize;
		Data->AmmoRow = Light;
		Data->ReloadSeconds = 1.0f;
		return Data;
	}

	struct FHudFixture
	{
		UWorld* World = nullptr;
		APawn* Pawn = nullptr;
		AMDPlayerState* State = nullptr;
		UMDRangedComponent* Ranged = nullptr;
		UMDInventoryComponent* Inv = nullptr;
		UMDRangedWeaponData* Gun = nullptr;

		bool Init(FAutomationTestBase& Test, UMDRangedWeaponData* InGun)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRangedHUDTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			if (AMDGameState* GameState = World->SpawnActor<AMDGameState>())
			{
				World->SetGameState(GameState);
			}
			Pawn = World->SpawnActor<APawn>();
			State = World->SpawnActor<AMDPlayerState>();
			if (!Pawn || !State)
			{
				Test.AddError(TEXT("Could not spawn the pawn or its player state"));
				return false;
			}
			State->SetRole(ROLE_Authority);
			Pawn->SetPlayerState(State);
			UMDAbilitySystemComponent* ASC = State->GetMDAbilitySystemComponent();
			if (!ASC)
			{
				Test.AddError(TEXT("Player state has no ASC"));
				return false;
			}
			ASC->InitAbilityActorInfo(State, Pawn);
			ASC->RegisterAttributeSet(State->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			Ranged = State->RangedComponent;
			UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
			if (!Ranged || !Items)
			{
				Test.AddError(TEXT("Missing ranged component or DT_Items"));
				return false;
			}
			Inv = NewObject<UMDInventoryComponent>(State);
			Inv->ItemTable = Items;
			Inv->RegisterComponent();
			Ranged->SetPawnForTests(Pawn);
			Gun = InGun;
			Ranged->SetWeaponDataForTests(Gun);
			Ranged->SetRangedMode(true);
			return true;
		}

		~FHudFixture()
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

	bool SameColour(const FLinearColor& A, const FLinearColor& B)
	{
		return FMath::IsNearlyEqual(A.R, B.R, 0.01f) && FMath::IsNearlyEqual(A.G, B.G, 0.01f) && FMath::IsNearlyEqual(A.B, B.B, 0.01f);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7AmmoCounterBindsTest, "MazeDemons.Ranged.UI.AmmoCounterBinds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7AmmoCounterBindsTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	// Pure rules first.
	TestEqual(TEXT("mag > 0: no prompt"), FMDRangedUIRules::AmmoPrompt(3, 40, false), EMDAmmoPrompt::None);
	TestEqual(TEXT("empty mag with reserve: RELOAD"), FMDRangedUIRules::AmmoPrompt(0, 40, false), EMDAmmoPrompt::Reload);
	TestEqual(TEXT("empty mag and bag: NO AMMO"), FMDRangedUIRules::AmmoPrompt(0, 0, false), EMDAmmoPrompt::NoAmmo);
	TestEqual(TEXT("reloading: no prompt"), FMDRangedUIRules::AmmoPrompt(0, 40, true), EMDAmmoPrompt::None);
	TestEqual(TEXT("shown in ranged mode with a gun"), FMDRangedUIRules::CounterTargetOpacity(true, true), 1.0f);
	TestEqual(TEXT("hidden outside ranged mode"), FMDRangedUIRules::CounterTargetOpacity(false, true), 0.0f);
	TestEqual(TEXT("hidden without a gun"), FMDRangedUIRules::CounterTargetOpacity(true, false), 0.0f);
	float Opacity = 0.0f;
	Opacity = FMDRangedUIRules::StepOpacity(Opacity, 1.0f, 0.06f);
	TestTrue(TEXT("fade in is under way and not instant"), Opacity > 0.0f && Opacity < 1.0f);
	Opacity = FMDRangedUIRules::StepOpacity(Opacity, 1.0f, 1.0f);
	TestEqual(TEXT("fade in lands on 1"), Opacity, 1.0f);
	Opacity = FMDRangedUIRules::StepOpacity(Opacity, 0.0f, 1.0f);
	TestEqual(TEXT("fade out lands on 0"), Opacity, 0.0f);

	FHudFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	Fx.Inv->AddItem(Light, 40);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 8);
	UMDAmmoCounterWidget* Counter = CreateWidget<UMDAmmoCounterWidget>(Fx.World);
	if (!TestNotNull(TEXT("counter widget"), Counter))
	{
		return false;
	}
	Counter->SetRanged(Fx.Ranged);
	Counter->Refresh();
	TestEqual(TEXT("mag text"), Counter->GetMagText().ToString(), FString(TEXT("8")));
	TestEqual(TEXT("reserve text"), Counter->GetReserveText().ToString(), FString(TEXT("40")));
	TestFalse(TEXT("full magazine is not red"), Counter->IsMagRed());
	TestEqual(TEXT("no prompt"), Counter->GetPrompt(), EMDAmmoPrompt::None);
	TestTrue(TEXT("ammo section shown for a gun that uses ammo"), Counter->IsAmmoSectionVisible());
	TestFalse(TEXT("no heat meter on a gun"), Counter->IsHeatMeterVisible());
	TestEqual(TEXT("target shown in ranged mode"), Counter->GetTargetOpacity(), 1.0f);

	// Reads replicated state live: change it under the widget and refresh again (no cache).
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 2);
	Counter->Refresh();
	TestEqual(TEXT("2 of 8 is above 20%"), Counter->IsMagRed(), false);
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 1);
	Counter->Refresh();
	TestTrue(TEXT("1 of 8 is red"), Counter->IsMagRed());
	TestEqual(TEXT("mag text follows"), Counter->GetMagText().ToString(), FString(TEXT("1")));
	Fx.Ranged->SetMagRounds(MDItemType::RangedSlot, 0);
	Counter->Refresh();
	TestEqual(TEXT("empty mag with reserve: RELOAD"), Counter->GetPrompt(), EMDAmmoPrompt::Reload);
	Fx.Inv->ConsumeAmmo(Light, 40);
	Counter->Refresh();
	TestEqual(TEXT("empty everywhere: NO AMMO"), Counter->GetPrompt(), EMDAmmoPrompt::NoAmmo);

	// Hidden outside ranged mode, then the fade runs.
	Fx.Ranged->SetRangedMode(false);
	Counter->Refresh();
	TestEqual(TEXT("target hidden after the swap back"), Counter->GetTargetOpacity(), 0.0f);
	Fx.Ranged->SetRangedMode(true);
	Counter->Refresh();
	Counter->TickFade(0.05f);
	TestTrue(TEXT("fading in"), Counter->GetShownOpacity() > 0.0f && Counter->GetShownOpacity() < 1.0f);
	Counter->TickFade(1.0f);
	TestEqual(TEXT("fully in"), Counter->GetShownOpacity(), 1.0f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7ReticleSpreadMappingTest, "MazeDemons.Ranged.UI.ReticleSpreadMapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7ReticleSpreadMappingTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	const float Min = FMDRangedUIRules::ReticleMinGap;
	TestEqual(TEXT("zero spread sits at the minimum gap"), FMDRangedUIRules::ReticleGapPixels(0.0f, 60.0f, 1080.0f), Min);
	TestEqual(TEXT("negative spread is clamped"), FMDRangedUIRules::ReticleGapPixels(-2.0f, 60.0f, 1080.0f), Min);
	const float Aimed = FMDRangedUIRules::ReticleGapPixels(1.0f, 60.0f, 1080.0f);
	const float Hip = FMDRangedUIRules::ReticleGapPixels(3.0f, 60.0f, 1080.0f);
	TestTrue(TEXT("hip is wider than aimed"), Hip > Aimed && Aimed > Min);
	// 1 degree at 60 deg vertical FOV on 1080 px = 18 px per degree.
	TestTrue(TEXT("pixels per degree follows the FOV and the viewport"), FMath::IsNearlyEqual(Aimed, Min + 18.0f, 0.01f));
	TestTrue(TEXT("a narrower FOV opens the gap"), FMDRangedUIRules::ReticleGapPixels(1.0f, 40.0f, 1080.0f) > Aimed);
	TestEqual(TEXT("reticle size option is clamped low"), FMDRangedUIRules::ClampReticleScale(0.1f), FMDRangedUIRules::ReticleScaleMin);
	TestEqual(TEXT("reticle size option is clamped high"), FMDRangedUIRules::ClampReticleScale(9.0f), FMDRangedUIRules::ReticleScaleMax);

	// Bloom animates: opens fast, closes slower, never overshoots.
	const float Opening = FMDRangedUIRules::AnimateGap(10.0f, 50.0f, 0.03f) - 10.0f;
	const float Closing = 50.0f - FMDRangedUIRules::AnimateGap(50.0f, 10.0f, 0.03f);
	TestTrue(TEXT("opens faster than it closes"), Opening > Closing);
	TestTrue(TEXT("opening stays below the target"), FMDRangedUIRules::AnimateGap(10.0f, 50.0f, 10.0f) <= 50.0f);
	TestTrue(TEXT("closing stays above the target"), FMDRangedUIRules::AnimateGap(50.0f, 10.0f, 10.0f) >= 10.0f);

	// The component's current spread (R1 math) feeds it: hip wide, aimed tight.
	FHudFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	const float HipSpread = Fx.Ranged->GetCurrentSpread(false);
	Fx.Ranged->SetAimed(true);
	const float AimedSpread = Fx.Ranged->GetCurrentSpread(false);
	TestTrue(TEXT("aiming tightens the spread"), AimedSpread < HipSpread);
	TestTrue(TEXT("so the aimed reticle gap is smaller"), FMDRangedUIRules::ReticleGapPixels(AimedSpread, 60.0f, 1080.0f) < FMDRangedUIRules::ReticleGapPixels(HipSpread, 60.0f, 1080.0f));

	UMDRangedReticleWidget* Reticle = CreateWidget<UMDRangedReticleWidget>(Fx.World);
	if (!TestNotNull(TEXT("reticle widget"), Reticle))
	{
		return false;
	}
	TestNotNull(TEXT("the lock-on diamond has a slot to nest into"), Reticle->GetLockOnSlot());
	TestEqual(TEXT("the widget forwards the mapping"), UMDRangedReticleWidget::SpreadToPixels(3.0f, 60.0f, 1080.0f), Hip);
	Reticle->UpdateGap(0.016f, 40.0f);
	TestEqual(TEXT("the first frame jumps to the target"), Reticle->GetDisplayGap(), 40.0f);
	Reticle->UpdateGap(0.016f, 80.0f);
	TestTrue(TEXT("then the gap eases toward it"), Reticle->GetDisplayGap() > 40.0f && Reticle->GetDisplayGap() < 80.0f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7HeatMeterStatesTest, "MazeDemons.Ranged.UI.HeatMeterStates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7HeatMeterStatesTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	using EState = EMDHeatMeterState;
	TestEqual(TEXT("not a beam: hidden"), FMDRangedUIRules::HeatMeterState(false, false, 50.0f, 100.0f, false), EState::Hidden);
	TestEqual(TEXT("beam, cold: idle"), FMDRangedUIRules::HeatMeterState(true, false, 0.0f, 100.0f, false), EState::Idle);
	TestEqual(TEXT("beam, zero max: idle"), FMDRangedUIRules::HeatMeterState(true, false, 5.0f, 0.0f, false), EState::Idle);
	TestEqual(TEXT("beam, warm: heating"), FMDRangedUIRules::HeatMeterState(true, false, 40.0f, 100.0f, false), EState::Heating);
	TestEqual(TEXT("beam, 80 percent: warning"), FMDRangedUIRules::HeatMeterState(true, false, 80.0f, 100.0f, false), EState::Warning);
	TestEqual(TEXT("overheat wins"), FMDRangedUIRules::HeatMeterState(true, false, 100.0f, 100.0f, true), EState::Overheated);
	TestEqual(TEXT("charge style shows the charge"), FMDRangedUIRules::HeatMeterState(true, true, 60.0f, 100.0f, false), EState::Charging);
	TestEqual(TEXT("heating is orange"), FMDRangedUIRules::HeatMeterToken(EState::Heating), EMDColor::Warning);
	TestEqual(TEXT("warning is red"), FMDRangedUIRules::HeatMeterToken(EState::Warning), EMDColor::Danger);
	TestEqual(TEXT("overheat is red"), FMDRangedUIRules::HeatMeterToken(EState::Overheated), EMDColor::Danger);
	TestEqual(TEXT("charge is steel blue"), FMDRangedUIRules::HeatMeterToken(EState::Charging), EMDColor::Upgrade);
	TestEqual(TEXT("fraction clamps"), FMDRangedUIRules::MeterFraction(150.0f, 100.0f), 1.0f);
	TestEqual(TEXT("fraction of a zero max is 0"), FMDRangedUIRules::MeterFraction(10.0f, 0.0f), 0.0f);

	// The stub binding: a beam gun shows an idle meter and zero values, a normal gun shows no meter.
	FHudFixture Fx;
	UMDRangedWeaponData* Beam = MakeGun();
	Beam->FireMode = EMDFireMode::Beam;
	Beam->HitModel = EMDRangedHitModel::Beam;
	Beam->AmmoRow = NAME_None;
	if (!Fx.Init(*this, Beam))
	{
		return false;
	}
	TestTrue(TEXT("beam weapon detected"), Fx.Ranged->IsBeamWeapon());
	TestEqual(TEXT("stub heat is 0"), Fx.Ranged->GetHeat(), 0.0f);
	TestEqual(TEXT("stub max heat is 0"), Fx.Ranged->GetMaxHeat(), 0.0f);
	TestFalse(TEXT("stub never overheats"), Fx.Ranged->IsOverheated());
	UMDAmmoCounterWidget* Counter = CreateWidget<UMDAmmoCounterWidget>(Fx.World);
	if (!TestNotNull(TEXT("counter widget"), Counter))
	{
		return false;
	}
	Counter->SetRanged(Fx.Ranged);
	Counter->Refresh();
	TestTrue(TEXT("meter visible for a beam"), Counter->IsHeatMeterVisible());
	TestEqual(TEXT("meter idle on the stub"), Counter->GetHeatState(), EState::Idle);
	TestFalse(TEXT("a beam shows the meter instead of the ammo numbers"), Counter->IsAmmoSectionVisible());
	TestEqual(TEXT("no reload prompt for a beam"), Counter->GetPrompt(), EMDAmmoPrompt::None);

	Beam->FireMode = EMDFireMode::Semi;
	Beam->HitModel = EMDRangedHitModel::Projectile;
	Beam->AmmoRow = Light;
	Counter->Refresh();
	TestFalse(TEXT("meter hidden once the gun is not a beam"), Counter->IsHeatMeterVisible());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7HitMarkerColoursTest, "MazeDemons.Ranged.UI.HitMarkerColours", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7HitMarkerColoursTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	const FGameplayTag None;
	TestTrue(TEXT("plain hit is white"), SameColour(FMDRangedUIRules::HitMarkerColour(false, None), FLinearColor::White));
	TestTrue(TEXT("physical hit is white"), SameColour(FMDRangedUIRules::HitMarkerColour(false, TAG_Damage_Type_Physical.GetTag()), FLinearColor::White));
	TestTrue(TEXT("kill is red"), SameColour(FMDRangedUIRules::HitMarkerColour(true, None), MDDamageNumbers::DefaultCritColor()));
	TestTrue(TEXT("frost hit is frost blue"), SameColour(FMDRangedUIRules::HitMarkerColour(false, TAG_Damage_Type_Frost.GetTag()), MDDamageNumbers::DefaultTypeColor(TAG_Damage_Type_Frost.GetTag())));
	TestTrue(TEXT("fire hit is orange"), SameColour(FMDRangedUIRules::HitMarkerColour(false, TAG_Damage_Type_Fire.GetTag()), MDDamageNumbers::DefaultTypeColor(TAG_Damage_Type_Fire.GetTag())));
	TestFalse(TEXT("typed hit is not white"), SameColour(FMDRangedUIRules::HitMarkerColour(false, TAG_Damage_Type_Poison.GetTag()), FLinearColor::White));
	TestTrue(TEXT("a kill beats the element"), SameColour(FMDRangedUIRules::HitMarkerColour(true, TAG_Damage_Type_Frost.GetTag()), MDDamageNumbers::DefaultCritColor()));
	TestTrue(TEXT("headshot adds the extra tick"), FMDRangedUIRules::ShowsHeadshotTick(true));
	TestFalse(TEXT("no tick without a headshot"), FMDRangedUIRules::ShowsHeadshotTick(false));

	FHudFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	UMDRangedReticleWidget* Reticle = CreateWidget<UMDRangedReticleWidget>(Fx.World);
	if (!TestNotNull(TEXT("reticle widget"), Reticle))
	{
		return false;
	}
	TestFalse(TEXT("no marker before a hit"), Reticle->IsMarkerActive());
	Reticle->OnHitConfirmed(false, true, TAG_Damage_Type_Frost.GetTag());
	TestTrue(TEXT("marker running"), Reticle->IsMarkerActive());
	TestTrue(TEXT("headshot flag kept"), Reticle->IsMarkerHeadshot());
	TestTrue(TEXT("element colour kept"), SameColour(Reticle->GetMarkerColour(), MDDamageNumbers::DefaultTypeColor(TAG_Damage_Type_Frost.GetTag())));
	Reticle->OnHitConfirmed(true, false, None);
	TestTrue(TEXT("kill turns it red"), SameColour(Reticle->GetMarkerColour(), MDDamageNumbers::DefaultCritColor()));
	TestFalse(TEXT("headshot flag cleared"), Reticle->IsMarkerHeadshot());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7GunCompareRowsTest, "MazeDemons.Ranged.UI.GunCompareRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7GunCompareRowsTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	UMDRangedWeaponData* Pistol = MakeGun(8);
	Pistol->DamagePerShot = 45.0f;
	Pistol->RoundsPerSecond = 3.0f;
	Pistol->ReloadSeconds = 1.2f;
	Pistol->SpreadHip = 3.0f;
	UMDRangedWeaponData* Repeater = MakeGun(24);
	Repeater->DamagePerShot = 22.0f;
	Repeater->RoundsPerSecond = 8.0f;
	Repeater->ReloadSeconds = 1.9f;
	Repeater->SpreadHip = 4.5f;

	TestEqual(TEXT("no gun, no rows"), UMDGameplayStatics::MakeGunCompareLines(Pistol, nullptr).Num(), 0);
	const TArray<FMDStatLine> Rows = UMDGameplayStatics::MakeGunCompareLines(Pistol, Repeater);
	if (!TestEqual(TEXT("five rows: damage, rounds/s, magazine, reload, spread"), Rows.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("damage got worse"), Rows[0].DeltaSign, -1);
	TestEqual(TEXT("damage delta text"), Rows[0].DeltaText.ToString(), FString(TEXT("−23")));
	TestEqual(TEXT("fire rate got better"), Rows[1].DeltaSign, 1);
	TestEqual(TEXT("magazine got better"), Rows[2].DeltaSign, 1);
	TestEqual(TEXT("magazine texts"), Rows[2].CurrentText.ToString() + TEXT(" > ") + Rows[2].NewText.ToString(), FString(TEXT("8 > 24")));
	TestEqual(TEXT("a longer reload is worse (lower is better)"), Rows[3].DeltaSign, -1);
	TestEqual(TEXT("reload delta text shows the plain change"), Rows[3].DeltaText.ToString(), FString(TEXT("+0.7 s")));
	TestEqual(TEXT("a wider spread is worse (lower is better)"), Rows[4].DeltaSign, -1);

	const TArray<FMDStatLine> Same = UMDGameplayStatics::MakeGunCompareLines(Pistol, Pistol);
	for (const FMDStatLine& Line : Same)
	{
		TestEqual(TEXT("identical gun: no change"), Line.DeltaSign, 0);
	}
	const TArray<FMDStatLine> NoOld = UMDGameplayStatics::MakeGunCompareLines(nullptr, Pistol);
	TestEqual(TEXT("empty slot still shows five rows"), NoOld.Num(), 5);
	TestEqual(TEXT("empty slot shows a dash"), NoOld[0].CurrentText.ToString(), FString(TEXT("-")));
	TestEqual(TEXT("empty slot shows no delta"), NoOld[0].DeltaSign, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7StationAmmoTabTest, "MazeDemons.Ranged.UI.StationAmmoTab", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7StationAmmoTabTest::RunTest(const FString& Parameters)
{
	using namespace MDRangedHUDTestsLocal;
	using Panel = UMDStationAmmoPanel;
	// LB / RB: steps over disabled tabs, wraps, stays put when nothing else is on.
	TestEqual(TEXT("RB from Repair -> Upgrade"), Panel::NextEnabledTab(0, 1, true, true, true, true), 1);
	TestEqual(TEXT("RB from Respec -> Ammo"), Panel::NextEnabledTab(2, 1, true, true, true, true), 3);
	TestEqual(TEXT("RB from Ammo wraps to Repair"), Panel::NextEnabledTab(3, 1, true, true, true, true), 0);
	TestEqual(TEXT("LB from Repair wraps to Ammo"), Panel::NextEnabledTab(0, -1, true, true, true, true), 3);
	TestEqual(TEXT("RB skips a disabled Upgrade"), Panel::NextEnabledTab(0, 1, true, false, true, true), 2);
	TestEqual(TEXT("LB from Ammo skips disabled Respec"), Panel::NextEnabledTab(3, -1, true, true, false, true), 1);
	TestEqual(TEXT("an ammo-only station stays on Ammo"), Panel::NextEnabledTab(3, 1, false, false, false, true), 3);
	TestEqual(TEXT("Repair + Ammo station toggles"), Panel::NextEnabledTab(0, 1, true, false, false, true), 3);
	TestEqual(TEXT("the default tab is the first offered service"), Panel::PickDefaultTab(true, true, true, true), 0);
	TestEqual(TEXT("no repair: Upgrade"), Panel::PickDefaultTab(false, true, true, true), 1);
	TestEqual(TEXT("Respec before Ammo"), Panel::PickDefaultTab(false, false, true, true), 2);
	TestEqual(TEXT("an ammo-only station opens on Ammo"), Panel::PickDefaultTab(false, false, false, true), 3);
	TestEqual(TEXT("nothing offered falls back to Repair"), Panel::PickDefaultTab(false, false, false, false), 0);

	FHudFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	UMDStationAmmoPanel* Pane = CreateWidget<UMDStationAmmoPanel>(Fx.World);
	if (!TestNotNull(TEXT("ammo pane"), Pane))
	{
		return false;
	}
	TestEqual(TEXT("one row per sold ammo type"), Pane->GetRowCount(), Panel::GetSoldRows().Num());
	TestEqual(TEXT("two types: light and heavy"), Pane->GetRowCount(), 2);
	Pane->MoveFocus(5);
	TestEqual(TEXT("focus clamps at the last row"), Pane->GetSelectedRow(), 1);
	Pane->MoveFocus(-5);
	TestEqual(TEXT("focus clamps at the first row"), Pane->GetSelectedRow(), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDR7OptionsTest, "MazeDemons.Ranged.UI.Options", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDR7OptionsTest::RunTest(const FString& Parameters)
{
	UMDLocalSettings* Settings = GetMutableDefault<UMDLocalSettings>();
	const float OldAssist = Settings->AimAssistStrength;
	const bool OldToggle = Settings->bAimToggle;
	const float OldScale = Settings->ReticleScale;
	const bool OldAuto = Settings->bAutoReload;
	const bool OldThreat = Settings->bOffscreenThreat;

	UMDHUDOptions::SetAimAssistStrength(7.0f);
	TestEqual(TEXT("aim assist clamps to 1"), Settings->AimAssistStrength, 1.0f);
	UMDHUDOptions::SetAimAssistStrength(-1.0f);
	TestEqual(TEXT("aim assist clamps to 0"), Settings->AimAssistStrength, 0.0f);
	UMDHUDOptions::SetReticleScale(10.0f);
	TestEqual(TEXT("reticle size clamps"), UMDHUDOptions::GetReticleScale(), FMDRangedUIRules::ReticleScaleMax);
	UMDHUDOptions::SetAimToggle(true);
	TestTrue(TEXT("toggle stored"), Settings->bAimToggle);
	UMDHUDOptions::SetAutoReload(false);
	TestFalse(TEXT("auto reload off stored"), UMDHUDOptions::GetAutoReload());

	UMDHUDOptions::SetOffscreenThreat(false);
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("md.UI.OffscreenThreat")))
	{
		TestEqual(TEXT("off-screen threat toggle drives the cvar"), Var->GetInt(), 0);
		UMDHUDOptions::SetOffscreenThreat(true);
		TestEqual(TEXT("and back on"), Var->GetInt(), 1);
	}
	else
	{
		AddWarning(TEXT("md.UI.OffscreenThreat is not registered; cvar drive not checked"));
	}

	UMDHUDOptions::SetAimAssistStrength(OldAssist);
	UMDHUDOptions::SetAimToggle(OldToggle);
	UMDHUDOptions::SetReticleScale(OldScale);
	UMDHUDOptions::SetAutoReload(OldAuto);
	UMDHUDOptions::SetOffscreenThreat(OldThreat);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
