#include "MDMapPlan.h"

#include "Async/ParallelFor.h"
#include "MDQuestTypes.h"

FBox2D FMDMapPlanRules::FitBounds(const TArray<FBox2D>& Boxes, float Padding)
{
	FBox2D Result(ForceInit);
	for (const FBox2D& Box : Boxes)
	{
		if (Box.bIsValid && FMath::IsFinite(Box.Min.X) && FMath::IsFinite(Box.Min.Y) && FMath::IsFinite(Box.Max.X) && FMath::IsFinite(Box.Max.Y))
		{
			Result += Box;
		}
	}
	if (!Result.bIsValid)
	{
		return Result;
	}
	const double Pad = FMath::IsFinite(Padding) ? FMath::Max(static_cast<double>(Padding), 0.0) : 0.0;
	Result.Min -= FVector2D(Pad, Pad);
	Result.Max += FVector2D(Pad, Pad);
	return Result;
}

FBox2D FMDMapPlanRules::MakeSquare(const FBox2D& Bounds)
{
	if (!Bounds.bIsValid)
	{
		return Bounds;
	}
	const FVector2D Centre = (Bounds.Min + Bounds.Max) * 0.5;
	const FVector2D Size = Bounds.Max - Bounds.Min;
	const double Half = FMath::Max(FMath::Max(FMath::Abs(Size.X), FMath::Abs(Size.Y)), 1.0) * 0.5;
	return FBox2D(Centre - FVector2D(Half, Half), Centre + FVector2D(Half, Half));
}

float FMDMapPlanRules::EstimateFloorHeight(const TArray<float>& Heights, float MinValidHeight, float BucketSize)
{
	const float Bucket = FMath::Max(BucketSize, 1.0f);
	TMap<int32, int32> Counts;
	TMap<int32, double> Sums;
	for (const float Height : Heights)
	{
		if (!FMath::IsFinite(Height) || Height < MinValidHeight)
		{
			continue;
		}
		const int32 Key = FMath::FloorToInt(Height / Bucket);
		Counts.FindOrAdd(Key)++;
		Sums.FindOrAdd(Key) += Height;
	}
	int32 BestKey = 0;
	int32 BestCount = 0;
	for (const TPair<int32, int32>& Pair : Counts)
	{
		// Ties go to the lower bucket so the result does not depend on map order.
		if (Pair.Value > BestCount || (Pair.Value == BestCount && Pair.Key < BestKey))
		{
			BestKey = Pair.Key;
			BestCount = Pair.Value;
		}
	}
	if (BestCount == 0)
	{
		return MinValidHeight;
	}
	return static_cast<float>(Sums[BestKey] / BestCount);
}

EMDMapCell FMDMapPlanRules::Classify(float Height, float FloorHeight, float MinValidHeight, const FMDMapPlanStyle& Style)
{
	if (!FMath::IsFinite(Height) || Height < MinValidHeight)
	{
		return EMDMapCell::Void;
	}
	const float Delta = Height - FloorHeight;
	const float Tolerance = FMath::Max(Style.FloorTolerance, 1.0f);
	if (Delta < -Tolerance)
	{
		return EMDMapCell::Low;
	}
	if (Delta <= Tolerance)
	{
		return EMDMapCell::Floor;
	}
	return Delta <= FMath::Max(Style.StepHeight, Tolerance) ? EMDMapCell::Obstacle : EMDMapCell::Solid;
}

bool FMDMapPlanRules::BuildPlan(const TArray<float>& Heights, int32 Size, float FloorHeight, float MinValidHeight,
	const FMDMapPlanStyle& Style, TArray<FColor>& Out, TArray<int32>* OutCounts)
{
	Out.Reset();
	if (Size <= 0 || Heights.Num() != Size * Size)
	{
		return false;
	}
	const int32 Num = Size * Size;
	TArray<uint8> Cells;
	Cells.SetNumUninitialized(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		Cells[Index] = static_cast<uint8>(Classify(Heights[Index], FloorHeight, MinValidHeight, Style));
	}

	// Distance-to-not-floor within EdgeWidth via a separable max filter of the "not floor" mask.
	const int32 Radius = FMath::Clamp(Style.EdgeWidth, 0, 8);
	TArray<uint8> NearWall;
	if (Radius > 0)
	{
		TArray<uint8> Horizontal;
		Horizontal.SetNumZeroed(Num);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 X0 = FMath::Max(X - Radius, 0);
				const int32 X1 = FMath::Min(X + Radius, Size - 1);
				for (int32 Sx = X0; Sx <= X1; ++Sx)
				{
					if (Cells[Y * Size + Sx] != static_cast<uint8>(EMDMapCell::Floor))
					{
						Horizontal[Y * Size + X] = 1;
						break;
					}
				}
			}
		}
		NearWall.SetNumZeroed(Num);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			const int32 Y0 = FMath::Max(Y - Radius, 0);
			const int32 Y1 = FMath::Min(Y + Radius, Size - 1);
			for (int32 X = 0; X < Size; ++X)
			{
				for (int32 Sy = Y0; Sy <= Y1; ++Sy)
				{
					if (Horizontal[Sy * Size + X])
					{
						NearWall[Y * Size + X] = 1;
						break;
					}
				}
			}
		}
	}

	if (OutCounts)
	{
		OutCounts->Init(0, 6);
	}
	Out.SetNumUninitialized(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const EMDMapCell Cell = static_cast<EMDMapCell>(Cells[Index]);
		FColor Colour;
		switch (Cell)
		{
		case EMDMapCell::Floor:    Colour = Style.FloorColor; break;
		case EMDMapCell::Low:      Colour = Style.LowColor; break;
		case EMDMapCell::Obstacle: Colour = Style.ObstacleColor; break;
		case EMDMapCell::Solid:    Colour = Style.SolidColor; break;
		default:                   Colour = Style.VoidColor; break;
		}
		const bool bEdge = Cell == EMDMapCell::Floor && NearWall.Num() == Num && NearWall[Index];
		if (bEdge)
		{
			Colour = Style.EdgeColor;
		}
		Colour.A = 255;
		Out[Index] = Colour;
		if (OutCounts)
		{
			(*OutCounts)[static_cast<int32>(Cell)]++;
			if (bEdge)
			{
				(*OutCounts)[5]++;
			}
		}
	}
	return true;
}

namespace
{
	// Fractional cell coordinate of map UV along one axis (cell centres at integer values).
	double FogCellCoord(double UV, double BoundsSize, float CellSize)
	{
		return FMath::Clamp(UV, 0.0, 1.0) * BoundsSize / FMath::Max(static_cast<double>(CellSize), 1.0) - 0.5;
	}

	struct FAxisSample
	{
		int32 I0 = 0;
		int32 I1 = 0;
		float T = 0.0f;
	};

	FAxisSample MakeAxisSample(double Coord, int32 Cells)
	{
		FAxisSample Sample;
		const double Clamped = FMath::Clamp(Coord, 0.0, static_cast<double>(Cells - 1));
		Sample.I0 = FMath::Clamp(FMath::FloorToInt(Clamped), 0, Cells - 1);
		Sample.I1 = FMath::Min(Sample.I0 + 1, Cells - 1);
		Sample.T = static_cast<float>(Clamped - Sample.I0);
		return Sample;
	}

	float Bilinear(const FMDFogGrid& Grid, const FAxisSample& X, const FAxisSample& Y)
	{
		const float A = Grid.IsRevealedAt(X.I0, Y.I0) ? 1.0f : 0.0f;
		const float B = Grid.IsRevealedAt(X.I1, Y.I0) ? 1.0f : 0.0f;
		const float C = Grid.IsRevealedAt(X.I0, Y.I1) ? 1.0f : 0.0f;
		const float D = Grid.IsRevealedAt(X.I1, Y.I1) ? 1.0f : 0.0f;
		return FMath::Lerp(FMath::Lerp(A, B, X.T), FMath::Lerp(C, D, X.T), Y.T);
	}
}

float FMDMapPlanRules::SampleFog(const FMDFogGrid& Grid, double U, double V)
{
	if (!Grid.IsValid())
	{
		return 0.0f;
	}
	const FVector2D BoundsSize = Grid.Bounds.Max - Grid.Bounds.Min;
	const FAxisSample X = MakeAxisSample(FogCellCoord(U, BoundsSize.X, Grid.CellSize), Grid.Width);
	const FAxisSample Y = MakeAxisSample(FogCellCoord(V, BoundsSize.Y, Grid.CellSize), Grid.Height);
	return Bilinear(Grid, X, Y);
}

bool FMDMapPlanRules::ComposeFog(const TArray<FColor>& Plan, int32 Size, const FMDFogGrid& Grid, FColor FogColor, TArray<FColor>& Out)
{
	if (Size <= 0 || Plan.Num() != Size * Size)
	{
		return false;
	}
	Out.SetNumUninitialized(Plan.Num());
	if (!Grid.IsValid())
	{
		for (FColor& Pixel : Out)
		{
			Pixel = FColor(FogColor.R, FogColor.G, FogColor.B, 255);
		}
		return true;
	}

	// Per-column and per-row samples are shared by every pixel of that column / row.
	const FVector2D BoundsSize = Grid.Bounds.Max - Grid.Bounds.Min;
	TArray<FAxisSample> Columns;
	TArray<FAxisSample> Rows;
	Columns.SetNum(Size);
	Rows.SetNum(Size);
	for (int32 I = 0; I < Size; ++I)
	{
		const double UV = (I + 0.5) / Size;
		Columns[I] = MakeAxisSample(FogCellCoord(UV, BoundsSize.X, Grid.CellSize), Grid.Width);
		Rows[I] = MakeAxisSample(FogCellCoord(UV, BoundsSize.Y, Grid.CellSize), Grid.Height);
	}

	ParallelFor(Size, [&](int32 Y)
	{
		const FAxisSample& Row = Rows[Y];
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 Index = Y * Size + X;
			const float Revealed = Bilinear(Grid, Columns[X], Row);
			const FColor& Source = Plan[Index];
			if (Revealed >= 1.0f)
			{
				Out[Index] = FColor(Source.R, Source.G, Source.B, 255);
			}
			else
			{
				Out[Index] = FColor(
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(FogColor.R), static_cast<float>(Source.R), Revealed))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(FogColor.G), static_cast<float>(Source.G), Revealed))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(FogColor.B), static_cast<float>(Source.B), Revealed))),
					255);
			}
		}
	});
	return true;
}
