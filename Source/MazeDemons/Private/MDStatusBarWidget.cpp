#include "MDStatusBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "MDStatusEffectComponent.h"
#include "MDStatusEffects.h"
#include "MDUITypes.h"
#include "MDUIWidgets.h"

bool UMDStatusBarWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		WidgetTree->RootWidget = Row;

		// Fixed order: Bleed, Poison, Burn, Frost. A chip only takes space while its status is active.
		for (const EMDStatusType Type : { EMDStatusType::Bleed, EMDStatusType::Poison, EMDStatusType::Burn, EMDStatusType::Frost })
		{
			const FString Name = StaticEnum<EMDStatusType>()->GetNameStringByValue(static_cast<int64>(Type));
			UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("Chip_%s"), *Name));
			Frame->SetPadding(FMargin(8.0f, 3.0f));
			Frame->SetBrushColor(FMDStatusRules::GetColor(Type) * FLinearColor(0.45f, 0.45f, 0.45f, 0.85f));
			Frame->SetVisibility(ESlateVisibility::Collapsed);

			UMDText* Label = WidgetTree->ConstructWidget<UMDText>(UMDText::StaticClass(), *FString::Printf(TEXT("Label_%s"), *Name));
			Label->SetTextStyle(EMDText::Caption);
			Label->SetColorToken(EMDColor::TextPrimary);
			Frame->SetContent(Label);

			if (UHorizontalBoxSlot* ChipSlot = Row->AddChildToHorizontalBox(Frame))
			{
				ChipSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			}
			Frames.Add(Frame);
			Labels.Add(Label);
			ChipTypes.Add(Type);
		}
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bResult;
}

void UMDStatusBarWidget::SetSource(UMDStatusEffectComponent* InSource)
{
	SourceOverride = InSource;
}

UMDStatusEffectComponent* UMDStatusBarWidget::GetSource() const
{
	if (UMDStatusEffectComponent* Override = SourceOverride.Get())
	{
		return Override;
	}
	return UMDStatusEffectComponent::FindFor(GetOwningPlayer());
}

void UMDStatusBarWidget::Refresh()
{
	const UMDStatusEffectComponent* Source = GetSource();
	for (int32 Index = 0; Index < ChipTypes.Num(); ++Index)
	{
		if (!Frames.IsValidIndex(Index) || !Labels.IsValidIndex(Index) || !Frames[Index] || !Labels[Index])
		{
			continue;
		}
		const EMDStatusType Type = ChipTypes[Index];
		const int32 Stacks = Source ? Source->GetStacks(Type) : 0;
		if (Stacks <= 0)
		{
			Frames[Index]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FString Stack = Stacks > 1 ? FString::Printf(TEXT(" x%d"), Stacks) : FString();
		Labels[Index]->SetText(FText::FromString(FString::Printf(TEXT("%s%s  %.1f"), *FMDStatusRules::GetDisplayName(Type).ToString(), *Stack, Source->GetRemainingSeconds(Type))));
		Frames[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

int32 UMDStatusBarWidget::GetVisibleChipCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<UBorder>& Frame : Frames)
	{
		Count += (Frame && Frame->GetVisibility() != ESlateVisibility::Collapsed) ? 1 : 0;
	}
	return Count;
}

bool UMDStatusBarWidget::IsChipVisible(EMDStatusType Type) const
{
	const int32 Index = IndexOf(Type);
	return Frames.IsValidIndex(Index) && Frames[Index] && Frames[Index]->GetVisibility() != ESlateVisibility::Collapsed;
}

FText UMDStatusBarWidget::GetChipText(EMDStatusType Type) const
{
	const int32 Index = IndexOf(Type);
	return IsChipVisible(Type) && Labels.IsValidIndex(Index) && Labels[Index] ? Labels[Index]->GetText() : FText::GetEmpty();
}

void UMDStatusBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}
