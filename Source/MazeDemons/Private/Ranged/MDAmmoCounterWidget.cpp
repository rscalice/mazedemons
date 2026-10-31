#include "Ranged/MDAmmoCounterWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDUIWidgets.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedWeaponData.h"

bool UMDAmmoCounterWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		UMDPanel* Panel = WidgetTree->ConstructWidget<UMDPanel>(UMDPanel::StaticClass(), TEXT("Panel"));
		Panel->SetPadding(FMargin(10.0f, 6.0f));
		WidgetTree->RootWidget = Panel;

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		Panel->SetContent(Row);

		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconBox"));
		IconBox->SetWidthOverride(40.0f);
		IconBox->SetHeightOverride(40.0f);
		if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconBox))
		{
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}
		WeaponIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("WeaponIcon"));
		WeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
		IconBox->AddChild(WeaponIcon);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		if (UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Column))
		{
			ColumnSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
			ColumnSlot->SetVerticalAlignment(VAlign_Center);
		}

		NumberRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NumberRow"));
		Column->AddChildToVerticalBox(NumberRow);

		MagText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("MagText"));
		MagText->SetTextStyle(EMDText::Numeric);
		NumberRow->AddChildToHorizontalBox(MagText);

		SlashText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("Slash"));
		SlashText->SetTextStyle(EMDText::Body);
		SlashText->SetColorToken(EMDColor::TextSecondary);
		SlashText->SetText(FText::FromString(TEXT(" / ")));
		NumberRow->AddChildToHorizontalBox(SlashText);

		ReserveText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("ReserveText"));
		ReserveText->SetTextStyle(EMDText::Body);
		ReserveText->SetColorToken(EMDColor::TextSecondary);
		NumberRow->AddChildToHorizontalBox(ReserveText);

		USizeBox* ReloadSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReloadSize"));
		ReloadSize->SetHeightOverride(6.0f);
		ReloadSize->SetMinDesiredWidth(96.0f);
		if (UVerticalBoxSlot* ReloadSlot = Column->AddChildToVerticalBox(ReloadSize))
		{
			ReloadSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		}
		ReloadBar = WidgetTree->ConstructWidget<UMDResourceBar>(UMDResourceBar::StaticClass(), TEXT("ReloadBar"));
		ReloadBar->SetFillToken(EMDColor::Accent);
		ReloadSize->AddChild(ReloadBar);
		ReloadSize->SetVisibility(ESlateVisibility::Collapsed);

		PromptText = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), TEXT("PromptText"));
		PromptText->SetTextStyle(EMDText::Caption);
		PromptText->SetColorToken(EMDColor::Danger);
		PromptText->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(PromptText);

		// The heat / charge meter: a thin vertical bar on the right, only for beam weapons.
		HeatBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HeatBox"));
		HeatBox->SetWidthOverride(8.0f);
		HeatBox->SetHeightOverride(40.0f);
		if (UHorizontalBoxSlot* HeatSlot = Row->AddChildToHorizontalBox(HeatBox))
		{
			HeatSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
			HeatSlot->SetVerticalAlignment(VAlign_Center);
		}
		HeatBar = WidgetTree->ConstructWidget<UMDResourceBar>(UMDResourceBar::StaticClass(), TEXT("HeatBar"));
		HeatBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		HeatBar->SetFillToken(EMDColor::Warning);
		HeatBox->AddChild(HeatBar);
		HeatBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Starts hidden; the fade brings it in when the player swaps to a gun.
	SetRenderOpacity(0.0f);
	SetVisibility(ESlateVisibility::HitTestInvisible); // never Hidden/Collapsed: a non-visible widget stops ticking and could never fade back in; opacity 0 hides it
	return bResult;
}

UMDRangedComponent* UMDAmmoCounterWidget::GetRanged() const
{
	if (UMDRangedComponent* Override = RangedOverride.Get())
	{
		return Override;
	}
	return UMDRangedComponent::Find(GetOwningPlayer());
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
	if (!MagText || !ReserveText || !PromptText || !ReloadBar || !HeatBar || !HeatBox || !NumberRow)
	{
		return;
	}
	const UMDRangedComponent* RangedComp = GetRanged();
	const bool bHasGun = RangedComp && RangedComp->HasGunEquipped();
	TargetOpacity = FMDRangedUIRules::CounterTargetOpacity(RangedComp && RangedComp->IsRangedMode(), bHasGun);
	if (!bHasGun)
	{
		IconRow = NAME_None;
		Prompt = EMDAmmoPrompt::None;
		HeatState = EMDHeatMeterState::Hidden;
		HeatFraction = 0.0f;
		return;
	}

	// ---- weapon icon (cached by row: the table lookup loads a texture) ----
	const UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetOwningPlayer());
	const FName GunRow = Inventory ? Inventory->GetEquippedRangedRow() : NAME_None;
	if (GunRow != IconRow)
	{
		IconRow = GunRow;
		UTexture2D* Icon = Inventory ? Inventory->GetItemIcon(GunRow) : nullptr;
		if (WeaponIcon)
		{
			if (Icon)
			{
				WeaponIcon->SetBrushFromTexture(Icon, false);
				WeaponIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				WeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	const UMDRangedWeaponData* Data = RangedComp->GetEquippedWeaponData();
	const bool bBeam = RangedComp->IsBeamWeapon();
	const bool bUsesAmmo = Data && !Data->AmmoRow.IsNone() && !bBeam;
	bAmmoSection = bUsesAmmo;

	// ---- magazine / reserve ----
	const int32 Mag = RangedComp->GetViewMagRounds();
	const int32 MagSize = RangedComp->GetMagSize();
	const int32 Reserve = RangedComp->GetReserveAmmo();
	const bool bReloading = RangedComp->IsReloading();
	NumberRow->SetVisibility(bUsesAmmo ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	MagText->SetText(FText::AsNumber(Mag));
	ReserveText->SetText(FText::AsNumber(Reserve));
	bMagRed = bUsesAmmo && FMDAmmoRules::IsMagLow(Mag, MagSize);
	MagText->SetColorToken(bMagRed ? EMDColor::Danger : EMDColor::TextPrimary);

	// ---- reload bar + prompt (RELOAD pulses at an empty magazine that can be refilled; NO AMMO when the bag is empty too) ----
	UWidget* ReloadSize = ReloadBar->GetParent();
	const bool bShowReloadBar = bUsesAmmo && bReloading;
	if (bShowReloadBar)
	{
		ReloadBar->SetPercent(RangedComp->GetReloadProgress());
	}
	if (ReloadSize)
	{
		ReloadSize->SetVisibility(bShowReloadBar ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	Prompt = bUsesAmmo ? FMDRangedUIRules::AmmoPrompt(Mag, Reserve, bReloading) : EMDAmmoPrompt::None;

	// ---- heat / charge meter (R5: real values from UMDBeamComponent; the bar unwinds with the overheat lock) ----
	const bool bCharge = RangedComp->IsChargeStyleBeam();
	const float Value = bCharge ? RangedComp->GetCharge() : RangedComp->GetHeat();
	const float Max = bCharge ? RangedComp->GetMaxCharge() : RangedComp->GetMaxHeat();
	HeatState = FMDRangedUIRules::HeatMeterState(bBeam, bCharge, Value, Max, RangedComp->IsOverheated());
	HeatFraction = FMDRangedUIRules::MeterFraction(Value, Max);
	if (HeatState == EMDHeatMeterState::Hidden)
	{
		HeatBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		HeatBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		HeatBar->SetPercent(FMDRangedUIRules::HeatRingSweep(HeatState, HeatFraction, RangedComp->GetOverheatLockFraction()));
		HeatBar->SetFillToken(FMDRangedUIRules::HeatMeterToken(HeatState));
	}
	if (HeatState == EMDHeatMeterState::Overheated)
	{
		PromptText->SetText(NSLOCTEXT("MDRangedHUD", "Overheat", "OVERHEAT"));
	}

	const bool bShowPrompt = Prompt != EMDAmmoPrompt::None || HeatState == EMDHeatMeterState::Overheated;
	if (Prompt == EMDAmmoPrompt::Reload)
	{
		PromptText->SetText(NSLOCTEXT("MDRangedHUD", "Reload", "RELOAD"));
	}
	else if (Prompt == EMDAmmoPrompt::NoAmmo)
	{
		PromptText->SetText(NSLOCTEXT("MDRangedHUD", "NoAmmo", "NO AMMO"));
	}
	if (bShowPrompt)
	{
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

void UMDAmmoCounterWidget::TickFade(float DeltaSeconds)
{
	ShownOpacity = FMDRangedUIRules::StepOpacity(ShownOpacity, TargetOpacity, DeltaSeconds);
	SetRenderOpacity(ShownOpacity);
	// Visibility stays HitTestInvisible at all times (see Initialize): the render opacity alone hides the counter.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMDAmmoCounterWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
	TickFade(InDeltaTime);
}
