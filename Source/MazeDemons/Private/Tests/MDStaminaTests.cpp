#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDGameplayTags.h"
#include "MDMeleeAbility.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"

#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

namespace
{
	// Transient world with an AMDPlayerState whose ASC is initialised with the default stamina pool.
	struct FMDStaminaFixture
	{
		UWorld* World = nullptr;
		AMDPlayerState* PS = nullptr;
		UMDAbilitySystemComponent* ASC = nullptr;
		UMDStaminaComponent* Stamina = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDStaminaTestWorld")));
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
			if (!ASC || !Stamina)
			{
				Test.AddError(TEXT("PlayerState is missing its ASC or stamina component"));
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

		~FMDStaminaFixture()
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

	FMDStaminaStepInput BaseInput()
	{
		FMDStaminaStepInput In;
		In.Stamina = 100.0f;
		In.MaxStamina = 100.0f;
		In.RegenPerSec = 20.0f;
		In.RegenDelay = 1.0f;
		In.SprintDrainPerSec = 15.0f;
		In.ExhaustedRecoverFraction = 0.3f;
		In.DeltaTime = 1.0f;
		In.TimeSinceSpend = 10.0f;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaSprintDrainsTest, "MazeDemons.Stamina.SprintDrains", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaSprintDrainsTest::RunTest(const FString& Parameters)
{
	FMDStaminaStepInput In = BaseInput();
	In.bSprinting = true;
	const FMDStaminaStepOutput Out = UMDStaminaComponent::Step(In);
	TestEqual(TEXT("one second of sprint drains 15"), Out.Stamina, 85.0f);
	TestTrue(TEXT("a drain step restarts the regen delay"), Out.bSpent);
	TestFalse(TEXT("not exhausted yet"), Out.bExhausted);

	In.Stamina = 10.0f;
	const FMDStaminaStepOutput Empty = UMDStaminaComponent::Step(In);
	TestEqual(TEXT("stamina never goes below 0"), Empty.Stamina, 0.0f);
	TestTrue(TEXT("hitting 0 exhausts"), Empty.bExhausted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaRegenDelayTest, "MazeDemons.Stamina.RegenAfterDelay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaRegenDelayTest::RunTest(const FString& Parameters)
{
	FMDStaminaStepInput In = BaseInput();
	In.Stamina = 50.0f;
	In.TimeSinceSpend = 0.5f;
	TestEqual(TEXT("no regen inside the delay"), UMDStaminaComponent::Step(In).Stamina, 50.0f);

	In.TimeSinceSpend = 1.0f;
	TestEqual(TEXT("regen once the delay has passed"), UMDStaminaComponent::Step(In).Stamina, 70.0f);

	In.Stamina = 95.0f;
	TestEqual(TEXT("regen clamps at max"), UMDStaminaComponent::Step(In).Stamina, 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaHysteresisTest, "MazeDemons.Stamina.ExhaustHysteresis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaHysteresisTest::RunTest(const FString& Parameters)
{
	FMDStaminaStepInput In = BaseInput();
	In.Stamina = 0.0f;
	In.bExhausted = true;
	In.DeltaTime = 1.0f;

	FMDStaminaStepOutput Out = UMDStaminaComponent::Step(In); // 0 -> 20
	TestEqual(TEXT("regen while exhausted"), Out.Stamina, 20.0f);
	TestTrue(TEXT("still exhausted below 30%"), Out.bExhausted);

	In.Stamina = Out.Stamina;
	In.bExhausted = Out.bExhausted;
	Out = UMDStaminaComponent::Step(In); // 20 -> 40
	TestEqual(TEXT("regen continues"), Out.Stamina, 40.0f);
	TestFalse(TEXT("recovered at or above 30%"), Out.bExhausted);

	// An exhausted player that still has speed must not drain further.
	In.Stamina = 0.0f;
	In.bExhausted = true;
	In.bSprinting = true;
	TestFalse(TEXT("exhausted sprint does not spend"), UMDStaminaComponent::Step(In).bSpent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaJumpVaultTest, "MazeDemons.Stamina.JumpVaultConsume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaJumpVaultTest::RunTest(const FString& Parameters)
{
	FMDStaminaFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	TestEqual(TEXT("starts full"), Fx.ASC->GetStamina(), 100.0f);
	TestTrue(TEXT("can jump when full"), Fx.Stamina->CanJump());
	TestTrue(TEXT("can vault when full"), Fx.Stamina->CanVault());

	Fx.Stamina->NotifyJumped();
	TestEqual(TEXT("jump costs 10"), Fx.ASC->GetStamina(), 90.0f);
	Fx.Stamina->NotifyTraversalStarted();
	TestEqual(TEXT("vault costs 15"), Fx.ASC->GetStamina(), 75.0f);

	// The server never refuses a started action: it charges, clamped at 0, and flags exhaustion.
	Fx.ASC->SetStaminaClamped(5.0f);
	Fx.Stamina->NotifyTraversalStarted();
	TestEqual(TEXT("charge clamps at 0"), Fx.ASC->GetStamina(), 0.0f);
	TestTrue(TEXT("0 stamina exhausts"), Fx.Stamina->IsExhausted());
	TestFalse(TEXT("cannot jump while exhausted"), Fx.Stamina->CanJump());
	TestFalse(TEXT("cannot vault while exhausted"), Fx.Stamina->CanVault());
	TestFalse(TEXT("cannot sprint while exhausted"), Fx.Stamina->IsSprintAllowed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaClampOnMaxTest, "MazeDemons.Stamina.ClampOnMaxChange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaClampOnMaxTest::RunTest(const FString& Parameters)
{
	FMDStaminaFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	UMDAttributeSet* Attrs = Fx.ASC->GetMazeDemonsAttributes();
	Attrs->SetMaxStamina(60.0f);
	TestEqual(TEXT("lowering the max pulls stamina down"), Fx.ASC->GetStamina(), 60.0f);
	Attrs->SetMaxStamina(150.0f);
	TestEqual(TEXT("raising the max keeps current stamina"), Fx.ASC->GetStamina(), 60.0f);
	Fx.ASC->SetStaminaClamped(500.0f);
	TestEqual(TEXT("stamina cannot exceed the max"), Fx.ASC->GetStamina(), 150.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaRespawnTest, "MazeDemons.Stamina.RespawnRefill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaRespawnTest::RunTest(const FString& Parameters)
{
	FMDStaminaFixture Fx;
	if (!Fx.Init(*this))
	{
		return true;
	}
	Fx.ASC->SetStaminaClamped(0.0f);
	Fx.Stamina->Consume(1.0f);
	TestTrue(TEXT("drained and exhausted"), Fx.Stamina->IsExhausted());

	Fx.ASC->ResetStaminaToMax();
	Fx.Stamina->ResetState();
	TestEqual(TEXT("respawn refills stamina"), Fx.ASC->GetStamina(), Fx.ASC->GetMaxStamina());
	TestFalse(TEXT("respawn clears exhaustion"), Fx.Stamina->IsExhausted());
	TestTrue(TEXT("can jump again"), Fx.Stamina->CanJump());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDStaminaMeleeFreeTest, "MazeDemons.Stamina.MeleeHasNoStaminaCost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDStaminaMeleeFreeTest::RunTest(const FString& Parameters)
{
	const UMDMeleeAbility* Melee = GetDefault<UMDMeleeAbility>();
	if (!TestNotNull(TEXT("melee ability CDO"), Melee))
	{
		return false;
	}
	TestNull(TEXT("melee has no cost effect"), Melee->GetCostGameplayEffect());

	// Attacks must never be blocked by exhaustion.
	const FStructProperty* BlockedProp = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), TEXT("ActivationBlockedTags"));
	if (TestNotNull(TEXT("ActivationBlockedTags property"), BlockedProp))
	{
		const FGameplayTagContainer* Blocked = BlockedProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Melee);
		TestFalse(TEXT("melee is not blocked by State.Exhausted"), Blocked->HasTag(TAG_State_Exhausted.GetTag()));
		TestFalse(TEXT("melee is not blocked by State.Sprinting"), Blocked->HasTag(TAG_State_Sprinting.GetTag()));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
