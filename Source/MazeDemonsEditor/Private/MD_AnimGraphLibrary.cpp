#include "MD_AnimGraphLibrary.h"

#include "AnimGraphNode_MDSupportHandIK.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MDAnimGraphLocal
{
	UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath)
	{
		return LoadObject<UAnimBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath)));
	}

	UEdGraph* FindAnimGraph(UAnimBlueprint* ABP)
	{
		for (UEdGraph* Graph : ABP->FunctionGraphs)
		{
			if (Graph && Graph->IsA<UAnimationGraph>())
			{
				return Graph;
			}
		}
		return nullptr;
	}

	// The pose pin of a node in a direction: by name when it exists, else the first pose-struct pin.
	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, const TCHAR* PreferredName)
	{
		if (!Node)
		{
			return nullptr;
		}
		if (UEdGraphPin* Pin = Node->FindPin(PreferredName, Direction))
		{
			return Pin;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
				&& Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject->GetName().Contains(TEXT("PoseLink")))
			{
				return Pin;
			}
		}
		return nullptr;
	}
}

int32 UMD_AnimGraphLibrary::CountSupportHandIKNodes(const FString& AssetPath)
{
	UAnimBlueprint* ABP = MDAnimGraphLocal::LoadAnimBlueprint(AssetPath);
	UEdGraph* Graph = ABP ? MDAnimGraphLocal::FindAnimGraph(ABP) : nullptr;
	if (!Graph)
	{
		return -1;
	}
	int32 Count = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		Count += Node && Node->IsA<UAnimGraphNode_MDSupportHandIK>() ? 1 : 0;
	}
	return Count;
}

bool UMD_AnimGraphLibrary::InsertSupportHandIK(const FString& AssetPath, TArray<FString>& OutLog)
{
	using namespace MDAnimGraphLocal;
	UAnimBlueprint* ABP = LoadAnimBlueprint(AssetPath);
	if (!ABP)
	{
		OutLog.Add(FString::Printf(TEXT("Error: %s does not load as an animation Blueprint"), *AssetPath));
		return false;
	}
	UEdGraph* AnimGraph = FindAnimGraph(ABP);
	if (!AnimGraph)
	{
		OutLog.Add(TEXT("Error: no AnimGraph"));
		return false;
	}
	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (Node && Node->IsA<UAnimGraphNode_MDSupportHandIK>())
		{
			OutLog.Add(TEXT("The support-hand IK node is already in the graph: nothing to do"));
			return true;
		}
		if (UAnimGraphNode_Root* AsRoot = Cast<UAnimGraphNode_Root>(Node))
		{
			Root = AsRoot;
		}
	}
	UEdGraphPin* ResultIn = FindPosePin(Root, EGPD_Input, TEXT("Result"));
	if (!ResultIn || ResultIn->LinkedTo.Num() != 1)
	{
		OutLog.Add(TEXT("Error: the Output Pose node must have exactly one input link"));
		return false;
	}
	UEdGraphPin* SourceOut = ResultIn->LinkedTo[0];

	const int32 X = Root->NodePosX - 700;
	const int32 Y = Root->NodePosY;
	UAnimGraphNode_LocalToComponentSpace* ToComponent = nullptr;
	UAnimGraphNode_MDSupportHandIK* IK = nullptr;
	UAnimGraphNode_ComponentToLocalSpace* ToLocal = nullptr;
	{
		FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> Creator(*AnimGraph);
		ToComponent = Creator.CreateNode();
		ToComponent->NodePosX = X;
		ToComponent->NodePosY = Y;
		Creator.Finalize();
	}
	{
		FGraphNodeCreator<UAnimGraphNode_MDSupportHandIK> Creator(*AnimGraph);
		IK = Creator.CreateNode();
		IK->NodePosX = X + 250;
		IK->NodePosY = Y;
		Creator.Finalize();
	}
	{
		FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> Creator(*AnimGraph);
		ToLocal = Creator.CreateNode();
		ToLocal->NodePosX = X + 500;
		ToLocal->NodePosY = Y;
		Creator.Finalize();
	}

	const UEdGraphSchema* Schema = AnimGraph->GetSchema();
	auto Connect = [&](UEdGraphPin* From, UEdGraphPin* To, const TCHAR* Label) -> bool
	{
		if (!From || !To || !Schema->TryCreateConnection(From, To))
		{
			OutLog.Add(FString::Printf(TEXT("Error: could not connect %s"), Label));
			return false;
		}
		return true;
	};
	ResultIn->BreakAllPinLinks();
	bool bOk = Connect(SourceOut, FindPosePin(ToComponent, EGPD_Input, TEXT("LocalPose")), TEXT("source -> LocalToComponent"));
	bOk &= Connect(FindPosePin(ToComponent, EGPD_Output, TEXT("Pose")), FindPosePin(IK, EGPD_Input, TEXT("ComponentPose")), TEXT("LocalToComponent -> IK"));
	bOk &= Connect(FindPosePin(IK, EGPD_Output, TEXT("Pose")), FindPosePin(ToLocal, EGPD_Input, TEXT("ComponentPose")), TEXT("IK -> ComponentToLocal"));
	bOk &= Connect(FindPosePin(ToLocal, EGPD_Output, TEXT("Pose")), ResultIn, TEXT("ComponentToLocal -> Output Pose"));
	if (!bOk)
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(ABP, EBlueprintCompileOptions::None, &Results);
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		OutLog.Add(Message->ToText().ToString());
	}
	OutLog.Add(FString::Printf(TEXT("Compile: %d errors, %d warnings"), Results.NumErrors, Results.NumWarnings));
	if (Results.NumErrors > 0)
	{
		return false;
	}

	ABP->MarkPackageDirty();
	UPackage* Package = ABP->GetOutermost();
	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, ABP, *FileName, SaveArgs);
	OutLog.Add(FString::Printf(TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Error: save failed"), *FileName));
	return bSaved;
}
