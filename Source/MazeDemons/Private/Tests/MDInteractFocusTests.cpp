#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MDInteractAbility.h"
#include "MDPlayerController.h"
#include "Misc/AutomationTest.h"

// Pure helpers behind the local interact focus trace; no world needed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInteractPromptTest, "MazeDemons.Interact.PromptText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDInteractPromptTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("pickup prompt"),
		AMDPlayerController::BuildInteractPrompt(FText::FromString(TEXT("Health Potion"))).ToString(),
		FString(TEXT("Pick up Health Potion")));
	TestEqual(TEXT("generic prompt"),
		AMDPlayerController::BuildInteractPrompt(FText::GetEmpty()).ToString(),
		FString(TEXT("Interact")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInteractFocusChangeTest, "MazeDemons.Interact.FocusChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDInteractFocusChangeTest::RunTest(const FString& Parameters)
{
	// Distinct non-null stand-ins; the helper only compares pointers.
	AActor* A = reinterpret_cast<AActor*>(static_cast<UPTRINT>(0x1000));
	AActor* B = reinterpret_cast<AActor*>(static_cast<UPTRINT>(0x2000));
	const FText P1 = FText::FromString(TEXT("Pick up Sword"));
	const FText P2 = FText::FromString(TEXT("Pick up Axe"));

	TestFalse(TEXT("none -> none"), AMDPlayerController::WouldFocusChange(nullptr, FText::GetEmpty(), nullptr, FText::GetEmpty()));
	TestTrue(TEXT("none -> A"), AMDPlayerController::WouldFocusChange(nullptr, FText::GetEmpty(), A, P1));
	TestFalse(TEXT("A -> A same prompt"), AMDPlayerController::WouldFocusChange(A, P1, A, P1));
	TestTrue(TEXT("A -> A new prompt"), AMDPlayerController::WouldFocusChange(A, P1, A, P2));
	TestTrue(TEXT("A -> B"), AMDPlayerController::WouldFocusChange(A, P1, B, P1));
	TestTrue(TEXT("A -> none"), AMDPlayerController::WouldFocusChange(A, P1, nullptr, FText::GetEmpty()));
	return true;
}

// Regression: a weapon attached to a pawn (own or another's) that blocks Visibility and sits between the
// avatar and a station must not hide the station from the interact sweep.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInteractIgnoresPawnAttachmentsTest, "MazeDemons.Interact.IgnoresPawnAttachments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDInteractIgnoresPawnAttachmentsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	auto MakeBox = [](AActor* Actor, const FVector& Extent, bool bBlock)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(bBlock ? ECR_Block : ECR_Ignore);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		return Box;
	};

	APawn* Avatar = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector::ZeroVector));
	AActor* Station = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(150, 0, 0)));
	AActor* OwnWeapon = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(60, 0, 0)));
	APawn* OtherPawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(0, 5000, 0)));
	AActor* OtherWeapon = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(100, 0, 0)));
	if (!Avatar || !Station || !OwnWeapon || !OtherPawn || !OtherWeapon)
	{
		World->DestroyWorld(false);
		return TestTrue(TEXT("spawned actors"), false);
	}
	// Bare pawns have no root component, so attachment would silently fail.
	MakeBox(Avatar, FVector(10), false);
	MakeBox(OtherPawn, FVector(10), false);
	MakeBox(Station, FVector(30), true);
	Station->Tags.Add(TEXT("Interactable"));
	MakeBox(OwnWeapon, FVector(20), true);
	MakeBox(OtherWeapon, FVector(20), true);
	OwnWeapon->AttachToActor(Avatar, FAttachmentTransformRules::KeepWorldTransform);
	OtherWeapon->AttachToActor(OtherPawn, FAttachmentTransformRules::KeepWorldTransform);

	TestTrue(TEXT("own weapon attached"), OwnWeapon->GetAttachParentActor() == Avatar);
	TestEqual(TEXT("target found through own + other pawn weapons"),
		UMDInteractAbility::FindInteractTargetFor(Avatar, 300.f, 20.f, TEXT("Interactable")), Station);
	TestTrue(TEXT("weapon counts as pawn attachment"), UMDInteractAbility::IsAttachedToPawn(OtherWeapon));
	TestFalse(TEXT("station is not a pawn attachment"), UMDInteractAbility::IsAttachedToPawn(Station));

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
