#include "MDDamageNumberWidget.h"

#include "MDDamageNumbers.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

FString UMDDamageNumberWidget::FormatDamage(float Damage)
{
	return FString::FromInt(FMath::Max(1, FMath::RoundToInt(Damage)));
}

void UMDDamageNumberWidget::EnsureText()
{
	if (Text || !WidgetTree)
	{
		return;
	}
	if (WidgetTree->RootWidget)
	{
		Text = Cast<UTextBlock>(WidgetTree->RootWidget);
		return;
	}
	Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DamageText"));
	WidgetTree->RootWidget = Text;
}

// The text block must exist before the first Start(): a widget made with CreateWidget has no Slate widget (and so no RebuildWidget)
// until it is added to the viewport, and Start() is what adds it. Building it lazily in RebuildWidget meant Start() always found no
// text and returned, so no native number was ever shown.
bool UMDDamageNumberWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	EnsureText();
	return bResult;
}

TSharedRef<SWidget> UMDDamageNumberWidget::RebuildWidget()
{
	EnsureText();
	return Super::RebuildWidget();
}

void UMDDamageNumberWidget::Start(APlayerController* Controller, const FVector& WorldLocation, float Damage, bool bCrit, const FGameplayTag& DamageType)
{
	EnsureText();
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
	ShownColor = MDDamageNumbers::ResolveColor(DamageType, bCrit, &TypeColorOverrides, bOverrideCritColor ? &CritColorOverride : nullptr);
	bShownCrit = bCrit;
	Text->SetColorAndOpacity(FSlateColor(ShownColor));
	Text->SetRenderOpacity(1.0f);
	SetRenderScale(bCrit ? FVector2D(CritPunchScale, CritPunchScale) : FVector2D(1.0, 1.0));

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
	if (bShownCrit)
	{
		const float Pop = FMath::Lerp(CritPunchScale, 1.0f, FMath::Clamp(Age / FMath::Max(CritPunchSeconds, 0.01f), 0.0f, 1.0f));
		SetRenderScale(FVector2D(Pop, Pop));
	}
	const float Eased = 1.0f - FMath::Square(1.0f - T);
	SetPositionInViewport(Screen + Drift * Eased + FVector2D(0.0f, -RisePixels * Eased), true);
	SetRenderOpacity(T < 0.6f ? 1.0f : FMath::Clamp((1.0f - T) / 0.4f, 0.0f, 1.0f));
}
