#include "MDUIWidgets.h"

#include "MDUIStatics.h"
#include "MDUIStyle.h"

// ---- UMDText ----

void UMDText::SetTextStyle(EMDText NewStyle)
{
	TextStyle = NewStyle;
	SynchronizeProperties();
}

void UMDText::SetColorToken(EMDColor NewToken)
{
	bOverrideColor = true;
	ColorToken = NewToken;
	SynchronizeProperties();
}

void UMDText::SynchronizeProperties()
{
	const UMDUIStyle* Style = UMDUIStatics::GetStyle();
	const FMDTextDef Def = Style->GetTextDef(TextStyle);

	SetFont(Style->MakeFont(TextStyle));
	SetColorAndOpacity(FSlateColor(Style->GetColor(bOverrideColor ? ColorToken : Def.Color)));
	SetTextTransformPolicy(Def.bUppercase ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
	SetShadowOffset(FVector2D::ZeroVector); // no shadows in this UI

	Super::SynchronizeProperties();
}

// ---- UMDButton ----

void UMDButton::SynchronizeProperties()
{
	SetStyle(UMDUIStatics::GetStyle()->MakeButtonStyle(ButtonKind));
	Super::SynchronizeProperties();
}

// ---- UMDPanel ----

void UMDPanel::SynchronizeProperties()
{
	const UMDUIStyle* Style = UMDUIStatics::GetStyle();
	SetBrush(UMDUIStyle::MakeBox(Style->GetColor(FillToken), Style->GetColor(StrokeToken), StrokeWidth));
	SetBrushColor(FLinearColor::White);
	Super::SynchronizeProperties();
}

// ---- UMDResourceBar ----

void UMDResourceBar::SetFillToken(EMDColor NewToken)
{
	FillToken = NewToken;
	SetFillColorAndOpacity(UMDUIStatics::GetColor(FillToken));
}

void UMDResourceBar::SynchronizeProperties()
{
	const UMDUIStyle* Style = UMDUIStatics::GetStyle();
	WidgetStyle.SetBackgroundImage(UMDUIStyle::MakeBox(Style->GetColor(EMDColor::Bg1), Style->GetColor(EMDColor::StrokeSubtle), 1.0f));
	WidgetStyle.SetFillImage(UMDUIStyle::MakeBox(FLinearColor::White, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), 0.0f));
	WidgetStyle.EnableFillAnimation = false; // the current value snaps; lag bars are separate widgets
	SetBarFillType(EProgressBarFillType::LeftToRight);
	SetFillColorAndOpacity(Style->GetColor(FillToken));
	Super::SynchronizeProperties();
}

// ---- UMDUserWidget ----

void UMDUserWidget::PlayOpen()
{
	Phase = EPhase::Opening;
	Elapsed = 0.0f;
	SetRenderOpacity(0.0f);
	SetRenderTranslation(FVector2D(0.0, UMDUIStatics::GetMotion().PanelSlidePx));
	SetVisibility(ESlateVisibility::Visible);
}

void UMDUserWidget::PlayClose()
{
	if (GetVisibility() == ESlateVisibility::Collapsed || GetVisibility() == ESlateVisibility::Hidden)
	{
		return;
	}
	Phase = EPhase::Closing;
	Elapsed = 0.0f;
}

void UMDUserWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Phase == EPhase::Idle)
	{
		return;
	}

	const FMDMotion Motion = UMDUIStatics::GetMotion();
	Elapsed += InDeltaTime;

	if (Phase == EPhase::Opening)
	{
		const float Duration = FMath::Max(Motion.PanelOpen, KINDA_SMALL_NUMBER);
		const float T = UMDUIStatics::EaseOutCubic(Elapsed / Duration);
		SetRenderOpacity(T);
		SetRenderTranslation(FVector2D(0.0, Motion.PanelSlidePx * (1.0f - T)));
		if (Elapsed >= Duration)
		{
			SetRenderOpacity(1.0f);
			SetRenderTranslation(FVector2D::ZeroVector);
			Phase = EPhase::Idle;
		}
	}
	else
	{
		const float Duration = FMath::Max(Motion.PanelClose, KINDA_SMALL_NUMBER);
		const float T = UMDUIStatics::EaseOutCubic(Elapsed / Duration);
		SetRenderOpacity(1.0f - T);
		if (Elapsed >= Duration)
		{
			SetRenderOpacity(0.0f);
			SetVisibility(ESlateVisibility::Collapsed);
			Phase = EPhase::Idle;
			OnClosed.Broadcast();
		}
	}
}

// ---- UMDItemSlot ----

void UMDItemSlot::SetItem(const FText& InName, int32 InCount, EMDRarity InRarity, UTexture2D* InIcon)
{
	ItemName = InName;
	Count = FMath::Max(InCount, 0);
	Rarity = InRarity;
	Icon = InIcon;
	OnItemChanged();
}

void UMDItemSlot::ClearItem()
{
	ItemName = FText::GetEmpty();
	Count = 0;
	Rarity = EMDRarity::Common;
	Icon = nullptr;
	OnItemChanged();
}

FLinearColor UMDItemSlot::GetRarityColor() const
{
	return UMDUIStatics::RarityColor(Rarity);
}
