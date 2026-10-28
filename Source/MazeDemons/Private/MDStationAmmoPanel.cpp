#include "MDStationAmmoPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPlayerController.h"
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

int32 UMDStationAmmoPanel::NextEnabledTab(int32 Current, int32 Delta, bool bRepair, bool bUpgrade, bool bRespec, bool bAmmo)
{
	const bool Enabled[NumTabs] = { bRepair, bUpgrade, bRespec, bAmmo };
	const int32 Step = Delta >= 0 ? 1 : -1;
	const int32 Start = FMath::Clamp(Current, 0, NumTabs - 1);
	for (int32 i = 1; i < NumTabs; ++i)
	{
		const int32 Candidate = ((Start + Step * i) % NumTabs + NumTabs) % NumTabs;
		if (Enabled[Candidate])
		{
			return Candidate;
		}
	}
	return Start;
}

int32 UMDStationAmmoPanel::PickDefaultTab(bool bRepair, bool bUpgrade, bool bRespec, bool bAmmo)
{
	if (bRepair)
	{
		return RepairTab;
	}
	if (bUpgrade)
	{
		return UpgradeTab;
	}
	if (bRespec)
	{
		return RespecTab;
	}
	return bAmmo ? AmmoTab : RepairTab;
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

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	WidgetTree->RootWidget = Column;

	UMDText* Title = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("Title"));
	Title->SetTextStyle(EMDText::H2);
	Title->SetText(NSLOCTEXT("MDStation", "AmmoTitle", "Ammo"));
	Column->AddChildToVerticalBox(Title);

	UMDText* Explain = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("Explain"));
	Explain->SetTextStyle(EMDText::Body);
	Explain->SetColorToken(EMDColor::TextSecondary);
	Explain->SetText(NSLOCTEXT("MDStation", "AmmoExplain", "Bundles of 10 rounds. You can carry one full stack."));
	if (UVerticalBoxSlot* ExplainSlot = Column->AddChildToVerticalBox(Explain))
	{
		ExplainSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 8.0f));
	}

	for (const FName AmmoRow : GetSoldRows())
	{
		const FString Suffix = AmmoRow.ToString();
		FMDAmmoRowUI Entry;
		Entry.AmmoRow = AmmoRow;

		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *(TEXT("Line_") + Suffix));
		if (UVerticalBoxSlot* LineSlot = Column->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
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
		Entry.Button->AddChild(Entry.ButtonLabel);
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

void UMDStationAmmoPanel::SetStation(AMDStation* InStation)
{
	StationOverride = InStation;
}

AMDStation* UMDStationAmmoPanel::GetStation() const
{
	if (AMDStation* Override = StationOverride.Get())
	{
		return Override;
	}
	const AMDPlayerController* PC = Cast<AMDPlayerController>(GetOwningPlayer());
	return PC ? PC->GetOpenStation() : nullptr;
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
	AMDStation* OpenStation = GetStation();
	if (Inventory && OpenStation)
	{
		Inventory->RequestBuyAmmo(OpenStation, AmmoRow);
	}
}

bool UMDStationAmmoPanel::IsRowBuyEnabled(int32 Index) const
{
	return Rows.IsValidIndex(Index) && Rows[Index].Button && Rows[Index].Button->GetIsEnabled();
}

void UMDStationAmmoPanel::Refresh()
{
	const UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetOwningPlayer());
	const AMDStation* OpenStation = GetStation();
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

void UMDStationAmmoPanel::UpdateTabState(int32 CurrentTab, UMDButton* TabButton, UMDText* TabLabel, UWidget* ListPanel)
{
	const AMDStation* OpenStation = GetStation();
	const bool bOffered = OpenStation && OpenStation->Supports(EMDStationService::Ammo);
	const bool bActive = CurrentTab == AmmoTab;
	if (TabButton)
	{
		TabButton->SetIsEnabled(bOffered);
	}
	if (TabLabel)
	{
		TabLabel->SetColorToken(bActive ? EMDColor::TextPrimary : EMDColor::TextSecondary);
	}
	SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bActive && ListPanel)
	{
		ListPanel->SetVisibility(ESlateVisibility::Collapsed); // the Ammo tab has no item list, like Respec
	}
	if (bActive && !bWasActive)
	{
		SelectedRow = 0;
		Refresh();
		FocusSelectedRow();
	}
	bWasActive = bActive;
}

void UMDStationAmmoPanel::FocusSelectedRow()
{
	if (Rows.IsValidIndex(SelectedRow) && Rows[SelectedRow].Button)
	{
		Rows[SelectedRow].Button->SetKeyboardFocus();
	}
}

void UMDStationAmmoPanel::MoveFocus(int32 Delta)
{
	if (Rows.Num() == 0)
	{
		return;
	}
	SelectedRow = FMath::Clamp(SelectedRow + Delta, 0, Rows.Num() - 1);
	FocusSelectedRow();
}

void UMDStationAmmoPanel::ConfirmSelected()
{
	if (IsRowBuyEnabled(SelectedRow))
	{
		BuyRow(Rows[SelectedRow].AmmoRow);
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
