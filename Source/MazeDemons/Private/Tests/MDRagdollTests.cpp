#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MDEnemyCharacter.h"
#include "MDCombatTypes.h"
#include "MDPhysicalHitReactComponent.h"
#include "MDPlayerState.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
