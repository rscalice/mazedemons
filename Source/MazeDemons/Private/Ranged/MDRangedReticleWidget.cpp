#include "Ranged/MDRangedReticleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Overlay.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MDUIStatics.h"
#include "Rendering/DrawElements.h"
#include "Ranged/MDHUDOptions.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedUIRules.h"

bool UMDRangedReticleWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LockOnSlot"));
		WidgetTree->RootWidget = Root;
		LockOnSlot = Root;
	}
	else
	{
		LockOnSlot = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("LockOnSlot")));
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bResult;
}

float UMDRangedReticleWidget::SpreadToPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap)
{
	return FMDRangedUIRules::ReticleGapPixels(SpreadDeg, VerticalFovDeg, ViewportHeight, MinGap);
}

bool UMDRangedReticleWidget::IsMarkerActive() const
{
	const UWorld* World = GetWorld();
	const double Age = World ? World->GetRealTimeSeconds() - HitMarkerStart : 1000.0;
	return Age >= 0.0 && Age < HitMarkerSeconds;
}

void UMDRangedReticleWidget::OnHitConfirmed(bool bKill, bool bHeadshot, FGameplayTag DamageType)
{
	const UWorld* World = GetWorld();
	HitMarkerStart = World ? World->GetRealTimeSeconds() : 0.0;
	MarkerColour = FMDRangedUIRules::HitMarkerColour(bKill, DamageType);
	bMarkerHead = FMDRangedUIRules::ShowsHeadshotTick(bHeadshot);
}

float UMDRangedReticleWidget::ComputeTargetGap(const FVector2D& ViewportSize) const
{
	const UMDRangedComponent* RangedComp = Ranged.Get();
	if (!RangedComp)
	{
		return FMDRangedUIRules::ReticleMinGap;
	}
	float Fov = 90.0f;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (PC->PlayerCameraManager)
		{
			const float HFov = PC->PlayerCameraManager->GetFOVAngle();
			// Horizontal -> vertical FOV for the viewport aspect.
			Fov = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HFov) * 0.5f) * (ViewportSize.Y / FMath::Max(ViewportSize.X, 1.0f))));
		}
	}
	return FMDRangedUIRules::ReticleGapPixels(RangedComp->GetCurrentSpread(false), Fov, static_cast<float>(ViewportSize.Y));
}

void UMDRangedReticleWidget::UpdateGap(float DeltaSeconds, float TargetGapPixels)
{
	DisplayGap = DisplayGap < 0.0f ? TargetGapPixels : FMDRangedUIRules::AnimateGap(DisplayGap, TargetGapPixels, DeltaSeconds);
}

void UMDRangedReticleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateGap(InDeltaTime, ComputeTargetGap(MyGeometry.GetLocalSize()));
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
	const float Scale = UMDHUDOptions::GetReticleScale();
	const float Gap = DisplayGap >= 0.0f ? DisplayGap : ComputeTargetGap(Size);

	const FPaintGeometry Geo = AllottedGeometry.ToPaintGeometry();
	FLinearColor Base = UMDUIStatics::GetColor(EMDColor::TextPrimary);
	Base.A = 0.9f;
	const FLinearColor Tint = Base * InWidgetStyle.GetColorAndOpacityTint();
	const float Length = TickLength * Scale;
	const FVector2D Dirs[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
	for (const FVector2D& D : Dirs)
	{
		TArray<FVector2D> Pts;
		Pts.Add(Center + D * Gap);
		Pts.Add(Center + D * (Gap + Length));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geo, Pts, ESlateDrawEffect::None, Tint, false, 2.0f);
	}

	// R5: a beam weapon's heat (or charge) as a ring around the reticle, clockwise from the top. Orange -> red as it fills; while the weapon
	// is overheated the ring is the lock still to run and unwinds as it cools (FMDRangedUIRules::HeatRingSweep).
	if (RangedComp->IsBeamWeapon())
	{
		const bool bCharge = RangedComp->IsChargeStyleBeam();
		const float Value = bCharge ? RangedComp->GetCharge() : RangedComp->GetHeat();
		const float Max = bCharge ? RangedComp->GetMaxCharge() : RangedComp->GetMaxHeat();
		const EMDHeatMeterState HeatState = FMDRangedUIRules::HeatMeterState(true, bCharge, Value, Max, RangedComp->IsOverheated());
		const float Sweep = FMDRangedUIRules::HeatRingSweep(HeatState, FMDRangedUIRules::MeterFraction(Value, Max), RangedComp->GetOverheatLockFraction());
		const float Radius = Gap + Length + 14.0f * Scale;
		auto DrawArc = [&](float FromFraction, float ToFraction, const FLinearColor& Color, float Thickness, int32 LayerOffset)
		{
			const int32 Segments = FMath::Max(2, FMath::CeilToInt((ToFraction - FromFraction) * 72.0f));
			TArray<FVector2D> Pts;
			Pts.Reserve(Segments + 1);
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float Angle = -HALF_PI + TWO_PI * (FromFraction + (ToFraction - FromFraction) * static_cast<float>(i) / static_cast<float>(Segments));
				Pts.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + LayerOffset, Geo, Pts, ESlateDrawEffect::None, Color, false, Thickness);
		};
		FLinearColor Track = Base;
		Track.A = 0.18f;
		DrawArc(0.0f, 1.0f, Track * InWidgetStyle.GetColorAndOpacityTint(), 2.0f, 0);
		if (Sweep > 0.002f)
		{
			FLinearColor Fill = UMDUIStatics::GetColor(FMDRangedUIRules::HeatMeterToken(HeatState));
			Fill.A = 0.95f;
			DrawArc(0.0f, Sweep, Fill * InWidgetStyle.GetColorAndOpacityTint(), 3.5f, 1);
		}
		++Layer;
	}

	if (IsMarkerActive())
	{
		const UWorld* World = GetWorld();
		const double Age = World->GetRealTimeSeconds() - HitMarkerStart;
		FLinearColor Marker = MarkerColour;
		Marker.A = 1.0f - static_cast<float>(Age / HitMarkerSeconds);
		const float Inner = 7.0f * Scale;
		const float Outer = 15.0f * Scale;
		const FVector2D Diagonals[4] = { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) };
		for (const FVector2D& D : Diagonals)
		{
			TArray<FVector2D> Pts;
			Pts.Add(Center + D * Inner);
			Pts.Add(Center + D * Outer);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Geo, Pts, ESlateDrawEffect::None, Marker, false, 2.5f);
		}
		if (bMarkerHead)
		{
			// The headshot tick: a short vertical stroke above the X.
			TArray<FVector2D> Pts;
			Pts.Add(Center + FVector2D(0.0f, -(Outer + 4.0f * Scale)));
			Pts.Add(Center + FVector2D(0.0f, -(Outer + 14.0f * Scale)));
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Geo, Pts, ESlateDrawEffect::None, Marker, false, 3.0f);
		}
		++Layer;
	}
	return Layer + 1;
}
