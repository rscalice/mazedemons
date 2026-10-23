// MazeDemons - editor-only Blueprint comment helpers

#include "MD_BlueprintCommentLibrary.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"

namespace MD_CommentLib
{
	/** Every graph the Blueprint owns (event graphs, functions, macros, delegate signatures). Widget BPs use the same lists. */
	static void GatherGraphs(const UBlueprint* BP, TArray<UEdGraph*>& Out)
	{
		Out.Append(BP->UbergraphPages);
		Out.Append(BP->FunctionGraphs);
		Out.Append(BP->MacroGraphs);
		Out.Append(BP->DelegateSignatureGraphs);
	}

	static UEdGraph* FindGraph(const UBlueprint* BP, FName GraphName)
	{
		if (!BP) { return nullptr; }
		TArray<UEdGraph*> Graphs;
		GatherGraphs(BP, Graphs);
		for (UEdGraph* G : Graphs)
		{
			if (G && G->GetFName() == GraphName) { return G; }
		}
		return nullptr;
	}

	static UEdGraphNode* FindNode(const UEdGraph* Graph, FName NodeName)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && N->GetFName() == NodeName) { return N; }
		}
		return nullptr;
	}

	/** Slate-measured size when the node has been drawn; otherwise a rough estimate from the pin count. */
	static FVector2D NodeSize(const UEdGraphNode* N)
	{
		const float W = N->NodeWidth > 0 ? N->NodeWidth : 260.f;
		const float H = N->NodeHeight > 0 ? N->NodeHeight : 60.f + 16.f * FMath::Max(1, N->Pins.Num() / 2);
		return FVector2D(W, H);
	}

	static FString MemberName(const UEdGraphNode* N)
	{
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(N))
		{
			return Call->FunctionReference.GetMemberName().ToString();
		}
		if (const UK2Node_Variable* Var = Cast<UK2Node_Variable>(N))
		{
			return Var->VariableReference.GetMemberName().ToString();
		}
		if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(N))
		{
			return Custom->CustomFunctionName.ToString();
		}
		if (const UK2Node_Event* Event = Cast<UK2Node_Event>(N))
		{
			return Event->EventReference.GetMemberName().ToString();
		}
		return FString();
	}

	static TArray<FString> ListNodes(UBlueprint* Blueprint, FName GraphName, bool bWithMember)
	{
		TArray<FString> Lines;
		if (!Blueprint) { return Lines; }

		TArray<UEdGraph*> Graphs;
		if (GraphName.IsNone())
		{
			GatherGraphs(Blueprint, Graphs);
		}
		else if (UEdGraph* G = FindGraph(Blueprint, GraphName))
		{
			Graphs.Add(G);
		}

		for (const UEdGraph* G : Graphs)
		{
			if (!G) { continue; }
			for (const UEdGraphNode* N : G->Nodes)
			{
				if (!N) { continue; }
				FString Title = N->GetNodeTitle(ENodeTitleType::ListView).ToString();
				Title.ReplaceInline(TEXT("\r"), TEXT(" "));
				Title.ReplaceInline(TEXT("\n"), TEXT(" "));
				const FString Prefix = GraphName.IsNone() ? FString::Printf(TEXT("[%s] "), *G->GetName()) : FString();
				if (bWithMember)
				{
					Lines.Add(FString::Printf(TEXT("%s%s | %s | %d,%d | %s"), *Prefix, *N->GetName(), *Title, N->NodePosX, N->NodePosY, *MemberName(N)));
				}
				else
				{
					Lines.Add(FString::Printf(TEXT("%s%s | %s | %d,%d"), *Prefix, *N->GetName(), *Title, N->NodePosX, N->NodePosY));
				}
			}
		}
		return Lines;
	}
}

bool UMD_BlueprintCommentLibrary::SetNodeComment(UBlueprint* Blueprint, FName GraphName, FName NodeName, const FString& Comment, bool bPinnedBubble)
{
	UEdGraph* Graph = MD_CommentLib::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Comment] Graph '%s' not found"), *GraphName.ToString());
		return false;
	}
	UEdGraphNode* Node = MD_CommentLib::FindNode(Graph, NodeName);
	if (!Node)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Comment] Node '%s' not found in '%s'"), *NodeName.ToString(), *GraphName.ToString());
		return false;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Set Node Comment")));
	Node->Modify();
	Node->NodeComment = Comment;
	Node->bCommentBubblePinned = bPinnedBubble;
	Node->bCommentBubbleVisible = bPinnedBubble;
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

FString UMD_BlueprintCommentLibrary::AddCommentBox(UBlueprint* Blueprint, FName GraphName, const TArray<FName>& NodeNames, const FString& Text, FLinearColor Color, int32 Padding)
{
	UEdGraph* Graph = MD_CommentLib::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Comment] Graph '%s' not found"), *GraphName.ToString());
		return FString();
	}

	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	int32 Found = 0;
	for (const FName& Name : NodeNames)
	{
		const UEdGraphNode* N = MD_CommentLib::FindNode(Graph, Name);
		if (!N)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MD_Comment] Node '%s' not found in '%s' (skipped)"), *Name.ToString(), *GraphName.ToString());
			continue;
		}
		const FVector2D Size = MD_CommentLib::NodeSize(N);
		Min.X = FMath::Min<double>(Min.X, N->NodePosX);
		Min.Y = FMath::Min<double>(Min.Y, N->NodePosY);
		Max.X = FMath::Max<double>(Max.X, N->NodePosX + Size.X);
		Max.Y = FMath::Max<double>(Max.Y, N->NodePosY + Size.Y);
		++Found;
	}
	if (Found == 0) { return FString(); }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Comment Box")));
	Graph->Modify();

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph, UEdGraphNode_Comment::StaticClass(), NAME_None, RF_Transactional);
	Graph->AddNode(Comment, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	Comment->CreateNewGuid();
	Comment->PostPlacedNewNode();
	Comment->AllocateDefaultPins();

	const double TitleSpace = 36.0; // room above the nodes for the comment title
	Comment->NodePosX = FMath::RoundToInt(Min.X - Padding);
	Comment->NodePosY = FMath::RoundToInt(Min.Y - Padding - TitleSpace);
	Comment->NodeWidth = FMath::RoundToInt((Max.X - Min.X) + 2 * Padding);
	Comment->NodeHeight = FMath::RoundToInt((Max.Y - Min.Y) + 2 * Padding + TitleSpace);
	Comment->NodeComment = Text;
	Comment->CommentColor = Color;

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Comment->GetName();
}

namespace MD_CommentLib
{
	/** Lower-cased copy with every whitespace character removed. */
	static FString Normalize(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (!FChar::IsWhitespace(C))
			{
				Out.AppendChar(FChar::ToLower(C));
			}
		}
		return Out;
	}
}

bool UMD_BlueprintCommentLibrary::CommentTextMatches(const FString& Existing, const FString& Needle)
{
	const FString NeedleNorm = MD_CommentLib::Normalize(Needle);
	return !NeedleNorm.IsEmpty() && MD_CommentLib::Normalize(Existing).Contains(NeedleNorm);
}

int32 UMD_BlueprintCommentLibrary::SetCommentBoxText(UBlueprint* Blueprint, FName GraphName, const FString& OldTextContains, const FString& NewText)
{
	UEdGraph* Graph = MD_CommentLib::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Comment] Graph '%s' not found"), *GraphName.ToString());
		return 0;
	}

	int32 Changed = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
		if (!Comment || !CommentTextMatches(Comment->NodeComment, OldTextContains))
		{
			continue;
		}
		Comment->Modify();
		Comment->NodeComment = NewText;
		++Changed;
	}
	if (Changed > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	return Changed;
}

TArray<FString> UMD_BlueprintCommentLibrary::ListGraphNodes(UBlueprint* Blueprint, FName GraphName)
{
	return MD_CommentLib::ListNodes(Blueprint, GraphName, false);
}

TArray<FString> UMD_BlueprintCommentLibrary::ListGraphNodesEx(UBlueprint* Blueprint, FName GraphName)
{
	return MD_CommentLib::ListNodes(Blueprint, GraphName, true);
}

TArray<FString> UMD_BlueprintCommentLibrary::GetGraphNames(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	if (!Blueprint) { return Names; }
	TArray<UEdGraph*> Graphs;
	MD_CommentLib::GatherGraphs(Blueprint, Graphs);
	for (const UEdGraph* G : Graphs)
	{
		if (G) { Names.Add(G->GetName()); }
	}
	return Names;
}

bool UMD_BlueprintCommentLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) { return false; }
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UMD_BlueprintCommentLibrary::SaveBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) { return false; }
	TArray<UPackage*> Packages;
	Packages.Add(Blueprint->GetOutermost());
	return UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);
}
