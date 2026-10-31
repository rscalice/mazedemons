#include "Misc/AutomationTest.h"

#include "AI/MDEnemyArchetype.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "MDCombatNotifies.h"

#if WITH_DEV_AUTOMATION_TESTS

// Attack selection with a minimum range (the leap) and weights (the combo is the main attack), plus the nimble facing rule.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDGhoulSelectionRulesTest, "MazeDemons.AI.Ghoul.AttackSelectionAndFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDGhoulSelectionRulesTest::RunTest(const FString& Parameters)
{
	UMDEnemyArchetype* A = NewObject<UMDEnemyArchetype>(GetTransientPackage());
	FMDEnemyAttack Combo; Combo.Name = TEXT("ScratchCombo"); Combo.Range = 200.0f; Combo.SelectionWeight = 3.0f;
	FMDEnemyAttack Claw; Claw.Name = TEXT("ClawHeavy"); Claw.Range = 190.0f; Claw.SelectionWeight = 1.0f;
	FMDEnemyAttack Leap; Leap.Name = TEXT("LeapScratch"); Leap.MinRange = 300.0f; Leap.Range = 480.0f; Leap.SelectionWeight = 1.0f;
	A->Attacks = { Combo, Claw, Leap };

	TestEqual(TEXT("melee: tightest is the claw"), A->PickAttack(150.0f), 1);
	TestEqual(TEXT("leap not chosen inside its min range"), A->PickAttack(250.0f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("dead zone 260 cm: nothing"), A->PickAttack(260.0f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("leap at 300"), A->PickAttack(300.0f), 2);
	TestEqual(TEXT("leap at 480"), A->PickAttack(480.0f), 2);
	TestEqual(TEXT("beyond the leap: none"), A->PickAttack(481.0f), static_cast<int32>(INDEX_NONE));

	TestEqual(TEXT("weighted melee low roll: combo"), A->PickAttackWeighted(150.0f, 0.1f), 0);
	TestEqual(TEXT("weighted melee mid roll: combo (3 of 4)"), A->PickAttackWeighted(150.0f, 0.7f), 0);
	TestEqual(TEXT("weighted melee high roll: claw"), A->PickAttackWeighted(150.0f, 0.9f), 1);
	TestEqual(TEXT("weighted 195 cm: combo only"), A->PickAttackWeighted(195.0f, 0.99f), 0);
	TestEqual(TEXT("weighted 400 cm: leap only"), A->PickAttackWeighted(400.0f, 0.0f), 2);
	TestEqual(TEXT("weighted out of reach: none"), A->PickAttackWeighted(700.0f, 0.5f), static_cast<int32>(INDEX_NONE));
	A->Attacks[0].SelectionWeight = 0.0f; A->Attacks[1].SelectionWeight = 0.0f; A->Attacks[2].SelectionWeight = 0.0f;
	TestEqual(TEXT("no weights: tightest wins"), A->PickAttackWeighted(150.0f, 0.9f), 1);

	// Nimble facing: engaged inside the range faces the target; outside, no target, rule off, or mid-attack orients to movement.
	TestTrue(TEXT("engaged close: face target"), UMDEnemyArchetype::ShouldFaceTarget(true, 400.0f, 600.0f, false));
	TestTrue(TEXT("edge of the range: face target"), UMDEnemyArchetype::ShouldFaceTarget(true, 600.0f, 600.0f, false));
	TestFalse(TEXT("outside: orient to movement"), UMDEnemyArchetype::ShouldFaceTarget(true, 601.0f, 600.0f, false));
	TestFalse(TEXT("no target: orient to movement"), UMDEnemyArchetype::ShouldFaceTarget(false, 100.0f, 600.0f, false));
	TestFalse(TEXT("rule off"), UMDEnemyArchetype::ShouldFaceTarget(true, 100.0f, 0.0f, false));
	TestFalse(TEXT("attacking: the attack owns facing"), UMDEnemyArchetype::ShouldFaceTarget(true, 100.0f, 600.0f, true));
	return true;
}

// The real Ghoul asset: nimble numbers, three montages with tell + trace windows, PSD with 17 entries.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDGhoulDataValidationTest, "MazeDemons.AI.Ghoul.DataValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDGhoulDataValidationTest::RunTest(const FString& Parameters)
{
	const UMDEnemyArchetype* G = LoadObject<UMDEnemyArchetype>(nullptr, TEXT("/Game/Data/Enemies/DA_Archetype_Ghoul.DA_Archetype_Ghoul"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!TestNotNull(TEXT("ghoul archetype"), G))
	{
		return false;
	}
	TestEqual(TEXT("quick: run speed"), G->MoveSpeed, 520.0f);
	TestEqual(TEXT("walk speed"), G->WalkSpeed, 150.0f);
	TestEqual(TEXT("acceleration"), G->MaxAcceleration, 2400.0f);
	TestEqual(TEXT("braking"), G->BrakingDeceleration, 2000.0f);
	TestEqual(TEXT("turn rate"), G->RotationYawRate, 720.0f);
	TestEqual(TEXT("engage face range 6 m"), G->EngageFaceRange, 600.0f);
	TestEqual(TEXT("three attacks"), G->Attacks.Num(), 3);

	const TCHAR* Names[] = { TEXT("ScratchCombo"), TEXT("ClawHeavy"), TEXT("LeapScratch") };
	for (int32 i = 0; i < G->Attacks.Num() && i < 3; ++i)
	{
		const FMDEnemyAttack& Attack = G->Attacks[i];
		TestEqual(TEXT("attack name"), Attack.Name.ToString(), FString(Names[i]));
		if (!TestNotNull(*FString::Printf(TEXT("%s montage"), Names[i]), Attack.Montage.Get()))
		{
			continue;
		}
		int32 Windows = 0, Tells = 0;
		for (const FAnimNotifyEvent& Event : Attack.Montage->Notifies)
		{
			Windows += Event.NotifyStateClass && Event.NotifyStateClass->IsA<UMDANS_WeaponTrace>() ? 1 : 0;
			Tells += Event.Notify && Event.Notify->GetClass()->GetName().Contains(TEXT("MDAN_Tell")) ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("%s has trace windows"), Names[i]), Windows >= (i == 0 ? 2 : 1));
		TestEqual(*FString::Printf(TEXT("%s has a tell"), Names[i]), Tells, 1);
		TestTrue(*FString::Printf(TEXT("%s claws"), Names[i]), Attack.TraceSockets.Contains(FName(TEXT("hand_r"))) && Attack.TraceSockets.Contains(FName(TEXT("hand_l"))));
	}
	if (G->Attacks.Num() == 3)
	{
		TestEqual(TEXT("leap min range"), G->Attacks[2].MinRange, 300.0f);
		TestEqual(TEXT("leap max range"), G->Attacks[2].Range, 480.0f);
		TestEqual(TEXT("melee at 150 uses a melee attack"), G->PickAttack(150.0f) != 2, true);
		TestEqual(TEXT("leap at 400"), G->PickAttackWeighted(400.0f, 0.5f), 2);
	}

	// PSD_Ghoul: 16 locomotion clips + idle.
	UObject* Psd = StaticLoadObject(UObject::StaticClass(), nullptr, TEXT("/Game/Animations/Enemies/Ghoul/MM/PSD_Ghoul.PSD_Ghoul"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (TestNotNull(TEXT("PSD_Ghoul"), Psd))
	{
		int32 Count = -1;
		if (const FArrayProperty* Prop = CastField<FArrayProperty>(Psd->GetClass()->FindPropertyByName(TEXT("DatabaseAnimationAssets"))))
		{
			FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Psd));
			Count = Helper.Num();
		}
		TestEqual(TEXT("PSD_Ghoul has 17 entries"), Count, 17);
	}
	return true;
}

#endif
