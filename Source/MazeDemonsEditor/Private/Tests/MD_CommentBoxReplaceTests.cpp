#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MD_BlueprintCommentLibrary.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCommentBoxReplaceTest, "MazeDemons.Editor.CommentBoxReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDCommentBoxReplaceTest::RunTest(const FString& Parameters)
{
	// Pure matcher: case and whitespace insensitive, empty needle never matches.
	TestTrue(TEXT("exact"), UMD_BlueprintCommentLibrary::CommentTextMatches(TEXT("Grants items without freezing time"), TEXT("without freezing time")));
	TestTrue(TEXT("case and spacing"), UMD_BlueprintCommentLibrary::CommentTextMatches(TEXT("Grants items\nWithout  Freezing Time"), TEXT("without freezing TIME")));
	TestFalse(TEXT("no match"), UMD_BlueprintCommentLibrary::CommentTextMatches(TEXT("Grants items"), TEXT("freezing")));
	TestFalse(TEXT("empty needle"), UMD_BlueprintCommentLibrary::CommentTextMatches(TEXT("anything"), TEXT("")));
	TestFalse(TEXT("whitespace-only needle"), UMD_BlueprintCommentLibrary::CommentTextMatches(TEXT("anything"), TEXT("  ")));

	// Transient Blueprint with two boxes.
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
		TEXT("MD_CommentReplaceTestBP"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("transient blueprint"), BP) || !TestTrue(TEXT("has a graph"), BP->UbergraphPages.Num() > 0))
	{
		return false;
	}
	UEdGraph* Graph = BP->UbergraphPages[0];

	UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = TEXT("MD_ReplaceTestEvent");
	Graph->AddNode(Event, false, false);
	Event->CreateNewGuid();
	Event->PostPlacedNewNode();
	Event->AllocateDefaultPins();

	const FName GraphName = Graph->GetFName();
	const FString First = UMD_BlueprintCommentLibrary::AddCommentBox(BP, GraphName, { Event->GetFName() }, TEXT("Opening the bag does not freeze time"), FLinearColor::Green, 40);
	const FString Second = UMD_BlueprintCommentLibrary::AddCommentBox(BP, GraphName, { Event->GetFName() }, TEXT("Unrelated box"), FLinearColor::Blue, 80);
	TestFalse(TEXT("first box created"), First.IsEmpty());
	TestFalse(TEXT("second box created"), Second.IsEmpty());

	auto TextOf = [Graph](const FString& BoxName) -> FString
	{
		for (const UEdGraphNode* N : Graph->Nodes)
		{
			if (const UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(N))
			{
				if (C->GetName() == BoxName)
				{
					return C->NodeComment;
				}
			}
		}
		return FString(TEXT("<missing>"));
	};

	TestEqual(TEXT("one box replaced"),
		UMD_BlueprintCommentLibrary::SetCommentBoxText(BP, GraphName, TEXT("DOES NOT freeze"), TEXT("Opening the bag freezes nothing")), 1);
	TestEqual(TEXT("new text applied"), TextOf(First), FString(TEXT("Opening the bag freezes nothing")));
	TestEqual(TEXT("other box untouched"), TextOf(Second), FString(TEXT("Unrelated box")));

	TestEqual(TEXT("second run finds nothing (idempotent)"),
		UMD_BlueprintCommentLibrary::SetCommentBoxText(BP, GraphName, TEXT("DOES NOT freeze"), TEXT("Opening the bag freezes nothing")), 0);
	TestEqual(TEXT("empty needle replaces nothing"),
		UMD_BlueprintCommentLibrary::SetCommentBoxText(BP, GraphName, TEXT(""), TEXT("oops")), 0);
	TestEqual(TEXT("missing graph replaces nothing"),
		UMD_BlueprintCommentLibrary::SetCommentBoxText(BP, TEXT("NoSuchGraph"), TEXT("Unrelated"), TEXT("oops")), 0);
	TestEqual(TEXT("null blueprint replaces nothing"),
		UMD_BlueprintCommentLibrary::SetCommentBoxText(nullptr, GraphName, TEXT("Unrelated"), TEXT("oops")), 0);
	TestEqual(TEXT("other box still untouched"), TextOf(Second), FString(TEXT("Unrelated box")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
