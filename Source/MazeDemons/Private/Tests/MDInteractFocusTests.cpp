#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

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

#endif // WITH_DEV_AUTOMATION_TESTS
