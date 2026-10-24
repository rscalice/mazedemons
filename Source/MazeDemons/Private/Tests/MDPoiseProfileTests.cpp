#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDCombatTypes.h"
#include "MDPhysicalHitReactComponent.h"
#include "MDPoiseProfile.h"

#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPoiseProfilesTest, "MazeDemons.Combat.PoiseProfiles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDPoiseProfilesTest::RunTest(const FString& Parameters)
{
	const UMDPoiseProfile* PlayerProfile = LoadObject<UMDPoiseProfile>(nullptr, TEXT("/Game/Data/Combat/DA_Poise_Player.DA_Poise_Player"));
	const UMDPoiseProfile* DummyProfile = LoadObject<UMDPoiseProfile>(nullptr, TEXT("/Game/Data/Combat/DA_Poise_Dummy.DA_Poise_Dummy"));
	if (!TestNotNull(TEXT("DA_Poise_Player loads"), PlayerProfile) || !TestNotNull(TEXT("DA_Poise_Dummy loads"), DummyProfile))
	{
		return false;
	}

	TestEqual(TEXT("Player MaxPoise"), PlayerProfile->MaxPoise, 50.0f);
	TestEqual(TEXT("Player RegenDelay"), PlayerProfile->RegenDelay, 2.0f);
	TestEqual(TEXT("Player RegenPerSec"), PlayerProfile->RegenPerSec, 25.0f);
	TestEqual(TEXT("Player StunDuration"), PlayerProfile->StunDuration, 0.6f);
	TestEqual(TEXT("Player StaggerImmunityAfterStun"), PlayerProfile->StaggerImmunityAfterStun, 1.5f);

	TestEqual(TEXT("Dummy MaxPoise"), DummyProfile->MaxPoise, 60.0f);
	TestEqual(TEXT("Dummy RegenDelay"), DummyProfile->RegenDelay, 2.0f);
	TestEqual(TEXT("Dummy RegenPerSec"), DummyProfile->RegenPerSec, 30.0f);
	TestEqual(TEXT("Dummy StunDuration"), DummyProfile->StunDuration, 1.2f);
	TestEqual(TEXT("Dummy StaggerImmunityAfterStun"), DummyProfile->StaggerImmunityAfterStun, 2.0f);

	const EMDHitDirection Directions[] = { EMDHitDirection::Front, EMDHitDirection::Back, EMDHitDirection::Left, EMDHitDirection::Right };
	for (const UMDPoiseProfile* Profile : { PlayerProfile, DummyProfile })
	{
		const FString Who = (Profile == PlayerProfile) ? TEXT("Player") : TEXT("Dummy");
		TSet<const UAnimMontage*> Distinct;
		for (const EMDHitDirection Direction : Directions)
		{
			const UAnimMontage* Stagger = Profile->FindMontage(EMDHitReaction::Stagger, Direction);
			TestNotNull(*FString::Printf(TEXT("%s Stagger montage, direction %d"), *Who, static_cast<int32>(Direction)), Stagger);
			if (Stagger)
			{
				Distinct.Add(Stagger);
			}
			// Stronger reactions without their own montage fall back to Stagger.
			if (Profile == PlayerProfile)
			{
				TestEqual(TEXT("Player Knockback falls back to Stagger"), Profile->FindMontage(EMDHitReaction::Knockback, Direction), const_cast<UAnimMontage*>(Stagger));
			}
		}
		TestEqual(*FString::Printf(TEXT("%s has four distinct stagger montages"), *Who), Distinct.Num(), 4);
		TestNull(*FString::Printf(TEXT("%s Flinch has no montage (physics handles it)"), *Who), Profile->FindMontage(EMDHitReaction::Flinch, EMDHitDirection::Front));
	}

	TestNotNull(TEXT("Dummy Knockback/Back montage"), DummyProfile->FindMontage(EMDHitReaction::Knockback, EMDHitDirection::Back));
	TestNotNull(TEXT("Dummy Knockback montage from the front still resolves"), DummyProfile->FindMontage(EMDHitReaction::Knockback, EMDHitDirection::Front));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDPhysicalReactBlendTest, "MazeDemons.Combat.PhysicalReactionBlend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDPhysicalReactBlendTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Blend starts at 0"), UMDPhysicalHitReactComponent::EvaluateBlend(0.0f, 0.25f, 0.25f, 0.5f), 0.0f);
	TestEqual(TEXT("Blend peaks at BlendIn"), UMDPhysicalHitReactComponent::EvaluateBlend(0.25f, 0.25f, 0.25f, 0.5f), 0.5f, 0.001f);
	TestEqual(TEXT("Blend ends at 0"), UMDPhysicalHitReactComponent::EvaluateBlend(0.5f, 0.25f, 0.25f, 0.5f), 0.0f);
	TestTrue(TEXT("Blend rises"), UMDPhysicalHitReactComponent::EvaluateBlend(0.1f, 0.25f, 0.25f, 0.5f) < UMDPhysicalHitReactComponent::EvaluateBlend(0.2f, 0.25f, 0.25f, 0.5f));

	const FVector Forward = FVector::ForwardVector;
	const FVector Right = FVector::RightVector;
	TestEqual(TEXT("Front hit pushes backwards"), UMDPhysicalHitReactComponent::ImpulseDirection(Forward, Right, EMDHitDirection::Front), -Forward);
	TestEqual(TEXT("Back hit pushes forwards"), UMDPhysicalHitReactComponent::ImpulseDirection(Forward, Right, EMDHitDirection::Back), Forward);
	TestEqual(TEXT("Left hit pushes right"), UMDPhysicalHitReactComponent::ImpulseDirection(Forward, Right, EMDHitDirection::Left), Right);
	TestEqual(TEXT("Right hit pushes left"), UMDPhysicalHitReactComponent::ImpulseDirection(Forward, Right, EMDHitDirection::Right), -Right);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
