// MazeDemons - editor-only WidgetLint

#include "MD_WidgetLintLibrary.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "MDUIStatics.h"
#include "MDUIStyle.h"
#include "MDUITypes.h"
#include "MDUIWidgets.h"
#include "MDGameMenuWidget.h"
#include "MDPerksWidget.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDWidgetLint, Log, All);

namespace MD_WidgetLint
{
	static const TCHAR* const DefaultRoots[] = { TEXT("/Game/UI"), TEXT("/Game/Blueprints/UI") };

	// Deleted-in-WP3 assets nothing may reference any more (package paths).
	static const TCHAR* const LegacyPackages[] = {
		TEXT("/Game/Blueprints/UI/W_MainUI"),
		TEXT("/Game/Blueprints/UI/W_HealthBar"),
		TEXT("/Game/Blueprints/UI/W_NotifyBox"),
		TEXT("/Game/Blueprints/UI/W_NotifyWidget"),
		TEXT("/Game/Blueprints/UI/E_NotifyType"),
		TEXT("/Game/Blueprints/UI/IA_InteractionWidget"),
		TEXT("/Game/Blueprints/UI/Inventory/W_OptionsMenu"),
		TEXT("/Game/Blueprints/UI/Inventory/W_ToolTip"),
		TEXT("/Game/Blueprints/UI/Inventory/W_EquipmentSlotPreview"),
	};

	// Menus and panels that must be gamepad-usable (L3).
	static const TCHAR* const MenuAssets[] = {
		TEXT("W_Inventory"), TEXT("W_MDGameMenu"), TEXT("W_Pause"), TEXT("W_MDStationPanel"), TEXT("W_MDPerksPanel"), TEXT("W_MDContextMenu"),
	};

	// Menus whose navigation edges are errors, not warnings (L4).
	static const TCHAR* const EdgeRuleAssets[] = { TEXT("W_Inventory"), TEXT("W_MDGameMenu"), TEXT("W_MDStationPanel") };

	static bool NameIn(const FString& Name, const TCHAR* const* List, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			if (Name == List[i])
			{
				return true;
			}
		}
		return false;
	}

	static FMDLintIssue MakeIssue(const FString& Asset, const FString& Widget, const TCHAR* Rule, EMDLintSeverity Severity, const FString& Message)
	{
		FMDLintIssue Issue;
		Issue.Asset = Asset;
		Issue.Widget = Widget;
		Issue.Rule = Rule;
		Issue.Severity = Severity;
		Issue.Message = Message;
		return Issue;
	}

	// ---- reflection helpers: property access by name, so a changed UMG access level cannot break the build ----

	template <typename T>
	static const T* FindStruct(const UObject* Object, const TCHAR* PropertyName, const TCHAR* StructName)
	{
		if (!Object)
		{
			return nullptr;
		}
		const FStructProperty* Property = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(FName(PropertyName)));
		if (!Property || !Property->Struct || Property->Struct->GetFName() != FName(StructName))
		{
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<T>(Object);
	}

	static bool ReadBool(const UObject* Object, const TCHAR* PropertyName, bool& bOut)
	{
		if (!Object)
		{
			return false;
		}
		const FBoolProperty* Property = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(FName(PropertyName)));
		if (!Property)
		{
			return false;
		}
		bOut = Property->GetPropertyValue_InContainer(Object);
		return true;
	}

	// ---- colours ----

	static bool IsNear(float A, float B, float Tolerance = 0.01f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	static bool IsAllowedColor(const FLinearColor& Color, const TArray<FLinearColor>& Tokens)
	{
		if (Color.A <= 0.01f)
		{
			return true; // transparent
		}
		if (IsNear(Color.R, 1.0f) && IsNear(Color.G, 1.0f) && IsNear(Color.B, 1.0f))
		{
			return true; // white (tint / pass-through)
		}
		for (const FLinearColor& Token : Tokens)
		{
			if (IsNear(Color.R, Token.R) && IsNear(Color.G, Token.G) && IsNear(Color.B, Token.B))
			{
				return true; // a style token, any alpha
			}
		}
		return false;
	}

	// Finds "(R=..,G=..,B=..,A=..)" inside a pin default (FLinearColor or the SpecifiedColor of an FSlateColor).
	static bool TryParseColorDefault(const FString& Default, FLinearColor& OutColor)
	{
		const int32 Start = Default.Find(TEXT("(R="), ESearchCase::IgnoreCase);
		if (Start == INDEX_NONE)
		{
			return false;
		}
		const FString Tail = Default.Mid(Start);
		const int32 End = Tail.Find(TEXT(")"));
		if (End == INDEX_NONE)
		{
			return false;
		}
		return OutColor.InitFromString(Tail.Left(End + 1));
	}

	static FString ColorToText(const FLinearColor& C)
	{
		return FString::Printf(TEXT("(R=%.2f G=%.2f B=%.2f A=%.2f)"), C.R, C.G, C.B, C.A);
	}

	// ---- graphs ----

	static void GatherGraphs(const UBlueprint* BP, TArray<UEdGraph*>& Out)
	{
		Out.Append(BP->UbergraphPages);
		Out.Append(BP->FunctionGraphs);
		Out.Append(BP->MacroGraphs);
		Out.Append(BP->DelegateSignatureGraphs);
	}

	static bool IsRounded(const FSlateBrush& Brush)
	{
		if (Brush.DrawAs != ESlateBrushDrawType::RoundedBox)
		{
			return false;
		}
		const FSlateBrushOutlineSettings& Outline = Brush.OutlineSettings;
		return Outline.RoundingType == ESlateBrushRoundingType::HalfHeightRadius
			|| Outline.CornerRadii.X > 0.0 || Outline.CornerRadii.Y > 0.0 || Outline.CornerRadii.Z > 0.0 || Outline.CornerRadii.W > 0.0;
	}

	static bool IsHiddenVisibility(ESlateVisibility Visibility)
	{
		return Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden;
	}

	static bool IsFocusableControl(const UWidget* Widget)
	{
		return Cast<UButton>(Widget) || Cast<UCheckBox>(Widget) || Cast<USlider>(Widget)
			|| Cast<UComboBoxString>(Widget) || Cast<UEditableTextBox>(Widget);
	}

	// ---- rules ----

	static void LintColors(const FString& Asset, const TArray<UWidget*>& Widgets, const TArray<FLinearColor>& Tokens, TArray<FMDLintIssue>& Out)
	{
		static const TCHAR* const ColorProperties[] = {
			TEXT("ColorAndOpacity"), TEXT("BrushColor"), TEXT("ContentColorAndOpacity"), TEXT("FillColorAndOpacity"), TEXT("BackgroundColor"),
		};
		for (const UWidget* Widget : Widgets)
		{
			// MD widgets take every colour from the style in SynchronizeProperties.
			if (!Widget || Cast<UMDText>(Widget) || Cast<UMDButton>(Widget) || Cast<UMDPanel>(Widget) || Cast<UMDResourceBar>(Widget))
			{
				continue;
			}
			for (const TCHAR* PropertyName : ColorProperties)
			{
				const FLinearColor* Color = FindStruct<FLinearColor>(Widget, PropertyName, TEXT("LinearColor"));
				if (Color && !IsAllowedColor(*Color, Tokens))
				{
					Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L1"), EMDLintSeverity::Error,
						FString::Printf(TEXT("%s is a hard-coded colour %s; use white, transparent or an EMDColor token"), PropertyName, *ColorToText(*Color))));
				}
			}
		}
	}

	static void LintGraphColors(const FString& Asset, const UBlueprint* BP, const TArray<FLinearColor>& Tokens, TArray<FMDLintIssue>& Out)
	{
		TArray<UEdGraph*> Graphs;
		GatherGraphs(BP, Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}
				if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const FString Member = Call->FunctionReference.GetMemberName().ToString();
					if (Member == TEXT("MakeColor") || Member == TEXT("MakeLinearColor") || Member == TEXT("MakeSlateColor"))
					{
						Out.Add(MakeIssue(Asset, Graph->GetName() + TEXT("/") + Node->GetName(), TEXT("L1"), EMDLintSeverity::Error,
							FString::Printf(TEXT("literal colour node '%s'; use an EMDColor token"), *Member)));
						continue;
					}
				}
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0 || Pin->bHidden
						|| Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct || Pin->DefaultValue.IsEmpty())
					{
						continue;
					}
					const UObject* Struct = Pin->PinType.PinSubCategoryObject.Get();
					if (!Struct || (Struct->GetFName() != FName(TEXT("LinearColor")) && Struct->GetFName() != FName(TEXT("SlateColor"))))
					{
						continue;
					}
					FLinearColor Color;
					if (TryParseColorDefault(Pin->DefaultValue, Color) && !IsAllowedColor(Color, Tokens))
					{
						Out.Add(MakeIssue(Asset, Graph->GetName() + TEXT("/") + Node->GetName(), TEXT("L1"), EMDLintSeverity::Error,
							FString::Printf(TEXT("literal colour %s on pin '%s'; use an EMDColor token"), *ColorToText(Color), *Pin->PinName.ToString())));
					}
				}
			}
		}
	}

	static void LintRawWidgets(const FString& Asset, const TArray<UWidget*>& Widgets, TArray<FMDLintIssue>& Out)
	{
		for (const UWidget* Widget : Widgets)
		{
			if (!Widget)
			{
				continue;
			}
			if (Cast<UTextBlock>(Widget) && !Cast<UMDText>(Widget))
			{
				Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L2"), EMDLintSeverity::Error, TEXT("raw TextBlock; use MD Text")));
			}
			else if (Cast<UButton>(Widget) && !Cast<UMDButton>(Widget))
			{
				Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L2"), EMDLintSeverity::Error, TEXT("raw Button; use MD Button")));
			}

			if (Cast<UMDPanel>(Widget))
			{
				continue; // MakeBox: zero radius by construction
			}
			static const TCHAR* const BrushProperties[] = { TEXT("Brush"), TEXT("Background") };
			for (const TCHAR* PropertyName : BrushProperties)
			{
				const FSlateBrush* Brush = FindStruct<FSlateBrush>(Widget, PropertyName, TEXT("SlateBrush"));
				if (Brush && IsRounded(*Brush))
				{
					Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L2"), EMDLintSeverity::Error,
						FString::Printf(TEXT("%s has rounded corners; the style is sharp-cornered"), PropertyName)));
				}
			}
		}
	}

	static bool HasFocusCall(const UBlueprint* BP)
	{
		TArray<UEdGraph*> Graphs;
		GatherGraphs(BP, Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const FString Member = Call->FunctionReference.GetMemberName().ToString();
					if (Member.StartsWith(TEXT("SetFocus")) || Member.StartsWith(TEXT("SetKeyboardFocus")) || Member.StartsWith(TEXT("SetUserFocus")))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	static bool IsMenuAsset(const UWidgetBlueprint* WidgetBP, const FString& Name)
	{
		if (NameIn(Name, MenuAssets, UE_ARRAY_COUNT(MenuAssets)))
		{
			return true;
		}
		const UClass* Parent = WidgetBP->ParentClass;
		return Parent && (Parent->IsChildOf(UMDGameMenuWidget::StaticClass()) || Parent->IsChildOf(UMDPerksWidget::StaticClass()));
	}

	static void LintFocus(const FString& Asset, const UWidgetBlueprint* WidgetBP, const TArray<UWidget*>& Widgets, TArray<FMDLintIssue>& Out)
	{
		bool bFocusable = false;
		for (const UWidget* Widget : Widgets)
		{
			if (Widget && IsFocusableControl(Widget) && !IsHiddenVisibility(Widget->GetVisibility()))
			{
				bFocusable = true;
				break;
			}
			if (const UUserWidget* Nested = Cast<UUserWidget>(Widget))
			{
				bool bNestedFocusable = false;
				if (Nested->GetClass() && ReadBool(Nested->GetClass()->GetDefaultObject(), TEXT("bIsFocusable"), bNestedFocusable) && bNestedFocusable)
				{
					bFocusable = true;
					break;
				}
			}
		}
		bool bSelfFocusable = false;
		if (WidgetBP->GeneratedClass)
		{
			ReadBool(WidgetBP->GeneratedClass->GetDefaultObject(), TEXT("bIsFocusable"), bSelfFocusable);
		}
		if (!bFocusable && !bSelfFocusable)
		{
			Out.Add(MakeIssue(Asset, FString(), TEXT("L3"), EMDLintSeverity::Error, TEXT("menu has no focusable widget (button, checkbox, slider...) and is not itself focusable")));
		}
		if (!HasFocusCall(WidgetBP))
		{
			Out.Add(MakeIssue(Asset, FString(), TEXT("L3"), EMDLintSeverity::Error, TEXT("menu never sets focus (no SetKeyboardFocus / SetUserFocus / SetFocus call in any graph)")));
		}
	}

	static void LintNavigation(const FString& Asset, const FString& Name, const UWidgetBlueprint* WidgetBP, const TArray<UWidget*>& Widgets, TArray<FMDLintIssue>& Out)
	{
		const bool bEdgeAsset = NameIn(Name, EdgeRuleAssets, UE_ARRAY_COUNT(EdgeRuleAssets));
		const EMDLintSeverity BrokenSeverity = bEdgeAsset ? EMDLintSeverity::Error : EMDLintSeverity::Warning;
		bool bAnyExplicit = false;

		for (const UWidget* Widget : Widgets)
		{
			if (!Widget || !Widget->Navigation)
			{
				continue;
			}
			struct FEdge { const TCHAR* Label; const FWidgetNavigationData* Data; };
			const FEdge Edges[] = {
				{ TEXT("Up"), &Widget->Navigation->Up }, { TEXT("Down"), &Widget->Navigation->Down },
				{ TEXT("Left"), &Widget->Navigation->Left }, { TEXT("Right"), &Widget->Navigation->Right },
			};
			for (const FEdge& Edge : Edges)
			{
				if (Edge.Data->Rule != EUINavigationRule::Explicit)
				{
					bAnyExplicit = bAnyExplicit || Edge.Data->Rule == EUINavigationRule::Custom || Edge.Data->Rule == EUINavigationRule::CustomBoundary
						|| Edge.Data->Rule == EUINavigationRule::Wrap || Edge.Data->Rule == EUINavigationRule::Stop;
					continue;
				}
				bAnyExplicit = true;
				const UWidget* Target = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(Edge.Data->WidgetToFocus) : nullptr;
				if (!Target)
				{
					Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L4"), BrokenSeverity,
						FString::Printf(TEXT("%s navigation points at missing widget '%s'"), Edge.Label, *Edge.Data->WidgetToFocus.ToString())));
				}
				else if (IsHiddenVisibility(Target->GetVisibility()))
				{
					Out.Add(MakeIssue(Asset, Widget->GetName(), TEXT("L4"), BrokenSeverity,
						FString::Printf(TEXT("%s navigation points at '%s', which is collapsed or hidden"), Edge.Label, *Edge.Data->WidgetToFocus.ToString())));
				}
			}
		}
		if (bEdgeAsset && !bAnyExplicit)
		{
			Out.Add(MakeIssue(Asset, FString(), TEXT("L4"), EMDLintSeverity::Error, TEXT("no explicit navigation rules (row wrap / edge targets) on a menu that needs them")));
		}
	}

	static void LintBindings(const FString& Asset, const UWidgetBlueprint* WidgetBP, TArray<FMDLintIssue>& Out)
	{
		const bool bHud = Asset.Contains(TEXT("/HUD/")) || FPackageName::GetShortName(Asset).Contains(TEXT("HUD"));
		for (const FDelegateEditorBinding& Binding : WidgetBP->Bindings)
		{
			Out.Add(MakeIssue(Asset, Binding.ObjectName, TEXT("L5"), bHud ? EMDLintSeverity::Error : EMDLintSeverity::Warning,
				FString::Printf(TEXT("property binding %s.%s runs every frame (function '%s'); push the value on change instead"),
					*Binding.ObjectName, *Binding.PropertyName.ToString(), *Binding.FunctionName.ToString())));
		}
	}

	static void LintReferences(const FString& Asset, TArray<FMDLintIssue>& Out)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FName> Dependencies;
		Registry.GetDependencies(FName(*Asset), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Dependency : Dependencies)
		{
			const FString Path = Dependency.ToString();
			if (Path == Asset || !Path.StartsWith(TEXT("/Game/")))
			{
				continue;
			}
			for (const TCHAR* Legacy : LegacyPackages)
			{
				if (Path == Legacy)
				{
					Out.Add(MakeIssue(Asset, FString(), TEXT("L6"), EMDLintSeverity::Error, FString::Printf(TEXT("references legacy asset %s"), *Path)));
				}
			}
			if (!FPackageName::DoesPackageExist(Path))
			{
				Out.Add(MakeIssue(Asset, FString(), TEXT("L6"), EMDLintSeverity::Error, FString::Printf(TEXT("references missing asset %s"), *Path)));
			}
		}
	}

	static void LintUnusedVariables(const FString& Asset, const UWidgetBlueprint* WidgetBP, TArray<FMDLintIssue>& Out)
	{
		TSet<FName> Used;
		TArray<UEdGraph*> Graphs;
		GatherGraphs(WidgetBP, Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
				{
					Used.Add(Variable->VariableReference.GetMemberName());
				}
			}
		}
		for (const FBPVariableDescription& Variable : WidgetBP->NewVariables)
		{
			const bool bExposed = (Variable.PropertyFlags & (CPF_ExposeOnSpawn | CPF_Net)) != 0 || (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0;
			if (!bExposed && !Used.Contains(Variable.VarName))
			{
				Out.Add(MakeIssue(Asset, Variable.VarName.ToString(), TEXT("L7"), EMDLintSeverity::Warning, TEXT("variable is never read or written in this Blueprint")));
			}
		}
	}

	static void LintCompile(const FString& Asset, UWidgetBlueprint* WidgetBP, TArray<FMDLintIssue>& Out)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		if (Results.NumErrors == 0 && Results.NumWarnings == 0)
		{
			return;
		}
		int32 Listed = 0;
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const EMessageSeverity::Type Severity = Message->GetSeverity();
			if (Severity != EMessageSeverity::Error && Severity != EMessageSeverity::Warning && Severity != EMessageSeverity::PerformanceWarning)
			{
				continue;
			}
			if (Listed >= 5)
			{
				break;
			}
			++Listed;
			Out.Add(MakeIssue(Asset, FString(), TEXT("L8"), EMDLintSeverity::Error,
				FString::Printf(TEXT("compile %s: %s"), Severity == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"), *Message->ToText().ToString())));
		}
		const int32 Total = Results.NumErrors + Results.NumWarnings;
		if (Total > Listed)
		{
			Out.Add(MakeIssue(Asset, FString(), TEXT("L8"), EMDLintSeverity::Error,
				FString::Printf(TEXT("%d compile errors and %d warnings in total (first %d listed)"), Results.NumErrors, Results.NumWarnings, Listed)));
		}
	}
}

TArray<FMDLintIssue> UMD_WidgetLintLibrary::LintWidget(UWidgetBlueprint* WidgetBlueprint)
{
	TArray<FMDLintIssue> Issues;
	if (!WidgetBlueprint)
	{
		return Issues;
	}
	const FString Asset = WidgetBlueprint->GetOutermost()->GetName();
	const FString Name = FPackageName::GetShortName(Asset);

	TArray<UWidget*> Widgets;
	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->GetAllWidgets(Widgets);
	}

	// Colour tokens from DA_MDUIStyle (via UMDUISettings), or the built-in defaults.
	TArray<FLinearColor> Tokens;
	if (const UMDUIStyle* Style = UMDUIStatics::GetStyle())
	{
		for (uint8 i = 0; i < static_cast<uint8>(EMDColor::Count); ++i)
		{
			Tokens.Add(Style->GetColor(static_cast<EMDColor>(i)));
		}
	}

	MD_WidgetLint::LintColors(Asset, Widgets, Tokens, Issues);
	MD_WidgetLint::LintGraphColors(Asset, WidgetBlueprint, Tokens, Issues);
	MD_WidgetLint::LintRawWidgets(Asset, Widgets, Issues);
	if (MD_WidgetLint::IsMenuAsset(WidgetBlueprint, Name))
	{
		MD_WidgetLint::LintFocus(Asset, WidgetBlueprint, Widgets, Issues);
	}
	MD_WidgetLint::LintNavigation(Asset, Name, WidgetBlueprint, Widgets, Issues);
	MD_WidgetLint::LintBindings(Asset, WidgetBlueprint, Issues);
	MD_WidgetLint::LintReferences(Asset, Issues);
	MD_WidgetLint::LintUnusedVariables(Asset, WidgetBlueprint, Issues);
	MD_WidgetLint::LintCompile(Asset, WidgetBlueprint, Issues);
	return Issues;
}

TArray<FString> UMD_WidgetLintLibrary::LoadAllowlist()
{
	TArray<FString> Lines;
	const FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("WidgetLintAllow.ini"));
	TArray<FString> Raw;
	if (!FFileHelper::LoadFileToStringArray(Raw, *Path))
	{
		return Lines;
	}
	for (FString Line : Raw)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 3 || Parts[2].TrimStartAndEnd().IsEmpty())
		{
			UE_LOG(LogMDWidgetLint, Warning, TEXT("Ignoring allowlist line without a reason: %s"), *Line);
			continue;
		}
		Lines.Add(Line);
	}
	return Lines;
}

bool UMD_WidgetLintLibrary::IsAllowlisted(const TArray<FString>& AllowLines, const FString& Asset, const FString& Rule)
{
	const FString ShortName = FPackageName::GetShortName(Asset);
	for (const FString& Line : AllowLines)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 3)
		{
			continue;
		}
		const FString EntryAsset = Parts[0].TrimStartAndEnd();
		const FString EntryRule = Parts[1].TrimStartAndEnd();
		const bool bAssetMatches = EntryAsset.Equals(Asset, ESearchCase::IgnoreCase) || EntryAsset.Equals(ShortName, ESearchCase::IgnoreCase);
		const bool bRuleMatches = EntryRule == TEXT("*") || EntryRule.Equals(Rule, ESearchCase::IgnoreCase);
		if (bAssetMatches && bRuleMatches)
		{
			return true;
		}
	}
	return false;
}

TArray<FMDLintIssue> UMD_WidgetLintLibrary::LintAll(const TArray<FString>& Roots)
{
	TArray<FMDLintIssue> Issues;

	TArray<FString> SearchRoots = Roots;
	if (SearchRoots.Num() == 0)
	{
		for (const TCHAR* Root : MD_WidgetLint::DefaultRoots)
		{
			SearchRoots.Add(Root);
		}
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.SearchAllAssets(true);
	Registry.WaitForCompletion();

	FARFilter Filter;
	Filter.ClassPaths.Add(UWidgetBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	for (const FString& Root : SearchRoots)
	{
		Filter.PackagePaths.Add(FName(*Root));
	}
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

	const TArray<FString> Allow = LoadAllowlist();
	int32 Linted = 0;
	int32 Suppressed = 0;
	for (const FAssetData& Data : Assets)
	{
		const FString Package = Data.PackageName.ToString();
		if (Package.StartsWith(TEXT("/Game/Fab")))
		{
			continue;
		}
		UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Data.GetAsset());
		if (!WidgetBP)
		{
			UE_LOG(LogMDWidgetLint, Warning, TEXT("Could not load %s"), *Package);
			continue;
		}
		++Linted;
		for (const FMDLintIssue& Issue : LintWidget(WidgetBP))
		{
			if (IsAllowlisted(Allow, Issue.Asset, Issue.Rule))
			{
				++Suppressed;
			}
			else
			{
				Issues.Add(Issue);
			}
		}
	}

	UE_LOG(LogMDWidgetLint, Display, TEXT("WidgetLint: %d widgets, %d errors, %d warnings, %d allowlisted"),
		Linted, CountErrors(Issues), Issues.Num() - CountErrors(Issues), Suppressed);
	return Issues;
}

FString UMD_WidgetLintLibrary::GetDefaultReportPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestResults"), TEXT("widgetlint.txt")));
}

int32 UMD_WidgetLintLibrary::CountErrors(const TArray<FMDLintIssue>& Issues)
{
	int32 Count = 0;
	for (const FMDLintIssue& Issue : Issues)
	{
		if (Issue.Severity == EMDLintSeverity::Error)
		{
			++Count;
		}
	}
	return Count;
}

bool UMD_WidgetLintLibrary::WriteReport(const FString& Path, const TArray<FMDLintIssue>& Issues)
{
	const FString Target = Path.IsEmpty() ? GetDefaultReportPath() : Path;

	TMap<FString, int32> ErrorsByRule;
	TMap<FString, int32> WarningsByRule;
	for (const FMDLintIssue& Issue : Issues)
	{
		(Issue.Severity == EMDLintSeverity::Error ? ErrorsByRule : WarningsByRule).FindOrAdd(Issue.Rule)++;
	}

	FString Text;
	Text += FString::Printf(TEXT("WidgetLint: %d errors, %d warnings\n"), CountErrors(Issues), Issues.Num() - CountErrors(Issues));
	for (int32 Rule = 1; Rule <= 8; ++Rule)
	{
		const FString Key = FString::Printf(TEXT("L%d"), Rule);
		Text += FString::Printf(TEXT("  %s: %d errors, %d warnings\n"), *Key, ErrorsByRule.FindRef(Key), WarningsByRule.FindRef(Key));
	}
	Text += TEXT("\n");

	TArray<FMDLintIssue> Sorted = Issues;
	Sorted.StableSort([](const FMDLintIssue& A, const FMDLintIssue& B)
	{
		if (A.Asset != B.Asset)
		{
			return A.Asset < B.Asset;
		}
		return A.Rule < B.Rule;
	});
	for (const FMDLintIssue& Issue : Sorted)
	{
		Text += FString::Printf(TEXT("[%s] %s %s%s%s: %s\n"), Issue.Severity == EMDLintSeverity::Error ? TEXT("Error") : TEXT("Warn"),
			*Issue.Rule, *Issue.Asset, Issue.Widget.IsEmpty() ? TEXT("") : TEXT(" / "), *Issue.Widget, *Issue.Message);
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
	if (!FFileHelper::SaveStringToFile(Text, *Target))
	{
		UE_LOG(LogMDWidgetLint, Warning, TEXT("Could not write %s"), *Target);
		return false;
	}
	UE_LOG(LogMDWidgetLint, Display, TEXT("WidgetLint report written to %s"), *Target);
	return true;
}
