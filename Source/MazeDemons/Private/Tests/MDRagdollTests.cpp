#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDEnemyCharacter.h"
#include "MDCombatTypes.h"
#include "MDPhysicalHitReactComponent.h"
#include "MDPlayerState.h"
#include "MDRagdollTuning.h"
#include "HAL/IConsoleManager.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

// Enemy ragdoll (MazeDemons.Combat.Ragdoll.*): a pawn without a separate body mesh ragdolls its whole main mesh, and the
// cosmetic hit reaction is shut down so it cannot keep part of the body stiff.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRagdollEnemyFullBodyTest, "MazeDemons.Combat.Ragdoll.EnemyFullBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRagdollEnemyFullBodyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRagdollTestWorld")));
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	AMDEnemyCharacter* Enemy = World->SpawnActor<AMDEnemyCharacter>();
	if (TestNotNull(TEXT("enemy"), Enemy))
	{
		UMDPhysicalHitReactComponent* HitReact = NewObject<UMDPhysicalHitReactComponent>(Enemy, TEXT("MDPhysicalHitReact"));
		HitReact->RegisterComponent();

		AMDPlayerState::RagdollPawn(Enemy);

		TestTrue(TEXT("the capsule stops colliding"), Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("movement is disabled"), Enemy->GetCharacterMovement()->MovementMode == MOVE_None);
		TestTrue(TEXT("the anim instance stops driving the main mesh"), Enemy->GetMesh()->bPauseAnims);
		TestEqual(TEXT("the collision profile is Ragdoll"), Enemy->GetMesh()->GetCollisionProfileName(), FName(TEXT("Ragdoll")));
		TestFalse(TEXT("the hit reaction is not running"), HitReact->IsReacting());
		TestFalse(TEXT("a dead enemy plays no more physical reactions"), HitReact->PlayHitReaction(Enemy->GetMesh(), EMDHitDirection::Front, EMDHitReaction::Stagger));
	}

	World->DestroyWorld(false);
	if (World->IsRooted())
	{
		World->RemoveFromRoot();
	}
	return true;
}

// The ragdoll damping cvars are what RagdollTuning::Get returns, players are firmer than enemies, and a mesh without bodies is safe.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRagdollTuningCVarsTest, "MazeDemons.Combat.Ragdoll.TuningCVars", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDRagdollTuningCVarsTest::RunTest(const FString& Parameters)
{
	IConsoleManager& Console = IConsoleManager::Get();
	IConsoleVariable* PlayerAngular = Console.FindConsoleVariable(TEXT("md.Combat.RagdollAngularDamping"));
	IConsoleVariable* EnemyStiffness = Console.FindConsoleVariable(TEXT("md.Combat.RagdollEnemyJointStiffness"));
	if (!TestNotNull(TEXT("player angular cvar"), PlayerAngular) || !TestNotNull(TEXT("enemy stiffness cvar"), EnemyStiffness))
	{
		return false;
	}
	const float OldAngular = PlayerAngular->GetFloat();
	const float OldStiffness = EnemyStiffness->GetFloat();

	const FMDRagdollTuning Player = MDRagdollTuning::Get(true);
	const FMDRagdollTuning Enemy = MDRagdollTuning::Get(false);
	TestTrue(TEXT("the player ragdoll is damped at least as much as the enemy one"), Player.AngularDamping >= Enemy.AngularDamping && Player.LinearDamping >= Enemy.LinearDamping);
	TestTrue(TEXT("the player joints are at least as firm as the enemy joints"), Player.JointStiffness >= Enemy.JointStiffness);

	PlayerAngular->Set(7.5f, ECVF_SetByCode);
	EnemyStiffness->Set(33.0f, ECVF_SetByCode);
	TestEqual(TEXT("player angular damping follows the cvar"), MDRagdollTuning::Get(true).AngularDamping, 7.5f);
	TestEqual(TEXT("enemy joint stiffness follows the cvar"), MDRagdollTuning::Get(false).JointStiffness, 33.0f);
	TestEqual(TEXT("the other kind is unaffected"), MDRagdollTuning::Get(false).AngularDamping, Enemy.AngularDamping);
	PlayerAngular->Set(OldAngular, ECVF_SetByCode);
	EnemyStiffness->Set(OldStiffness, ECVF_SetByCode);

	// A component with no physics bodies must be a harmless no-op.
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	MDRagdollTuning::ApplyForDeath(Mesh, true);
	MDRagdollTuning::ApplyForDeath(nullptr, false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
