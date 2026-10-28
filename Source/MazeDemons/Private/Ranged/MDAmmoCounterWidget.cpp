#include "Ranged/MDAmmoCounterWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "MDUIWidgets.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDRangedComponent.h"

bool UMDAmmoCounterWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		UMDPanel* Panel = WidgetTree->ConstructWidget<UMDPanel>(UMDPanel::StaticClass(), TEXT("Panel"));
		Panel->SetPadding(FMargin(14.0f, 8.0f));
		if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
		{
			PanelSlot->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
			PanelSlot->SetAlignment(FVector2D(1.0f, 1.0f));
			PanelSlot->SetPosition(FVector2D(-40.0f, -150.0f));
			PanelSlot->SetAutoSize(true);
		}

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		Panel->SetContent(Column);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		Column->AddChildToVerticalBox(Row);

		MagText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("MagText"));
		MagText->SetTextStyle(EMDText::Numeric);
		Row->AddChildToHorizontalBox(MagText);

		UMDText* Slash = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("Slash"));
		Slash->SetTextStyle(EMDText::Body);
		Slash->SetColorToken(EMDColor::TextSecondary);
		Slash->SetText(FText::FromString(TEXT(" / ")));
		Row->AddChildToHorizontalBox(Slash);

		ReserveText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("ReserveText"));
		ReserveText->SetTextStyle(EMDText::Body);
		ReserveText->SetColorToken(EMDColor::TextSecondary);
		Row->AddChildToHorizontalBox(ReserveText);

		ReloadBar = WidgetTree->ConstructWidget<UMDResourceBar>(UMDResourceBar::StaticClass(), TEXT("ReloadBar"));
		ReloadBar->SetFillToken(EMDColor::Accent);
		ReloadBar->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(ReloadBar);

		PromptText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("PromptText"));
		PromptText->SetTextStyle(EMDText::Caption);
		PromptText->SetColorToken(EMDColor::Danger);
		PromptText->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(PromptText);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bResult;
}

FText UMDAmmoCounterWidget::GetMagText() const
{
	return MagText ? MagText->GetText() : FText::GetEmpty();
}

FText UMDAmmoCounterWidget::GetReserveText() const
{
	return ReserveText ? ReserveText->GetText() : FText::GetEmpty();
}

void UMDAmmoCounterWidget::Refresh()
{
	const UMDRangedComponent* RangedComp = Ranged.Get();
	if (!RangedComp || !MagText || !ReserveText || !PromptText || !ReloadBar)
	{
		return;
	}
	const int32 Mag = RangedComp->GetViewMagRounds();
	const int32 MagSize = RangedComp->GetMagSize();
	const int32 Reserve = RangedComp->GetReserveAmmo();
	const bool bReloading = RangedComp->IsReloading();

	MagText->SetText(FText::AsNumber(Mag));
	ReserveText->SetText(FText::AsNumber(Reserve));

	bMagRed = FMDAmmoRules::IsMagLow(Mag, MagSize);
	MagText->SetColorToken(bMagRed ? EMDColor::Danger : EMDColor::TextPrimary);

	// RELOAD pulses at an empty magazine that can actually be refilled; NO AMMO when the bag has nothing either.
	bReloadPrompt = Mag <= 0 && !bReloading;
	if (bReloading)
	{
		ReloadBar->SetPercent(RangedComp->GetReloadProgress());
		ReloadBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		ReloadBar->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bReloadPrompt)
	{
		PromptText->SetText(Reserve > 0 ? FText::FromString(TEXT("RELOAD")) : FText::FromString(TEXT("NO AMMO")));
		PromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
		const UWorld* World = GetWorld();
		const float Time = World ? World->GetRealTimeSeconds() : 0.0f;
		PromptText->SetRenderOpacity(0.55f + 0.45f * (0.5f + 0.5f * FMath::Sin(Time * 8.0f)));
	}
	else
	{
		PromptText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMDAmmoCounterWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}
