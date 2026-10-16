#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDUIStatics.h"
#include "MDUIStyle.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDUITokensCompleteTest, "MazeDemons.UI.StyleTokensComplete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDUITokensCompleteTest::RunTest(const FString& Parameters)
{
	const UMDUIStyle* Style = GetDefault<UMDUIStyle>();
	if (!TestNotNull(TEXT("default style"), Style))
	{
		return false;
	}

	for (uint8 i = 0; i < static_cast<uint8>(EMDColor::Count); ++i)
	{
		const EMDColor Token = static_cast<EMDColor>(i);
		TestTrue(*FString::Printf(TEXT("colour token %d is defined"), i), Style->Colors.Contains(Token));
		TestTrue(*FString::Printf(TEXT("colour token %d is visible"), i), Style->GetColor(Token).A > 0.0f);
	}
	for (uint8 i = 0; i < static_cast<uint8>(EMDText::Count); ++i)
	{
		const EMDText Token = static_cast<EMDText>(i);
		TestTrue(*FString::Printf(TEXT("text style %d is defined"), i), Style->Texts.Contains(Token));
		TestTrue(*FString::Printf(TEXT("text style %d has a size"), i), Style->GetTextDef(Token).Size > 0);
	}

	// Snappy: every transition is short, none is zero where one is required, and holds are bounded.
	const FMDMotion& M = Style->Motion;
	TestTrue(TEXT("hover tint is a short positive time"), M.HoverTint > 0.0f && M.HoverTint <= 0.15f);
	TestTrue(TEXT("press is a short positive time"), M.Press > 0.0f && M.Press <= 0.1f);
	TestTrue(TEXT("panel open <= 150 ms"), M.PanelOpen > 0.0f && M.PanelOpen <= 0.15f);
	TestTrue(TEXT("panel close <= 100 ms"), M.PanelClose > 0.0f && M.PanelClose <= 0.1f);
	TestTrue(TEXT("toast in/out are short"), M.ToastIn <= 0.2f && M.ToastOut <= 0.2f);
	TestTrue(TEXT("toast hold is bounded"), M.ToastHold > 0.0f && M.ToastHold <= 5.0f);

	// Ease-out only: monotonic, hits 0 and 1 exactly, and never overshoots (no back/elastic/bounce).
	TestEqual(TEXT("ease(0) = 0"), UMDUIStatics::EaseOutCubic(0.0f), 0.0f);
	TestEqual(TEXT("ease(1) = 1"), UMDUIStatics::EaseOutCubic(1.0f), 1.0f);
	float Previous = 0.0f;
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		const float Value = UMDUIStatics::EaseOutCubic(Step / 20.0f);
		TestTrue(TEXT("ease is monotonic and never overshoots"), Value >= Previous && Value <= 1.0f);
		Previous = Value;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDUIContrastTest, "MazeDemons.UI.ContrastAA", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDUIContrastTest::RunTest(const FString& Parameters)
{
	const UMDUIStyle* Style = GetDefault<UMDUIStyle>();
	const FLinearColor Bg1 = Style->GetColor(EMDColor::Bg1);
	const FLinearColor Bg2 = Style->GetColor(EMDColor::Bg2);

	// Tier 1: anything used for body-size text must reach WCAG AA (4.5:1) on the panel colour.
	const EMDColor BodyText[] = {
		EMDColor::TextPrimary, EMDColor::TextSecondary, EMDColor::Accent, EMDColor::Gold, EMDColor::Upgrade,
		EMDColor::Warning, EMDColor::Success, EMDColor::RarityCommon, EMDColor::RarityUncommon, EMDColor::RarityLegendary,
	};
	for (const EMDColor Token : BodyText)
	{
		const float Ratio = UMDUIStyle::ContrastRatio(Style->GetColor(Token), Bg1);
		TestTrue(*FString::Printf(TEXT("body-text token %d reaches 4.5:1 on Bg1 (got %.2f)"), static_cast<int32>(Token), Ratio), Ratio >= 4.5f);
	}

	// Tier 2: tokens only used for bold titles, strokes and fills (WCAG large-text / UI-component threshold, 3:1).
	const EMDColor TitleAndStroke[] = { EMDColor::Danger, EMDColor::RarityRare, EMDColor::RarityEpic };
	for (const EMDColor Token : TitleAndStroke)
	{
		const float Ratio = UMDUIStyle::ContrastRatio(Style->GetColor(Token), Bg1);
		TestTrue(*FString::Printf(TEXT("title/stroke token %d reaches 3:1 on Bg1 (got %.2f)"), static_cast<int32>(Token), Ratio), Ratio >= 3.0f);
	}
	TestTrue(TEXT("primary text reaches 4.5:1 on Bg2 slots"),
		UMDUIStyle::ContrastRatio(Style->GetColor(EMDColor::TextPrimary), Bg2) >= 4.5f);

	// Sanity: the formula itself (black on white is 21:1).
	TestEqual(TEXT("black on white is 21:1"), UMDUIStyle::ContrastRatio(FLinearColor::Black, FLinearColor::White), 21.0f, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
