// MazeDemons - editor-only WidgetLint (U5). Read-only static checks of Widget Blueprints against UI_STYLE_GUIDE.
// Rules: L1 hard-coded colours, L2 raw TextBlock/Button and rounded corners, L3 menu focus, L4 navigation,
// L5 property bindings, L6 broken or legacy references, L7 unused variables, L8 compile errors/warnings.
// Allowlist: Config/WidgetLintAllow.ini, one "Asset | Rule | Reason" per line. Report: Saved/TestResults/widgetlint.txt.
// Python usage: unreal.MD_WidgetLintLibrary.lint_all([])

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_WidgetLintLibrary.generated.h"

class UWidgetBlueprint;

UENUM(BlueprintType)
enum class EMDLintSeverity : uint8
{
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct MAZEDEMONSEDITOR_API FMDLintIssue
{
	GENERATED_BODY()

	// Long package name of the widget Blueprint, e.g. /Game/UI/Menus/W_Pause.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lint")
	FString Asset;

	// Widget (or graph / variable) the finding is about; empty for asset-level findings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lint")
	FString Widget;

	// "L1" .. "L8".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lint")
	FString Rule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lint")
	EMDLintSeverity Severity = EMDLintSeverity::Warning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lint")
	FString Message;
};

UCLASS()
class MAZEDEMONSEDITOR_API UMD_WidgetLintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Lints one widget Blueprint (loads and compiles nothing else). Raw result: the allowlist is not applied here. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FMDLintIssue> LintWidget(UWidgetBlueprint* WidgetBlueprint);

	/**
	 * Lints every widget Blueprint under the given package roots (empty = /Game/UI and /Game/Blueprints/UI), skipping
	 * /Game/Fab, and drops issues listed in Config/WidgetLintAllow.ini. Logs a summary under LogMDWidgetLint.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FMDLintIssue> LintAll(const TArray<FString>& Roots);

	/** Writes the issues (plus per-rule counts) to Path; empty Path = <Project>/Saved/TestResults/widgetlint.txt. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool WriteReport(const FString& Path, const TArray<FMDLintIssue>& Issues);

	/** Default report location (absolute). */
	UFUNCTION(BlueprintPure, Category = "MazeDemons|Editor")
	static FString GetDefaultReportPath();

	/** Number of issues with Severity == Error. */
	UFUNCTION(BlueprintPure, Category = "MazeDemons|Editor")
	static int32 CountErrors(const TArray<FMDLintIssue>& Issues);

	/** Pure: true when Asset + Rule is covered by an allowlist entry. Entry asset is a long package name or a bare asset name; rule may be "*". */
	static bool IsAllowlisted(const TArray<FString>& AllowLines, const FString& Asset, const FString& Rule);

	/** Reads Config/WidgetLintAllow.ini (comments start with ';' or '#'; entries without a reason are ignored). */
	static TArray<FString> LoadAllowlist();
};
