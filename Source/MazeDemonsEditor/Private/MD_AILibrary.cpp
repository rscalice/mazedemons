#include "MD_AILibrary.h"

#include "AI/MDAIRules.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "AI/StateTree/MDSTEnemyTasks.h"
#include "MDGameplayTags.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace MDAILibraryLocal
{
	// TSubclassOf members of the schema are protected UPROPERTYs: set them by reflection.
	void SetClassProperty(UObject* Object, const TCHAR* PropertyName, UClass* Value)
	{
		if (FClassProperty* Property = FindFProperty<FClassProperty>(Object->GetClass(), PropertyName))
		{
			Property->SetObjectPropertyValue_InContainer(Object, Value);
		}
	}

	const TCHAR* SeverityName(int32 Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error: return TEXT("Error");
		case EMessageSeverity::PerformanceWarning: return TEXT("Perf");
		case EMessageSeverity::Warning: return TEXT("Warning");
		default: return TEXT("Info");
		}
	}
}

bool UMD_AILibrary::BuildEnemyStateTree(const FString& PackagePath, TArray<FString>& OutLog)
{
	using namespace MDAILibraryLocal;

	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		OutLog.Add(FString::Printf(TEXT("Error: '%s' is not a long package name (/Game/AI/ST_Enemy)"), *PackagePath));
		return false;
	}
	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutLog.Add(TEXT("Error: CreatePackage failed"));
		return false;
	}
	Package->FullyLoad();

	bool bNew = false;
	UStateTree* Tree = FindObject<UStateTree>(Package, *AssetName);
	if (!Tree)
	{
		Tree = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		bNew = true;
	}

	// Fresh editor data every build: the asset is fully described by this function.
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	UStateTreeAIComponentSchema* Schema = NewObject<UStateTreeAIComponentSchema>(EditorData, NAME_None, RF_Transactional);
	SetClassProperty(Schema, TEXT("AIControllerClass"), AMDEnemyController::StaticClass());
	SetClassProperty(Schema, TEXT("ContextActorClass"), AMDEnemyCharacter::StaticClass());
	EditorData->Schema = Schema;
	Tree->EditorData = EditorData;
	Tree->ResetCompiled();

	UStateTreeState& Root = EditorData->AddRootState();
	Root.SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	Root.Description = TEXT("V1 enemy executor. The brain (UMDEnemyBrainComponent) decides; it sends AI.Intent.<X> events and this tree runs the matching state. Keep decision logic out of here.");

	struct FLeaf
	{
		UStateTreeState* State = nullptr;
		FGameplayTag Tag;
	};
	TArray<FLeaf> Leaves;

	auto AddLeaf = [&Root, &Leaves](const TCHAR* Name, const FGameplayTag& Tag, const TCHAR* Description, TFunctionRef<void(UStateTreeState&)> AddTaskFn) -> UStateTreeState&
	{
		UStateTreeState& State = Root.AddChildState(FName(Name));
		State.Description = Description;
		State.Tag = Tag;
		AddTaskFn(State);
		Leaves.Add(FLeaf{ &State, Tag });
		return State;
	};

	UStateTreeState& Idle = AddLeaf(TEXT("Idle"), MDIntentToTag(EMDIntent::Idle), TEXT("No target: stand still."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyIdle>(); });
	AddLeaf(TEXT("Chase"), MDIntentToTag(EMDIntent::Chase), TEXT("Move on the navmesh to the brain's target (last known position when out of sight)."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyChase>(); });
	AddLeaf(TEXT("HoldThreat"), MDIntentToTag(EMDIntent::HoldThreat), TEXT("No attack token: hold the archetype's threat range, facing the target."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyHoldThreat>(); });
	AddLeaf(TEXT("Attack"), MDIntentToTag(EMDIntent::Attack), TEXT("Play the chosen attack montage (motion warp to the target, claw trace window). The montage end returns control to the brain."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyAttack>(); });
	AddLeaf(TEXT("Stagger"), MDIntentToTag(EMDIntent::Stagger), TEXT("Poise broke: stop until the stun ends (interrupt, token returned)."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyStagger>(); });
	AddLeaf(TEXT("Dead"), MDIntentToTag(EMDIntent::Dead), TEXT("Dead: stop; the character ragdolls and detaches its controller."), [](UStateTreeState& S) { S.AddTask<FMDST_EnemyDead>(); });

	// The brain's events move the tree from any leaf (transitions are evaluated leaf to root).
	for (const FLeaf& Leaf : Leaves)
	{
		Root.AddTransition(EStateTreeTransitionTrigger::OnEvent, Leaf.Tag, EStateTreeTransitionType::GotoState, Leaf.State);
	}
	// Safety net: a leaf that completes (the tasks never do) falls back to Idle instead of stopping the tree.
	for (const FLeaf& Leaf : Leaves)
	{
		if (Leaf.State != &Idle)
		{
			Leaf.State->AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Idle);
		}
	}

	UStateTreeEditingSubsystem::MarkAsPubliclyModified(Tree);
	UStateTreeEditingSubsystem::ValidateStateTree(Tree);

	FStateTreeCompilerLog Log;
	const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
	for (const TSharedRef<FTokenizedMessage>& Message : Log.ToTokenizedMessages())
	{
		OutLog.Add(FString::Printf(TEXT("%s: %s"), SeverityName(Message->GetSeverity()), *Message->ToText().ToString()));
	}
	Log.DumpToLog(Tree, LogTemp);
	if (!bCompiled)
	{
		OutLog.Add(TEXT("Error: StateTree compile failed"));
		return false;
	}
	OutLog.Add(FString::Printf(TEXT("Compiled: %d states, ready to run = %s"), Tree->GetStates().Num(), Tree->IsReadyToRun() ? TEXT("yes") : TEXT("no")));

	Tree->MarkPackageDirty();
	if (bNew)
	{
		FAssetRegistryModule::AssetCreated(Tree);
	}
	const FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, Tree, *FileName, SaveArgs);
	OutLog.Add(FString::Printf(TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Error: save failed"), *FileName));
	return bSaved;
}

TArray<FString> UMD_AILibrary::DescribeStateTree(const FString& PackagePath)
{
	TArray<FString> Out;
	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName));
	if (!Tree)
	{
		Out.Add(TEXT("not found"));
		return Out;
	}
	const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(Tree->EditorData);
	Out.Add(FString::Printf(TEXT("%s: schema %s, compiled states %d, ready %s, editor data %s"), *Tree->GetName(),
		Tree->GetSchema() ? *Tree->GetSchema()->GetClass()->GetName() : TEXT("none"), Tree->GetStates().Num(), Tree->IsReadyToRun() ? TEXT("yes") : TEXT("no"),
		EditorData ? TEXT("yes") : TEXT("no")));
	if (!EditorData)
	{
		return Out;
	}
	TFunction<void(const UStateTreeState&, int32)> Visit = [&](const UStateTreeState& State, int32 Depth)
	{
		FString Line = FString::ChrN(Depth * 2, TEXT(' ')) + State.Name.ToString();
		for (const FStateTreeEditorNode& Task : State.Tasks)
		{
			Line += FString::Printf(TEXT("  [task %s]"), Task.Node.IsValid() ? *Task.Node.GetScriptStruct()->GetName() : TEXT("?"));
		}
		for (const FStateTreeTransition& T : State.Transitions)
		{
			const UStateTreeState* To = EditorData->GetStateByID(T.State.ID);
			Line += FString::Printf(TEXT("  {%s %s -> %s}"), *UEnum::GetValueAsString(T.Trigger), *T.RequiredEvent.Tag.ToString(), To ? *To->Name.ToString() : TEXT("?"));
		}
		Out.Add(Line);
		for (const UStateTreeState* Child : State.Children)
		{
			if (Child)
			{
				Visit(*Child, Depth + 1);
			}
		}
	};
	for (const UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (SubTree)
		{
			Visit(*SubTree, 0);
		}
	}
	return Out;
}
