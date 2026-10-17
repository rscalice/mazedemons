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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCommentBoxTest, "MazeDemons.Editor.CommentBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDCommentBoxTest::RunTest(const FString& Parameters)
{
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
		TEXT("MD_CommentTestBP"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("transient blueprint"), BP) || !TestTrue(TEXT("has a graph"), BP->UbergraphPages.Num() > 0))
	{
		return false;
	}
	UEdGraph* Graph = BP->UbergraphPages[0];

	UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = TEXT("MD_TestEvent");
	Graph->AddNode(Event, false, false);
	Event->CreateNewGuid();
	Event->PostPlacedNewNode();
	Event->AllocateDefaultPins();
	Event->NodePosX = 100;
	Event->NodePosY = 200;

	const FName GraphName = Graph->GetFName();
	const TArray<FString> Lines = UMD_BlueprintCommentLibrary::ListGraphNodesEx(BP, GraphName);
	TestTrue(TEXT("lists the event with its member name"), Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("MD_TestEvent")); }));

	const FString BoxName = UMD_BlueprintCommentLibrary::AddCommentBox(BP, GraphName, { Event->GetFName() }, TEXT("Test box"), FLinearColor::Green, 40);
	TestFalse(TEXT("returned a comment node name"), BoxName.IsEmpty());

	int32 CommentNodes = 0;
	for (const UEdGraphNode* N : Graph->Nodes)
	{
		if (const UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(N))
		{
			++CommentNodes;
			TestEqual(TEXT("comment text"), C->NodeComment, FString(TEXT("Test box")));
			TestTrue(TEXT("box encloses the node"), C->NodePosX < 100 && C->NodePosY < 200);
		}
	}
	TestEqual(TEXT("one comment box"), CommentNodes, 1);

	TestTrue(TEXT("node comment set"), UMD_BlueprintCommentLibrary::SetNodeComment(BP, GraphName, Event->GetFName(), TEXT("why"), true));
	TestFalse(TEXT("missing graph fails"), UMD_BlueprintCommentLibrary::SetNodeComment(BP, TEXT("NoSuchGraph"), Event->GetFName(), TEXT("x"), true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
