#include "MDStationAmmoPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDStation.h"
#include "MDUIWidgets.h"

namespace MDStationAmmoPanelLocal
{
	const FName RowLight(TEXT("AmmoLight"));
	const FName RowHeavy(TEXT("AmmoHeavy"));
}

TArray<FName> UMDStationAmmoPanel::GetSoldRows()
{
	return { MDStationAmmoPanelLocal::RowLight, MDStationAmmoPanelLocal::RowHeavy };
}

bool UMDStationAmmoPanel::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget)
	{
		return bResult;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UMDPanel* Panel = WidgetTree->ConstructWidget<UMDPanel>(UMDPanel::StaticClass(), TEXT("Panel"));
	Panel->SetPadding(FMargin(16.0f, 12.0f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.0f, 0.5f, 1.0f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(1.0f, 0.5f));
		PanelSlot->SetPosition(FVector2D(-32.0f, 0.0f));
		PanelSlot->SetAutoSize(true);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	UMDText* Title = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("Title"));
	Title->SetTextStyle(EMDText::Display);
	Title->SetText(NSLOCTEXT("MDStation", "AmmoTitle", "Ammo"));
	Column->AddChildToVerticalBox(Title);

	for (const FName AmmoRow : GetSoldRows())
	{
		const FString Suffix = AmmoRow.ToString();
		FMDAmmoRowUI Entry;
		Entry.AmmoRow = AmmoRow;

		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *(TEXT("Line_") + Suffix));
		if (UVerticalBoxSlot* LineSlot = Column->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
		}

		Entry.Label = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), *(TEXT("Label_") + Suffix));
		Entry.Label->SetTextStyle(EMDText::Body);
		if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Entry.Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
		}

		Entry.Button = WidgetTree->ConstructWidget<UMDButton>(UMDButton::StaticClass(), *(TEXT("Buy_") + Suffix));
		Entry.Button->ButtonKind = EMDButton::Primary;
		Entry.ButtonLabel = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), *(TEXT("BuyLabel_") + Suffix));
		Entry.ButtonLabel->SetTextStyle(EMDText::Body);
		Entry.Button->SetContent(Entry.ButtonLabel);
		if (AmmoRow == MDStationAmmoPanelLocal::RowLight)
		{
			Entry.Button->OnClicked.AddDynamic(this, &UMDStationAmmoPanel::OnBuyLight);
		}
		else
		{
			Entry.Button->OnClicked.AddDynamic(this, &UMDStationAmmoPanel::OnBuyHeavy);
		}
		Line->AddChildToHorizontalBox(Entry.Button);

		Entry.Reason = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), *(TEXT("Reason_") + Suffix));
		Entry.Reason->SetTextStyle(EMDText::Caption);
		Entry.Reason->SetColorToken(EMDColor::Warning);
		Entry.Reason->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(Entry.Reason);

		Rows.Add(Entry);
	}
	return bResult;
}

void UMDStationAmmoPanel::OnBuyLight()
{
	BuyRow(MDStationAmmoPanelLocal::RowLight);
}

void UMDStationAmmoPanel::OnBuyHeavy()
{
	BuyRow(MDStationAmmoPanelLocal::RowHeavy);
}

void UMDStationAmmoPanel::BuyRow(FName AmmoRow)
{
	UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetOwningPlayer());
	AMDStation* OpenStation = Station.Get();
	if (Inventory && OpenStation)
	{
		Inventory->RequestBuyAmmo(OpenStation, AmmoRow);
	}
}

void UMDStationAmmoPanel::Refresh()
{
	const UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetOwningPlayer());
	const AMDStation* OpenStation = Station.Get();
	if (!Inventory || !OpenStation)
	{
		return;
	}
	for (FMDAmmoRowUI& Entry : Rows)
	{
		if (!Entry.Label || !Entry.Button || !Entry.ButtonLabel || !Entry.Reason)
		{
			continue;
		}
		int32 Qty = 0;
		int32 Gold = 0;
		const EMDStationReject Verdict = Inventory->GetBuyAmmoReject(OpenStation, Entry.AmmoRow, Qty, Gold);
		Entry.Label->SetText(FText::Format(NSLOCTEXT("MDStation", "AmmoRowLabel", "{0}   {1} / {2}"),
			UMDGameplayStatics::GetItemDisplayName(Entry.AmmoRow), FText::AsNumber(Inventory->GetAmmoCount(Entry.AmmoRow)), FText::AsNumber(Inventory->GetAmmoCap(Entry.AmmoRow))));

		const bool bBuyable = Verdict == EMDStationReject::None;
		Entry.Button->SetIsEnabled(bBuyable);
		if (Qty > 0 && Verdict != EMDStationReject::AmmoFull && Verdict != EMDStationReject::NotForSale)
		{
			Entry.ButtonLabel->SetText(FText::Format(NSLOCTEXT("MDStation", "AmmoBuyLabel", "Buy {0} - {1} gold"), FText::AsNumber(Qty), FText::AsNumber(Gold)));
		}
		else
		{
			Entry.ButtonLabel->SetText(NSLOCTEXT("MDStation", "AmmoBuyUnavailable", "Buy"));
		}
		if (bBuyable)
		{
			Entry.Reason->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			Entry.Reason->SetText(UMDInventoryComponent::GetStationRejectText(Verdict, Gold, Inventory->GetGold()));
			Entry.Reason->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void UMDStationAmmoPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshTimer -= InDeltaTime;
	if (RefreshTimer <= 0.0f)
	{
		RefreshTimer = 0.2f;
		Refresh();
	}
}
