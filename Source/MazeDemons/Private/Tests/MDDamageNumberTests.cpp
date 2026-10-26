#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyArchetype.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatTypes.h"
#include "MDDamageNumberWidget.h"
#include "MDDamageNumbers.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"

// C1 follow-up: floating damage numbers on every creature, colored by damage type (MazeDemons.Combat.DamageNumbers.*).

namespace MDDamageNumberTestsLocal
{
	bool SameColor(const FLinearColor& A, const FLinearColor& B)
	{
		return A.Equals(B, 0.001f);
	}

	// Transient game world, destroyed with the fixture.
	struct FWorldFixture
	{
		UWorld* World = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDDamageNumberTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FWorldFixture()
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

// Physical white, Frost icy blue, Fire orange, Poison green, Bleed crimson pink; an empty or unrelated tag is Physical.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDamageNumberTypeColorTest, "MazeDemons.Combat.DamageNumbers.TypeColor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDamageNumberTypeColorTest::RunTest(const FString& Parameters)
{
	using namespace MDDamageNumberTestsLocal;
	const FLinearColor Physical = MDDamageNumbers::ResolveColor(TAG_Damage_Type_Physical.GetTag(), false);
	const FLinearColor Frost = MDDamageNumbers::ResolveColor(TAG_Damage_Type_Frost.GetTag(), false);
	const FLinearColor Fire = MDDamageNumbers::ResolveColor(TAG_Damage_Type_Fire.GetTag(), false);
	const FLinearColor Poison = MDDamageNumbers::ResolveColor(TAG_Damage_Type_Poison.GetTag(), false);
	const FLinearColor Bleed = MDDamageNumbers::ResolveColor(TAG_Damage_Type_Bleed.GetTag(), false);

	TestTrue(TEXT("physical is white"), SameColor(Physical, FLinearColor::White));
	TestTrue(TEXT("frost is blue-dominant"), Frost.B > Frost.R && Frost.B > 0.9f && Frost.G > Frost.R);
	TestTrue(TEXT("fire is orange"), Fire.R > 0.9f && Fire.G > 0.3f && Fire.G < 0.7f && Fire.B < 0.3f);
	TestTrue(TEXT("poison is green-dominant"), Poison.G > Poison.R && Poison.G > Poison.B && Poison.G > 0.8f);
	TestTrue(TEXT("bleed is crimson pink (red-dominant with some blue)"), Bleed.R > 0.8f && Bleed.G < 0.3f && Bleed.B > 0.25f);

	const FLinearColor All[] = { Physical, Frost, Fire, Poison, Bleed };
	for (int32 i = 0; i < UE_ARRAY_COUNT(All); ++i)
	{
		for (int32 j = i + 1; j < UE_ARRAY_COUNT(All); ++j)
		{
			TestFalse(FString::Printf(TEXT("types %d and %d have different colors"), i, j), SameColor(All[i], All[j]));
		}
	}

	TestTrue(TEXT("no tag -> physical white"), SameColor(MDDamageNumbers::ResolveColor(FGameplayTag(), false), FLinearColor::White));
	TestTrue(TEXT("an unrelated tag -> physical white"), SameColor(MDDamageNumbers::ResolveColor(TAG_Data_Damage.GetTag(), false), FLinearColor::White));
	TestTrue(TEXT("resolve of no tag is Physical"), MDDamageNumbers::ResolveType(FGameplayTag()) == TAG_Damage_Type_Physical.GetTag());
	TestTrue(TEXT("resolve keeps Fire"), MDDamageNumbers::ResolveType(TAG_Damage_Type_Fire.GetTag()) == TAG_Damage_Type_Fire.GetTag());

	// Data override: a listed type changes, the others keep their default.
	TMap<FGameplayTag, FLinearColor> Overrides;
	Overrides.Add(TAG_Damage_Type_Frost.GetTag(), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f));
	TestTrue(TEXT("override replaces frost"), SameColor(MDDamageNumbers::ResolveColor(TAG_Damage_Type_Frost.GetTag(), false, &Overrides), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)));
	TestTrue(TEXT("fire keeps its default beside an override"), SameColor(MDDamageNumbers::ResolveColor(TAG_Damage_Type_Fire.GetTag(), false, &Overrides), Fire));
	return true;
}

// A crit is red whatever the type, and that red is not any of the type colors (bleed in particular).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDamageNumberCritOverrideTest, "MazeDemons.Combat.DamageNumbers.CritOverridesType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDamageNumberCritOverrideTest::RunTest(const FString& Parameters)
{
	using namespace MDDamageNumberTestsLocal;
	const FLinearColor Crit = MDDamageNumbers::DefaultCritColor();
	TestTrue(TEXT("crit is red"), Crit.R > 0.9f && Crit.G < 0.2f && Crit.B < 0.2f);

	const FGameplayTag Types[] = {
		FGameplayTag(),
		TAG_Damage_Type_Physical.GetTag(),
		TAG_Damage_Type_Frost.GetTag(),
		TAG_Damage_Type_Fire.GetTag(),
		TAG_Damage_Type_Poison.GetTag(),
		TAG_Damage_Type_Bleed.GetTag(),
	};
	for (const FGameplayTag& Type : Types)
	{
		TestTrue(FString::Printf(TEXT("crit of '%s' is the crit red"), *Type.ToString()), SameColor(MDDamageNumbers::ResolveColor(Type, true), Crit));
		if (Type.IsValid())
		{
			TestFalse(FString::Printf(TEXT("the crit red is not the '%s' color"), *Type.ToString()), SameColor(MDDamageNumbers::DefaultTypeColor(Type), Crit));
		}
	}

	TMap<FGameplayTag, FLinearColor> Overrides;
	Overrides.Add(TAG_Damage_Type_Fire.GetTag(), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
	TestTrue(TEXT("a type override does not touch the crit color"), SameColor(MDDamageNumbers::ResolveColor(TAG_Damage_Type_Fire.GetTag(), true, &Overrides), Crit));
	const FLinearColor CustomCrit(0.5f, 0.0f, 0.5f, 1.0f);
	TestTrue(TEXT("the crit color can be overridden"), SameColor(MDDamageNumbers::ResolveColor(TAG_Damage_Type_Fire.GetTag(), true, &Overrides, &CustomCrit), CustomCrit));
	return true;
}

// Enemies (AMDEnemyCharacter) get numbers like anything with an ability system; null and plain actors do not. The native widget
// builds its text when it is created (it used to build it only once on screen, so Start() never had a text block to fill).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDamageNumberEligibilityTest, "MazeDemons.Combat.DamageNumbers.EnemiesEligible", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDamageNumberEligibilityTest::RunTest(const FString& Parameters)
{
	using namespace MDDamageNumberTestsLocal;
	FWorldFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = Fx.World->SpawnActor<AMDEnemyCharacter>();
	AActor* Plain = Fx.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("enemy"), Enemy) || !TestNotNull(TEXT("plain actor"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("an enemy is eligible"), MDDamageNumbers::IsEligibleVictim(Enemy));
	TestFalse(TEXT("a plain actor is not"), MDDamageNumbers::IsEligibleVictim(Plain));
	TestFalse(TEXT("null is not"), MDDamageNumbers::IsEligibleVictim(nullptr));

	UMDDamageNumberWidget* Widget = CreateWidget<UMDDamageNumberWidget>(Fx.World, UMDDamageNumberWidget::StaticClass());
	if (TestNotNull(TEXT("widget"), Widget))
	{
		TestTrue(TEXT("the widget has its text block straight after creation"), Widget->HasText());
	}
	return true;
}

// The damage type travels with the hit: a context type (or its step's) comes out of ApplyDamage on the result, and a hit that
// carries none is Physical.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDamageNumberTypeFlowTest, "MazeDemons.Combat.DamageNumbers.TypeFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDDamageNumberTypeFlowTest::RunTest(const FString& Parameters)
{
	using namespace MDDamageNumberTestsLocal;
	FWorldFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = Fx.World->SpawnActor<AMDEnemyCharacter>();
	APawn* AttackerPawn = Fx.World->SpawnActor<APawn>();
	AMDPlayerState* AttackerState = Fx.World->SpawnActor<AMDPlayerState>();
	if (!Enemy || !AttackerPawn || !AttackerState)
	{
		AddError(TEXT("Could not spawn the enemy, the attacker pawn or its player state"));
		return false;
	}
	AttackerState->SetRole(ROLE_Authority);
	AttackerPawn->SetPlayerState(AttackerState);
	UMDAbilitySystemComponent* AttackerASC = AttackerState->GetMDAbilitySystemComponent();
	if (!TestNotNull(TEXT("attacker ASC"), AttackerASC))
	{
		return false;
	}
	AttackerASC->InitAbilityActorInfo(AttackerState, AttackerPawn);
	AttackerASC->RegisterAttributeSet(AttackerState->Attributes);
	AttackerASC->InitializeStats(100.0f, 5.0f, 0.0f);

	UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
	Archetype->ArchetypeTag = TAG_Enemy_Ghoul.GetTag();
	Archetype->MaxHealth = 500.0f;
	Archetype->MaxPoise = 30.0f;
	Enemy->ApplyArchetype(Archetype);

	UMDAbilitySystemComponent* EnemyASC = Enemy->AbilitySystem;
	if (!TestNotNull(TEXT("enemy ASC"), EnemyASC))
	{
		return false;
	}

	FMDHitContext Context;
	Context.Instigator = AttackerPawn;
	Context.Causer = AttackerPawn;
	Context.BaseDamage = 5.0f;

	FMDDamageResult Result;
	TestTrue(TEXT("a plain hit lands"), EnemyASC->ApplyDamage(Context, Result));
	TestTrue(TEXT("a hit with no type is Physical"), Result.DamageType == TAG_Damage_Type_Physical.GetTag());

	Context.DamageType = TAG_Damage_Type_Frost.GetTag();
	TestTrue(TEXT("a frost hit lands"), EnemyASC->ApplyDamage(Context, Result));
	TestTrue(TEXT("the context type reaches the result"), Result.DamageType == TAG_Damage_Type_Frost.GetTag());

	Context.DamageType = FGameplayTag();
	Context.Step.DamageType = TAG_Damage_Type_Fire.GetTag();
	TestTrue(TEXT("a fire step lands"), EnemyASC->ApplyDamage(Context, Result));
	TestTrue(TEXT("the step type reaches the result"), Result.DamageType == TAG_Damage_Type_Fire.GetTag());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
