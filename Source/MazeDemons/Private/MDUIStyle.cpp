#include "MDUIStyle.h"

#include "Engine/Font.h"
#include "Styling/CoreStyle.h"

namespace
{
	FLinearColor Hex(const TCHAR* Value, float Alpha = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Value));
		C.A = Alpha;
		return C;
	}

	FMDTextDef Def(int32 Size, bool bBold, bool bUpper, int32 Tracking, EMDColor Color, bool bDisplay = false)
	{
		FMDTextDef D;
		D.Size = Size;
		D.bBold = bBold;
		D.bUppercase = bUpper;
		D.Tracking = Tracking;
		D.Color = Color;
		D.bDisplayFont = bDisplay;
		return D;
	}
}

UMDUIStyle::UMDUIStyle()
{
	ResetToDefaults();
}

void UMDUIStyle::ResetToDefaults()
{
	Colors.Empty();
	Colors.Add(EMDColor::Bg0, Hex(TEXT("#0E1012"), 0.85f));
	Colors.Add(EMDColor::Bg1, Hex(TEXT("#16191C")));
	Colors.Add(EMDColor::Bg2, Hex(TEXT("#1F2327")));
	Colors.Add(EMDColor::StrokeSubtle, Hex(TEXT("#2E3338")));
	Colors.Add(EMDColor::StrokeStrong, Hex(TEXT("#4A5157")));
	Colors.Add(EMDColor::TextPrimary, Hex(TEXT("#D8DCDF")));
	Colors.Add(EMDColor::TextSecondary, Hex(TEXT("#9AA1A7")));
	Colors.Add(EMDColor::TextDisabled, Hex(TEXT("#5B6166")));
	Colors.Add(EMDColor::Accent, Hex(TEXT("#B89B5E")));
	Colors.Add(EMDColor::DisabledFill, Hex(TEXT("#2A2E32")));
	Colors.Add(EMDColor::Health, Hex(TEXT("#9E3B3B")));
	Colors.Add(EMDColor::HealthLag, Hex(TEXT("#5E2626")));
	Colors.Add(EMDColor::Stamina, Hex(TEXT("#8A9A4B")));
	Colors.Add(EMDColor::StaminaExhausted, Hex(TEXT("#5A6236")));
	Colors.Add(EMDColor::Gold, Hex(TEXT("#C2A25A")));
	Colors.Add(EMDColor::Upgrade, Hex(TEXT("#6F8FB3")));
	Colors.Add(EMDColor::Warning, Hex(TEXT("#C27A3A")));
	Colors.Add(EMDColor::Danger, Hex(TEXT("#B84A3F")));
	Colors.Add(EMDColor::Success, Hex(TEXT("#6E9468")));
	Colors.Add(EMDColor::RarityCommon, Hex(TEXT("#8C9196")));
	Colors.Add(EMDColor::RarityUncommon, Hex(TEXT("#6E9468")));
	Colors.Add(EMDColor::RarityRare, Hex(TEXT("#5F7FA8")));
	Colors.Add(EMDColor::RarityEpic, Hex(TEXT("#8467A0")));
	Colors.Add(EMDColor::RarityLegendary, Hex(TEXT("#B88A4A")));

	Texts.Empty();
	Texts.Add(EMDText::Display, Def(32, true, true, 80, EMDColor::TextPrimary, true));
	Texts.Add(EMDText::H1, Def(22, true, false, 0, EMDColor::TextPrimary));
	Texts.Add(EMDText::H2, Def(16, true, true, 40, EMDColor::TextPrimary));
	Texts.Add(EMDText::Body, Def(14, false, false, 0, EMDColor::TextPrimary));
	Texts.Add(EMDText::Caption, Def(11, true, true, 0, EMDColor::TextSecondary));
	Texts.Add(EMDText::Numeric, Def(18, true, false, 0, EMDColor::TextPrimary));
	Texts.Add(EMDText::KeyHint, Def(11, true, true, 0, EMDColor::TextSecondary));

	Motion = FMDMotion();
}

FLinearColor UMDUIStyle::GetColor(EMDColor Token) const
{
	if (const FLinearColor* Found = Colors.Find(Token))
	{
		return *Found;
	}
	return FLinearColor(1.0f, 0.0f, 1.0f, 1.0f); // magenta: loud on purpose: a missing token must be noticed
}

FLinearColor UMDUIStyle::RarityColor(EMDRarity Rarity) const
{
	switch (Rarity)
	{
	case EMDRarity::Uncommon: return GetColor(EMDColor::RarityUncommon);
	case EMDRarity::Rare: return GetColor(EMDColor::RarityRare);
	case EMDRarity::Epic: return GetColor(EMDColor::RarityEpic);
	case EMDRarity::Legendary: return GetColor(EMDColor::RarityLegendary);
	default: return GetColor(EMDColor::RarityCommon);
	}
}

FMDTextDef UMDUIStyle::GetTextDef(EMDText Token) const
{
	if (const FMDTextDef* Found = Texts.Find(Token))
	{
		return *Found;
	}
	return FMDTextDef();
}

FSlateFontInfo UMDUIStyle::MakeFont(EMDText Token) const
{
	const FMDTextDef D = GetTextDef(Token);
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(D.bBold ? FName("Bold") : FName("Regular"), D.Size);

	const TSoftObjectPtr<UFont>& Source = D.bDisplayFont && !DisplayFont.IsNull() ? DisplayFont : BodyFont;
	if (UFont* FontAsset = Source.LoadSynchronous())
	{
		Font.FontObject = FontAsset;
		Font.TypefaceFontName = D.bBold ? FName("Bold") : FName("Regular");
		Font.Size = D.Size;
	}
	Font.LetterSpacing = D.Tracking;
	Font.OutlineSettings.OutlineSize = 1;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.4f);
	return Font;
}

FSlateBrush UMDUIStyle::MakeBox(const FLinearColor& Fill, const FLinearColor& Stroke, float StrokeWidth)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Fill);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0); // sharp corners, always
	Brush.OutlineSettings.Color = FSlateColor(Stroke);
	Brush.OutlineSettings.Width = StrokeWidth;
	return Brush;
}

FButtonStyle UMDUIStyle::MakeButtonStyle(EMDButton Kind) const
{
	const FLinearColor Bg1 = GetColor(EMDColor::Bg1);
	const FLinearColor Bg2 = GetColor(EMDColor::Bg2);
	const FLinearColor Subtle = GetColor(EMDColor::StrokeSubtle);
	const FLinearColor Strong = GetColor(EMDColor::StrokeStrong);
	const FLinearColor Accent = GetColor(EMDColor::Accent);
	const FLinearColor Danger = GetColor(EMDColor::Danger);
	const FLinearColor Text = GetColor(EMDColor::TextPrimary);
	const FLinearColor TextOff = GetColor(EMDColor::TextDisabled);

	FLinearColor NormalFill = Bg2;
	FLinearColor NormalStroke = Subtle;
	FLinearColor HoverStroke = Strong;
	FLinearColor Foreground = Text;
	float StrokeWidth = 1.0f;

	switch (Kind)
	{
	case EMDButton::Primary:
		NormalStroke = Accent;
		break;
	case EMDButton::Danger:
		NormalStroke = Danger;
		HoverStroke = Danger;
		Foreground = Danger;
		break;
	case EMDButton::Tab:
		NormalFill = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		NormalStroke = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		break;
	default:
		break;
	}

	const FLinearColor HoverFill = FMath::Lerp(NormalFill.A > 0.0f ? NormalFill : Bg2, FLinearColor::White, 0.06f);
	FLinearColor DisabledStroke = Subtle;
	DisabledStroke.A = 0.5f;

	FButtonStyle Style;
	Style.SetNormal(MakeBox(NormalFill, NormalStroke, StrokeWidth));
	Style.SetHovered(MakeBox(HoverFill, HoverStroke, StrokeWidth));
	Style.SetPressed(MakeBox(Bg1, Accent, StrokeWidth));
	Style.SetDisabled(MakeBox(GetColor(EMDColor::DisabledFill), DisabledStroke, StrokeWidth));
	Style.SetNormalForeground(FSlateColor(Foreground));
	Style.SetHoveredForeground(FSlateColor(Foreground));
	Style.SetPressedForeground(FSlateColor(Foreground));
	Style.SetDisabledForeground(FSlateColor(TextOff));
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f)); // 1 px y-offset on press
	return Style;
}

float UMDUIStyle::ContrastRatio(const FLinearColor& A, const FLinearColor& B)
{
	auto Luminance = [](const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	};
	const float La = Luminance(A);
	const float Lb = Luminance(B);
	const float Hi = FMath::Max(La, Lb);
	const float Lo = FMath::Min(La, Lb);
	return (Hi + 0.05f) / (Lo + 0.05f);
}
