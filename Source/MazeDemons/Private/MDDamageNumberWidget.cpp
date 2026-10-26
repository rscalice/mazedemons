#include "MDDamageNumberWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

FString UMDDamageNumberWidget::FormatDamage(float Damage)
{
	return FString::FromInt(FMath::Max(1, FMath::RoundToInt(Damage)));
}

TSharedRef<SWidget> UMDDamageNumberWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DamageText"));
		WidgetTree->RootWidget = Text;
	}
	return Super::RebuildWidget();
}

void UMDDamageNumberWidget::Start(APlayerController* Controller, const FVector& WorldLocation, float Damage, bool bCrit)
{
	if (!Controller || !Text)
	{
		return;
	}
	Owner = Controller;
	Location = WorldLocation;
	StartTime = Controller->GetWorld() ? Controller->GetWorld()->GetRealTimeSeconds() : 0.0;
	Drift = FVector2D(FMath::FRandRange(-25.0f, 25.0f), 0.0f);

	FSlateFontInfo Font = Text->GetFont();
	Font.Size = bCrit ? CritFontSize : NormalFontSize;
	Font.OutlineSettings.OutlineSize = bCrit ? 3 : 2;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.9f);
	Text->SetFont(Font);
	Text->SetText(FText::FromString(bCrit ? FormatDamage(Damage) + TEXT("!") : FormatDamage(Damage)));
	Text->SetColorAndOpacity(FSlateColor(bCrit ? CritColor : NormalColor));
	Text->SetRenderOpacity(1.0f);

	bActive = true;
	SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	if (!IsInViewport())
	{
		AddToViewport(50);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMDDamageNumberWidget::Stop()
{
	bActive = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMDDamageNumberWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bActive)
	{
		return;
	}
	APlayerController* Controller = Owner.Get();
	if (!Controller || !Controller->GetWorld())
	{
		Stop();
		return;
	}
	const float Age = static_cast<float>(Controller->GetWorld()->GetRealTimeSeconds() - StartTime);
	const float T = Age / FMath::Max(Lifetime, 0.1f);
	if (T >= 1.0f)
	{
		Stop();
		return;
	}
	FVector2D Screen;
	if (!Controller->ProjectWorldLocationToScreen(Location, Screen, true))
	{
		SetRenderOpacity(0.0f);
		return;
	}
	const float Eased = 1.0f - FMath::Square(1.0f - T);
	SetPositionInViewport(Screen + Drift * Eased + FVector2D(0.0f, -RisePixels * Eased), true);
	SetRenderOpacity(T < 0.6f ? 1.0f : FMath::Clamp((1.0f - T) / 0.4f, 0.0f, 1.0f));
}
