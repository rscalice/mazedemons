// MazeDemons - editor-only helpers so scripts (Python) can add Blueprint node comments and comment boxes.
// MCP cannot set UEdGraphNode::NodeComment or create UEdGraphNode_Comment, so Content/Python scripts call these.
// Python usage: unreal.MD_BlueprintCommentLibrary.set_node_comment(bp, "SetupTabs", "K2Node_CallFunction_3", "why", True)

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_BlueprintCommentLibrary.generated.h"

class UBlueprint;

UCLASS()
class MAZEDEMONSEDITOR_API UMD_BlueprintCommentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Set (and show) the comment bubble on one node. NodeName is the node object name, e.g. K2Node_CallFunction_3. Returns false if the graph or node is not found. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool SetNodeComment(UBlueprint* Blueprint, FName GraphName, FName NodeName, const FString& Comment, bool bPinnedBubble = true);

	/** Create a comment box around the named nodes (bounds + Padding). Returns the new comment node's name, or empty on failure. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static FString AddCommentBox(UBlueprint* Blueprint, FName GraphName, const TArray<FName>& NodeNames, const FString& Text, FLinearColor Color, int32 Padding = 40);

	/** "Name | Title | X,Y" for every node in the graph (pass NAME_None / "None" to list all graphs; lines are then prefixed "[Graph] "). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FString> ListGraphNodes(UBlueprint* Blueprint, FName GraphName);

	/** Like ListGraphNodes, plus a 4th field: the member name (called function, variable, event or custom event name). Format: "Name | Title | X,Y | Member". */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FString> ListGraphNodesEx(UBlueprint* Blueprint, FName GraphName);

	/** Names of every graph the Blueprint owns (event graphs, functions, macros, delegate signatures). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FString> GetGraphNames(UBlueprint* Blueprint);

	/** Compile the Blueprint (widget Blueprints included). Returns true if it compiled without errors. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool CompileBlueprint(UBlueprint* Blueprint);

	/** Save the Blueprint's package to disk (no prompts). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool SaveBlueprint(UBlueprint* Blueprint);
};
