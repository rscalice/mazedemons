#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MD_WidgetLintLibrary.h"

#include "Misc/AutomationTest.h"

// Lints every widget Blueprint under /Game/UI and /Game/Blueprints/UI (never /Game/Fab), writes
// Saved/TestResults/widgetlint.txt, and fails only on Errors. Warnings are reported in the log and the file.
// A per-rule fixture-widget test needs editor-created assets under /Game/Tests/UI and is a WP6 item.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDWidgetLintTest, "MazeDemons.UI.WidgetLint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDWidgetLintTest::RunTest(const FString& Parameters)
{
	// Allowlist matching is pure; check it first so a broken parser is not mistaken for lint findings.
	const TArray<FString> Allow = { TEXT("/Game/UI/Menus/W_Pause | L7 | kept on purpose"), TEXT("W_Inventory | * | legacy") };
	TestTrue(TEXT("long name + rule"), UMD_WidgetLintLibrary::IsAllowlisted(Allow, TEXT("/Game/UI/Menus/W_Pause"), TEXT("L7")));
	TestFalse(TEXT("other rule"), UMD_WidgetLintLibrary::IsAllowlisted(Allow, TEXT("/Game/UI/Menus/W_Pause"), TEXT("L1")));
	TestTrue(TEXT("short name + wildcard rule"), UMD_WidgetLintLibrary::IsAllowlisted(Allow, TEXT("/Game/Blueprints/UI/Inventory/W_Inventory"), TEXT("L4")));
	TestFalse(TEXT("other asset"), UMD_WidgetLintLibrary::IsAllowlisted(Allow, TEXT("/Game/UI/HUD/W_MDHUD"), TEXT("L7")));

	const TArray<FMDLintIssue> Issues = UMD_WidgetLintLibrary::LintAll(TArray<FString>());
	TestTrue(TEXT("report written"), UMD_WidgetLintLibrary::WriteReport(FString(), Issues));

	int32 Errors = 0;
	int32 Warnings = 0;
	for (const FMDLintIssue& Issue : Issues)
	{
		if (Issue.Severity == EMDLintSeverity::Error)
		{
			++Errors;
			if (Errors <= 25)
			{
				AddError(FString::Printf(TEXT("%s %s%s%s: %s"), *Issue.Rule, *Issue.Asset, Issue.Widget.IsEmpty() ? TEXT("") : TEXT(" / "), *Issue.Widget, *Issue.Message));
			}
		}
		else
		{
			++Warnings;
		}
	}
	if (Errors > 25)
	{
		AddError(FString::Printf(TEXT("... and %d more errors, see %s"), Errors - 25, *UMD_WidgetLintLibrary::GetDefaultReportPath()));
	}
	AddInfo(FString::Printf(TEXT("WidgetLint: %d errors, %d warnings. Report: %s"), Errors, Warnings, *UMD_WidgetLintLibrary::GetDefaultReportPath()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
