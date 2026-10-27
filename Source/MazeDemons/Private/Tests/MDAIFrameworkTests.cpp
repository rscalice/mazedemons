#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDAITypes.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttributeSet.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDTargetableComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatStatics.h"
#include "MDGameSettings.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDInventoryTypes.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"
#include "MDPoiseProfile.h"
#include "MDQuestTypes.h"
#include "MDStatCalculator.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"

// V1 WP0 enemy framework tests (MazeDemons.AI.Framework.*). The rules are plain functions; the world tests use a transient world.

namespace MDAIFrameworkTestsLocal
{
	UMDEnemyArchetype* MakeArchetype(const FGameplayTag& Tag, float Health, float Poise, int32 Gold)
	{
		UMDEnemyArchetype* Archetype = NewObject<UMDEnemyArchetype>(GetTransientPackage());
		Archetype->ArchetypeTag = Tag;
		Archetype->MaxHealth = Health;
		Archetype->MaxPoise = Poise;
		Archetype->KillGold = Gold;
		Archetype->MoveSpeed = 520.0f;
		return Archetype;
	}

	// Transient world with an authoritative enemy and a player-team attacker (an AMDPlayerState-backed pawn).
	struct FEnemyFixture
	{
		UWorld* World = nullptr;
		AMDEnemyCharacter* Enemy = nullptr;
		APawn* AttackerPawn = nullptr;
		AMDPlayerState* AttackerState = nullptr;
		UMDEnemyArchetype* Archetype = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDAIFrameworkTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());

			Enemy = World->SpawnActor<AMDEnemyCharacter>();
			AttackerPawn = World->SpawnActor<APawn>();
			AttackerState = World->SpawnActor<AMDPlayerState>();
			if (!Enemy || !AttackerPawn || !AttackerState)
			{
				Test.AddError(TEXT("Could not spawn the enemy, the attacker pawn or its player state"));
				return false;
			}
			AttackerState->SetRole(ROLE_Authority);
			AttackerPawn->SetPlayerState(AttackerState);
			UMDAbilitySystemComponent* AttackerASC = AttackerState->GetMDAbilitySystemComponent();
			if (!AttackerASC)
			{
				Test.AddError(TEXT("Attacker player state has no ASC"));
				return false;
			}
			AttackerASC->InitAbilityActorInfo(AttackerState, AttackerPawn);
			AttackerASC->RegisterAttributeSet(AttackerState->Attributes);
			AttackerASC->InitializeStats(100.0f, 10.0f, 0.0f);

			Archetype = MakeArchetype(TAG_Enemy_Ghoul.GetTag(), 60.0f, 30.0f, 5);
			Enemy->ApplyArchetype(Archetype);
			return true;
		}

		~FEnemyFixture()
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

// A Ghoul-like enemy takes the archetype's stats, takes player hits through the C1 pipeline (ApplyMeleeHit) and dies once:
// ragdoll state, no more targeting, a corpse lifespan.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIFrameworkSpawnAndKillTest, "MazeDemons.AI.Framework.SpawnAndKill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIFrameworkSpawnAndKillTest::RunTest(const FString& Parameters)
{
	using namespace MDAIFrameworkTestsLocal;
	FEnemyFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	AMDEnemyCharacter* Enemy = Fx.Enemy;
	UMDAbilitySystemComponent* ASC = Enemy->AbilitySystem;
	if (!TestNotNull(TEXT("enemy ASC"), ASC) || !TestNotNull(TEXT("enemy attribute set"), Enemy->Attributes.Get()))
	{
		return false;
	}

	TestTrue(TEXT("the ASC finds the enemy attribute set as the MazeDemons one"), ASC->GetMazeDemonsAttributes() == Enemy->Attributes);
	TestEqual(TEXT("health from the archetype"), ASC->GetHealth(), 60.0f);
	TestEqual(TEXT("max health from the archetype"), ASC->GetMaxHealth(), 60.0f);
	TestEqual(TEXT("max poise attribute from the archetype"), Enemy->Attributes->GetMaxPoise(), 30.0f);
	TestEqual(TEXT("poise component max poise from the archetype"), Enemy->Poise->GetMaxPoise(), 30.0f);
	TestEqual(TEXT("move speed from the archetype"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 520.0f);
	TestEqual(TEXT("replicated intent starts Idle"), static_cast<int32>(Enemy->ReplicatedIntent), static_cast<int32>(EMDIntent::Idle));
	TestTrue(TEXT("targetable while alive"), UMDTargetableComponent::IsActorTargetable(Enemy));
	TestFalse(TEXT("not dead yet"), Enemy->IsEnemyDead());

	FGameplayTagContainer Tags;
	Enemy->GetOwnedGameplayTags(Tags);
	TestTrue(TEXT("carries Enemy.Ghoul"), Tags.HasTagExact(TAG_Enemy_Ghoul.GetTag()));

	// Player hits through the C1 entry point: 10 attack power per default light step, 60 health.
	const FHitResult NoHit;
	TestTrue(TEXT("first hit lands"), UMDCombatStatics::ApplyMeleeHit(Fx.AttackerPawn, Enemy, NoHit));
	TestTrue(TEXT("health went down"), ASC->GetHealth() < 60.0f);
	TestFalse(TEXT("one hit does not kill"), Enemy->IsEnemyDead());

	int32 Guard = 0;
	while (!ASC->IsDead() && Guard++ < 20)
	{
		UMDCombatStatics::ApplyMeleeHit(Fx.AttackerPawn, Enemy, NoHit);
	}
	TestTrue(TEXT("hits kill the enemy"), ASC->IsDead());
	TestTrue(TEXT("death handled once"), Enemy->IsEnemyDead());
	TestFalse(TEXT("no longer targetable"), UMDTargetableComponent::IsActorTargetable(Enemy));
	TestTrue(TEXT("corpse has a lifespan"), Enemy->GetLifeSpan() > 0.0f);
	TestTrue(TEXT("capsule collision off after the ragdoll"), Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("movement disabled after the ragdoll"), Enemy->GetCharacterMovement()->MovementMode == MOVE_None);
	TestFalse(TEXT("a dead enemy takes no more hits"), UMDCombatStatics::ApplyMeleeHit(Fx.AttackerPawn, Enemy, NoHit));
	return true;
}

// Kill gold is the archetype's KillGold through the one U2 rule (UMDStatCalculator::DistributeKillGold, 30 m): an even split,
// the remainder to the killer, far players excluded.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIFrameworkKillGoldSplitTest, "MazeDemons.AI.Framework.KillGoldSplit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIFrameworkKillGoldSplitTest::RunTest(const FString& Parameters)
{
	using namespace MDAIFrameworkTestsLocal;
	UMDEnemyArchetype* Ghoul = MakeArchetype(TAG_Enemy_Ghoul.GetTag(), 60.0f, 30.0f, 5);
	TestEqual(TEXT("ghoul gold"), Ghoul->GetKillGold(), 5);

	const float Radius = UMDGameSettings::GetEconomy().KillGoldRadius;
	TestEqual(TEXT("the economy radius is 30 m"), Radius, 3000.0f);

	// 3 players: killer (index 1) and one other within 30 m, one at 45 m. 5 gold -> 2 each, remainder 1 to the killer.
	TArray<float> Distances = { 1000.0f, 2500.0f, 4500.0f };
	TArray<int32> Shares;
	UMDStatCalculator::DistributeKillGold(Ghoul->GetKillGold(), Distances, Radius, 1, Shares);
	if (!TestEqual(TEXT("one share per player"), Shares.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("in-range non-killer gets the even share"), Shares[0], 2);
	TestEqual(TEXT("killer gets the share plus the remainder"), Shares[1], 3);
	TestEqual(TEXT("out-of-range player gets nothing"), Shares[2], 0);
	TestEqual(TEXT("shares sum to the kill gold"), Shares[0] + Shares[1] + Shares[2], 5);

	// The enemy reads the same number, and a missing archetype or negative gold awards nothing.
	{
		FEnemyFixture Fx;
		if (Fx.Init(*this))
		{
			TestEqual(TEXT("the enemy awards the archetype's gold"), Fx.Enemy->GetKillGoldAward(), 5);
			Fx.Archetype->KillGold = -3;
			TestEqual(TEXT("negative gold is clamped to 0"), Fx.Enemy->GetKillGoldAward(), 0);
			Fx.Archetype->KillGold = 5;

			// Killer lookup: pawn -> its controller (none here), null -> null.
			TestNull(TEXT("no killer controller for a controller-less pawn"), AMDEnemyCharacter::GetKillerController(Fx.AttackerPawn));
			TestNull(TEXT("no killer controller for null"), AMDEnemyCharacter::GetKillerController(nullptr));

			// With no game state in the test world the award is a safe no-op.
			UMDGameplayStatics::AwardKillGold(Fx.Enemy, nullptr, Fx.Enemy->GetKillGoldAward());
		}
	}
	return true;
}

// Kill credit: the archetype tag matches a Kill objective on Enemy.Ghoul, never another archetype's objective, and the kill only
// counts when the victim stands in the active quest's zone (the Zone 2 quest).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIFrameworkQuestCreditZoneOnlyTest, "MazeDemons.AI.Framework.QuestCreditZoneOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIFrameworkQuestCreditZoneOnlyTest::RunTest(const FString& Parameters)
{
	using namespace MDAIFrameworkTestsLocal;
	UMDEnemyArchetype* Ghoul = MakeArchetype(TAG_Enemy_Ghoul.GetTag(), 60.0f, 30.0f, 5);
	UMDEnemyArchetype* Archer = MakeArchetype(TAG_Enemy_BoneArcher.GetTag(), 40.0f, 20.0f, 6);

	FMDObjectiveDef Zone2Objective;
	Zone2Objective.Type = EMDObjectiveType::Kill;
	Zone2Objective.TargetTag = TAG_Enemy_Ghoul.GetTag();
	Zone2Objective.Required = 6;

	TestTrue(TEXT("a ghoul kill matches the Zone 2 objective"), UMDQuestRules::MatchesObjective(Zone2Objective, EMDObjectiveType::Kill, Ghoul->GetVictimTags()));
	TestFalse(TEXT("a bone archer kill does not"), UMDQuestRules::MatchesObjective(Zone2Objective, EMDObjectiveType::Kill, Archer->GetVictimTags()));
	TestFalse(TEXT("an archetype without a tag never matches"), UMDQuestRules::MatchesObjective(Zone2Objective, EMDObjectiveType::Kill, MakeArchetype(FGameplayTag(), 1.0f, 1.0f, 0)->GetVictimTags()));

	TestTrue(TEXT("kill in Zone 2 counts for the Zone 2 quest"), UMDQuestRules::KillCountsForZone(2, 2));
	TestFalse(TEXT("kill in Zone 1 does not"), UMDQuestRules::KillCountsForZone(1, 2));
	TestFalse(TEXT("kill in Zone 3 does not"), UMDQuestRules::KillCountsForZone(3, 2));
	TestFalse(TEXT("kill outside any zone does not"), UMDQuestRules::KillCountsForZone(0, 2));

	// The live enemy exposes the same tags the ASC's kill report collects (the report reads GetOwnedGameplayTags).
	FEnemyFixture Fx;
	if (Fx.Init(*this))
	{
		FGameplayTagContainer Reported;
		Fx.Enemy->GetOwnedGameplayTags(Reported);
		TestTrue(TEXT("the placed ghoul reports Enemy.Ghoul"), UMDQuestRules::MatchesObjective(Zone2Objective, EMDObjectiveType::Kill, Reported));
	}
	return true;
}

// One native tag per archetype, all under Enemy, all distinct; tier ranges and poise resolution on the data asset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIFrameworkTagsPerArchetypeTest, "MazeDemons.AI.Framework.TagsPerArchetype", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIFrameworkTagsPerArchetypeTest::RunTest(const FString& Parameters)
{
	using namespace MDAIFrameworkTestsLocal;
	const TArray<TPair<FGameplayTag, const TCHAR*>> Expected = {
		{ TAG_Enemy_Ghoul.GetTag(), TEXT("Enemy.Ghoul") },
		{ TAG_Enemy_BoneArcher.GetTag(), TEXT("Enemy.BoneArcher") },
		{ TAG_Enemy_GraveBrute.GetTag(), TEXT("Enemy.GraveBrute") },
		{ TAG_Enemy_Cultist.GetTag(), TEXT("Enemy.Cultist") },
		{ TAG_Enemy_Revenant.GetTag(), TEXT("Enemy.Revenant") },
		{ TAG_Enemy_Werewolf.GetTag(), TEXT("Enemy.Werewolf") },
	};
	TSet<FGameplayTag> Seen;
	for (const TPair<FGameplayTag, const TCHAR*>& Entry : Expected)
	{
		TestTrue(*FString::Printf(TEXT("%s is registered"), Entry.Value), Entry.Key.IsValid());
		TestEqual(*FString::Printf(TEXT("%s name"), Entry.Value), Entry.Key.ToString(), FString(Entry.Value));
		TestTrue(*FString::Printf(TEXT("%s is an Enemy tag"), Entry.Value), UMDEnemyArchetype::IsEnemyTag(Entry.Key));
		TestTrue(*FString::Printf(TEXT("%s resolves by name"), Entry.Value), FGameplayTag::RequestGameplayTag(FName(Entry.Value), false) == Entry.Key);

		bool bAlreadyInSet = false;
		Seen.Add(Entry.Key, &bAlreadyInSet);
		TestFalse(*FString::Printf(TEXT("%s is unique"), Entry.Value), bAlreadyInSet);

		// An archetype built on the tag reports exactly that tag.
		const UMDEnemyArchetype* Archetype = MakeArchetype(Entry.Key, 10.0f, 10.0f, 1);
		const FGameplayTagContainer Victim = Archetype->GetVictimTags();
		TestEqual(*FString::Printf(TEXT("%s victim tag count"), Entry.Value), Victim.Num(), 1);
		TestTrue(*FString::Printf(TEXT("%s victim tag"), Entry.Value), Victim.HasTagExact(Entry.Key));
	}
	TestFalse(TEXT("a non-enemy tag is not an Enemy tag"), UMDEnemyArchetype::IsEnemyTag(TAG_State_Staggered.GetTag()));
	TestFalse(TEXT("an empty tag is not an Enemy tag"), UMDEnemyArchetype::IsEnemyTag(FGameplayTag()));

	// Tier range (Ghoul T0..T3 defaults) and a swapped range.
	UMDEnemyArchetype* Ghoul = MakeArchetype(TAG_Enemy_Ghoul.GetTag(), 60.0f, 30.0f, 5);
	TestTrue(TEXT("T0 allowed"), Ghoul->AllowsTier(EMDIntelligenceTier::T0));
	TestTrue(TEXT("T3 allowed"), Ghoul->AllowsTier(EMDIntelligenceTier::T3));
	TestFalse(TEXT("T4 not allowed"), Ghoul->AllowsTier(EMDIntelligenceTier::T4));
	Ghoul->MinTier = EMDIntelligenceTier::T3;
	Ghoul->MaxTier = EMDIntelligenceTier::T1;
	TestTrue(TEXT("a swapped range still spans T1..T3"), Ghoul->AllowsTier(EMDIntelligenceTier::T2) && !Ghoul->AllowsTier(EMDIntelligenceTier::T0));

	// Poise resolution: no profile -> default with the archetype's max; matching profile -> same object; different -> a copy.
	Ghoul->PoiseProfile = nullptr;
	UMDPoiseProfile* Fresh = Ghoul->ResolvePoiseProfile(GetTransientPackage());
	if (TestNotNull(TEXT("a profile is made when none is set"), Fresh))
	{
		TestEqual(TEXT("fresh profile max poise"), Fresh->MaxPoise, 30.0f);
	}
	UMDPoiseProfile* Profile = NewObject<UMDPoiseProfile>(GetTransientPackage());
	Profile->MaxPoise = 30.0f;
	Ghoul->PoiseProfile = Profile;
	TestTrue(TEXT("a matching profile is used as is"), Ghoul->ResolvePoiseProfile(GetTransientPackage()) == Profile);
	Profile->MaxPoise = 80.0f;
	UMDPoiseProfile* Copy = Ghoul->ResolvePoiseProfile(GetTransientPackage());
	TestTrue(TEXT("a differing profile is copied"), Copy != Profile);
	if (Copy)
	{
		TestEqual(TEXT("the copy takes the archetype's max poise"), Copy->MaxPoise, 30.0f);
		TestEqual(TEXT("the source profile is untouched"), Profile->MaxPoise, 80.0f);
	}

	// Enum zero entries and the token struct.
	TestEqual(TEXT("Idle is 0"), static_cast<int32>(EMDIntent::Idle), 0);
	TestEqual(TEXT("Role None is 0"), static_cast<int32>(EMDRole::None), 0);
	FMDAttackToken Token;
	TestFalse(TEXT("a default token is not issued"), Token.IsIssued());
	return true;
}

// The enemy collision rules: Camera IGNORED (the player's camera probe must pass through enemies, or the view collapses into the
// player's shoulder when a ghoul is close) and Visibility BLOCKED (the weapon sweeps), on the capsule and the mesh. Checked on
// a spawned AMDEnemyCharacter, on the BP_Enemy_Ghoul asset (a Blueprint serializes its own collision over the constructor's)
// and, for co-op, on BP_Player: players do not block each other's cameras either.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAIFrameworkCollisionResponsesTest, "MazeDemons.AI.Framework.CollisionResponses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAIFrameworkCollisionResponsesTest::RunTest(const FString& Parameters)
{
	using namespace MDAIFrameworkTestsLocal;
	auto CheckEnemy = [this](const TCHAR* Label, const ACharacter* Character)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s character"), Label), Character) || !TestNotNull(*FString::Printf(TEXT("%s capsule"), Label), Character->GetCapsuleComponent())
			|| !TestNotNull(*FString::Printf(TEXT("%s mesh"), Label), Character->GetMesh()))
		{
			return;
		}
		const UPrimitiveComponent* Parts[2] = { Character->GetCapsuleComponent(), Character->GetMesh() };
		const TCHAR* Names[2] = { TEXT("capsule"), TEXT("mesh") };
		for (int32 i = 0; i < 2; ++i)
		{
			TestTrue(*FString::Printf(TEXT("%s %s ignores Camera"), Label, Names[i]), Parts[i]->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);
			TestTrue(*FString::Printf(TEXT("%s %s blocks Visibility (weapon sweeps)"), Label, Names[i]), Parts[i]->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		}
	};

	// The C++ class on its own, then as spawned (PostInitializeComponents re-applies the rules).
	CheckEnemy(TEXT("AMDEnemyCharacter CDO"), GetDefault<AMDEnemyCharacter>());
	{
		FEnemyFixture Fx;
		if (Fx.Init(*this))
		{
			CheckEnemy(TEXT("spawned enemy"), Fx.Enemy);
			// A Blueprint-style override (everything blocking, as BP_Enemy_Ghoul once serialized) is undone by re-applying.
			AMDEnemyCharacter* Fresh = Fx.World->SpawnActor<AMDEnemyCharacter>();
			if (TestNotNull(TEXT("second enemy"), Fresh))
			{
				Fresh->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
				Fresh->GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
				Fresh->ApplyCollisionResponses();
				CheckEnemy(TEXT("re-applied enemy"), Fresh);
			}
		}
	}

	// The shipped Blueprints.
	if (const UClass* GhoulClass = LoadClass<AMDEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemies/BP_Enemy_Ghoul.BP_Enemy_Ghoul_C")))
	{
		CheckEnemy(TEXT("BP_Enemy_Ghoul"), Cast<ACharacter>(GhoulClass->GetDefaultObject()));
	}
	else
	{
		AddError(TEXT("BP_Enemy_Ghoul could not be loaded"));
	}
	if (const UClass* PlayerClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Blueprints/BP_Player.BP_Player_C")))
	{
		const ACharacter* Player = Cast<ACharacter>(PlayerClass->GetDefaultObject());
		if (TestNotNull(TEXT("BP_Player CDO"), Player))
		{
			TestTrue(TEXT("BP_Player capsule ignores Camera"), Player->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);
			TestTrue(TEXT("BP_Player mesh ignores Camera"), Player->GetMesh()->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);
			TestTrue(TEXT("BP_Player capsule ignores Pawn (players overlap each other)"), Player->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
		}
	}
	else
	{
		AddError(TEXT("BP_Player could not be loaded"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
