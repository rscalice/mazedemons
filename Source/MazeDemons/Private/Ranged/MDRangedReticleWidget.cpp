#include "Ranged/MDRangedReticleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Rendering/DrawElements.h"
#include "Ranged/MDRangedComponent.h"

bool UMDRangedReticleWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bResult;
}

float UMDRangedReticleWidget::SpreadToPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap)
{
	const float Fov = FMath::Clamp(VerticalFovDeg, 10.0f, 170.0f);
	const float PixelsPerDegree = ViewportHeight / Fov;
	return FMath::Max(MinGap, MinGap + SpreadDeg * PixelsPerDegree);
}

void UMDRangedReticleWidget::OnHitConfirmed(bool bKill, bool bHeadshot, FGameplayTag DamageType)
{
	const UWorld* World = GetWorld();
	HitMarkerStart = World ? World->GetRealTimeSeconds() : 0.0;
	bMarkerKill = bKill;
	bMarkerHead = bHeadshot;
}

int32 UMDRangedReticleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const UMDRangedComponent* RangedComp = Ranged.Get();
	if (!RangedComp || !RangedComp->IsRangedMode())
	{
		return Layer;
	}
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Center = Size * 0.5f;

	float Fov = 90.0f;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (PC->PlayerCameraManager)
		{
			const float HFov = PC->PlayerCameraManager->GetFOVAngle();
			// Horizontal -> vertical FOV for the viewport aspect.
			Fov = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HFov) * 0.5f) * (Size.Y / FMath::Max(Size.X, 1.0f))));
		}
	}
	const float Gap = SpreadToPixels(RangedComp->GetCurrentSpread(false), Fov, static_cast<float>(Size.Y));

	const FPaintGeometry Geo = AllottedGeometry.ToPaintGeometry();
	const FLinearColor Tint = Color * InWidgetStyle.GetColorAndOpacityTint();
	const FVector2D Dirs[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
	for (const FVector2D& D : Dirs)
	{
		TArray<FVector2D> Pts;
		Pts.Add(Center + D * Gap);
		Pts.Add(Center + D * (Gap + TickLength));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, Pts, ESlateDrawEffect::None, Tint, true, 2.0f);
	}

	const UWorld* World = GetWorld();
	const double Age = World ? World->GetRealTimeSeconds() - HitMarkerStart : 1000.0;
	if (Age >= 0.0 && Age < HitMarkerSeconds)
	{
		const float Alpha = 1.0f - static_cast<float>(Age / HitMarkerSeconds);
		FLinearColor Marker = bMarkerKill ? FLinearColor(1.0f, 0.1f, 0.08f, Alpha) : (bMarkerHead ? FLinearColor(1.0f, 0.85f, 0.2f, Alpha) : FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
		const float Inner = 7.0f;
		const float Outer = 15.0f;
		const FVector2D Diagonals[4] = { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) };
		for (const FVector2D& D : Diagonals)
		{
			TArray<FVector2D> Pts;
			Pts.Add(Center + D * Inner);
			Pts.Add(Center + D * Outer);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Geo, Pts, ESlateDrawEffect::None, Marker, true, 2.5f);
		}
		++Layer;
	}
	return Layer + 1;
}
