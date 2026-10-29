#include "LockOn/MDLockOnReticleWidget.h"

#include "Rendering/DrawElements.h"

namespace MDLockOnReticleLocal
{
	constexpr float HalfDiagonal = 15.0f; // pixels at scale 1
	constexpr float RingRadius = 24.0f;
	constexpr int32 RingSegments = 28;

	void Diamond(TArray<FVector2D>& Out, const FVector2D& C, float R)
	{
		Out = { C + FVector2D(0, -R), C + FVector2D(R, 0), C + FVector2D(0, R), C + FVector2D(-R, 0), C + FVector2D(0, -R) };
	}
}

void UMDLockOnReticleWidget::SetLocked(const FVector2D& TargetPixel, float PoiseFraction, bool bThreatening, float Scale)
{
	bLocked = true;
	SnapOffAlpha = 0.0f;
	Pixel = TargetPixel;
	Poise = PoiseFraction;
	bThreat = bThreatening;
	ReticleScale = FMath::Clamp(Scale, 0.5f, 2.0f);
}

void UMDLockOnReticleWidget::SetNeighbours(bool bInHasLeft, const FVector2D& InLeftPixel, bool bInHasRight, const FVector2D& InRightPixel)
{
	bHasLeft = bInHasLeft;
	bHasRight = bInHasRight;
	LeftPixel = InLeftPixel;
	RightPixel = InRightPixel;
}

void UMDLockOnReticleWidget::BeginSnapOff()
{
	if (bLocked)
	{
		bLocked = false;
		SnapOffAlpha = 1.0f;
		bHasLeft = false;
		bHasRight = false;
	}
}

void UMDLockOnReticleWidget::SetHidden()
{
	bLocked = false;
	SnapOffAlpha = 0.0f;
	bHasLeft = false;
	bHasRight = false;
}

void UMDLockOnReticleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bLocked && SnapOffAlpha > 0.0f)
	{
		SnapOffAlpha = FMath::Max(0.0f, SnapOffAlpha - InDeltaTime / SnapOffSeconds);
	}
}

int32 UMDLockOnReticleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!IsShowing())
	{
		return LayerId;
	}
	using namespace MDLockOnReticleLocal;

	// Slate paint is in the widget's local (DPI-scaled) space: convert from viewport pixels.
	const float DPI = FMath::Max(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale(), 0.01f);
	const FVector2D Centre = Pixel / DPI;
	const float Fade = bLocked ? 1.0f : SnapOffAlpha;
	// Snap-off: the diamond opens up and fades; locked: steady.
	const float Grow = bLocked ? 1.0f : 1.0f + (1.0f - SnapOffAlpha) * 0.9f;
	const float R = HalfDiagonal * ReticleScale * Grow;

	const FLinearColor Calm(0.88f, 0.86f, 0.80f, 0.95f * Fade); // muted off-white
	const FLinearColor Alert(0.92f, 0.20f, 0.16f, 0.98f * Fade);
	const FLinearColor Colour = (bLocked && bThreat) ? Alert : Calm;
	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.55f * Fade);

	TArray<FVector2D> Points;
	Diamond(Points, Centre, R);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Shadow, true, 5.0f);
	++LayerId;
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, 2.2f);
	++LayerId;

	// Poise ring: a thin arc, full = full poise.
	if (bLocked && Poise >= 0.0f)
	{
		const float Radius = RingRadius * ReticleScale;
		const int32 Segments = FMath::Clamp(FMath::CeilToInt(RingSegments * FMath::Clamp(Poise, 0.0f, 1.0f)), 0, RingSegments);
		if (Segments > 0)
		{
			Points.Reset();
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float A = -HALF_PI + (2.0f * PI * i) / RingSegments;
				Points.Add(Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, FLinearColor(Colour.R, Colour.G, Colour.B, 0.6f * Fade), true, 1.5f);
			++LayerId;
		}
	}

	// Candidate hint: a small tick over the two targets a cycle would pick.
	if (bLocked)
	{
		const FLinearColor Hint(Calm.R, Calm.G, Calm.B, 0.55f);
		for (const TPair<bool, FVector2D>& Neighbour : { TPair<bool, FVector2D>(bHasLeft, LeftPixel), TPair<bool, FVector2D>(bHasRight, RightPixel) })
		{
			if (Neighbour.Key)
			{
				const FVector2D C = Neighbour.Value / DPI;
				Points = { C + FVector2D(-5, -9), C + FVector2D(0, -4), C + FVector2D(5, -9) };
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Hint, true, 1.6f);
				++LayerId;
			}
		}
	}
	return LayerId;
}
