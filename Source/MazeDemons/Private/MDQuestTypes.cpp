#include "MDQuestTypes.h"

#include "Internationalization/Text.h"

void FMDFogGrid::Init(const FBox2D& InBounds, float InCellSize)
{
	FVector2D Min = InBounds.Min;
	FVector2D Size = InBounds.Max - InBounds.Min;
	if (!FMath::IsFinite(Min.X) || !FMath::IsFinite(Min.Y))
	{
		Min = FVector2D::ZeroVector;
	}
	Size.X = FMath::IsFinite(Size.X) ? FMath::Max(Size.X, 1.0) : 1.0;
	Size.Y = FMath::IsFinite(Size.Y) ? FMath::Max(Size.Y, 1.0) : 1.0;

	Bounds = FBox2D(Min, Min + Size);
	CellSize = FMath::IsFinite(InCellSize) ? FMath::Max(InCellSize, 1.0f) : 400.0f;
	// Raise the cell size so the longer axis fits in MaxCellsPerAxis cells.
	CellSize = FMath::Max(CellSize, static_cast<float>(FMath::Max(Size.X, Size.Y)) / static_cast<float>(MaxCellsPerAxis));

	Width = FMath::Clamp(FMath::CeilToInt(Size.X / CellSize), 1, MaxCellsPerAxis);
	Height = FMath::Clamp(FMath::CeilToInt(Size.Y / CellSize), 1, MaxCellsPerAxis);
	Bits.Init(0, (Width * Height + 7) / 8);
}

int32 FMDFogGrid::WorldToCell(const FVector2D& Pos) const
{
	if (!IsValid() || Pos.X < Bounds.Min.X || Pos.Y < Bounds.Min.Y || Pos.X > Bounds.Max.X || Pos.Y > Bounds.Max.Y)
	{
		return -1;
	}
	const int32 X = FMath::Clamp(FMath::FloorToInt((Pos.X - Bounds.Min.X) / CellSize), 0, Width - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt((Pos.Y - Bounds.Min.Y) / CellSize), 0, Height - 1);
	return Y * Width + X;
}

bool FMDFogGrid::RevealRadius(const FVector2D& Pos, float Radius)
{
	if (!IsValid())
	{
		return false;
	}
	const float R = FMath::Max(Radius, 0.0f);
	bool bChanged = false;

	const int32 Home = WorldToCell(Pos);
	if (Home >= 0 && !IsRevealed(Home))
	{
		Bits[Home >> 3] |= static_cast<uint8>(1 << (Home & 7));
		bChanged = true;
	}

	const int32 MinX = FMath::Clamp(FMath::FloorToInt((Pos.X - R - Bounds.Min.X) / CellSize), 0, Width - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt((Pos.X + R - Bounds.Min.X) / CellSize), 0, Width - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt((Pos.Y - R - Bounds.Min.Y) / CellSize), 0, Height - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt((Pos.Y + R - Bounds.Min.Y) / CellSize), 0, Height - 1);
	const float RSq = R * R;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FVector2D Centre(Bounds.Min.X + (X + 0.5f) * CellSize, Bounds.Min.Y + (Y + 0.5f) * CellSize);
			if (FVector2D::DistSquared(Centre, Pos) > RSq)
			{
				continue;
			}
			const int32 Index = Y * Width + X;
			if (!IsRevealed(Index))
			{
				Bits[Index >> 3] |= static_cast<uint8>(1 << (Index & 7));
				bChanged = true;
			}
		}
	}
	return bChanged;
}

bool FMDFogGrid::RevealAll()
{
	bool bChanged = false;
	const int32 Num = GetNumCells();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		if (!IsRevealed(Index))
		{
			Bits[Index >> 3] |= static_cast<uint8>(1 << (Index & 7));
			bChanged = true;
		}
	}
	return bChanged;
}

bool FMDFogGrid::Clear()
{
	bool bChanged = false;
	for (uint8& Byte : Bits)
	{
		if (Byte != 0)
		{
			Byte = 0;
			bChanged = true;
		}
	}
	return bChanged;
}

bool FMDFogGrid::IsRevealed(int32 CellIndex) const
{
	if (CellIndex < 0 || CellIndex >= GetNumCells() || !Bits.IsValidIndex(CellIndex >> 3))
	{
		return false;
	}
	return (Bits[CellIndex >> 3] & (1 << (CellIndex & 7))) != 0;
}

bool FMDFogGrid::IsRevealedAt(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || X >= Width || Y >= Height)
	{
		return false;
	}
	return IsRevealed(Y * Width + X);
}

float FMDFogGrid::GetRevealedFraction() const
{
	const int32 Num = GetNumCells();
	if (Num <= 0)
	{
		return 0.0f;
	}
	int32 Count = 0;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		Count += IsRevealed(Index) ? 1 : 0;
	}
	return static_cast<float>(Count) / static_cast<float>(Num);
}

bool FMDFogGrid::MergeBits(const TArray<uint8>& Other)
{
	// Bits past the last cell in the final byte are never set by this class; mask them anyway so foreign data cannot leak in.
	const int32 NumCells = GetNumCells();
	const int32 Bytes = FMath::Min(Bits.Num(), Other.Num());
	bool bChanged = false;
	for (int32 i = 0; i < Bytes; ++i)
	{
		uint8 Incoming = Other[i];
		const int32 FirstCell = i * 8;
		if (FirstCell + 8 > NumCells)
		{
			const int32 Valid = FMath::Max(NumCells - FirstCell, 0);
			Incoming &= static_cast<uint8>((1 << Valid) - 1);
		}
		const uint8 Merged = Bits[i] | Incoming;
		if (Merged != Bits[i])
		{
			Bits[i] = Merged;
			bChanged = true;
		}
	}
	return bChanged;
}

bool UMDQuestRules::ApplyProgress(FMDQuestProgress& Progress, int32 Delta)
{
	if (Progress.State != EMDQuestState::Active || Delta <= 0)
	{
		return false;
	}
	const int32 Required = FMath::Max(Progress.Required, 1);
	const int32 NewCount = FMath::Clamp(Progress.Count + Delta, 0, Required);
	const bool bComplete = NewCount >= Required;
	if (NewCount == Progress.Count && !bComplete)
	{
		return false;
	}
	Progress.Count = NewCount;
	if (bComplete)
	{
		Progress.State = EMDQuestState::Completed;
	}
	return true;
}

int32 UMDQuestRules::CountZonesCompleted(const TArray<FMDQuestProgress>& Quests)
{
	int32 Count = 0;
	for (const FMDQuestProgress& Quest : Quests)
	{
		Count += Quest.State == EMDQuestState::Completed ? 1 : 0;
	}
	return FMath::Clamp(Count, 0, 4);
}

bool UMDQuestRules::UnlockNext(TArray<FMDQuestProgress>& Quests)
{
	for (FMDQuestProgress& Quest : Quests)
	{
		if (Quest.State == EMDQuestState::Completed)
		{
			continue;
		}
		if (Quest.State == EMDQuestState::Locked)
		{
			Quest.State = EMDQuestState::Active;
			return true;
		}
		return false; // an earlier quest is still Active
	}
	return false;
}

bool UMDQuestRules::MatchesObjective(const FMDObjectiveDef& Def, EMDObjectiveType EventType, const FGameplayTagContainer& EventTags)
{
	if (Def.Type != EventType)
	{
		return false;
	}
	if (!Def.TargetTag.IsValid())
	{
		return true;
	}
	return EventTags.HasTag(Def.TargetTag); // HasTag matches child tags of TargetTag
}

FText UMDQuestRules::FormatObjective(const FMDObjectiveDef& Def, const FMDQuestProgress& Progress)
{
	if (Def.Text.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("%d/%d"), Progress.Count, Progress.Required));
	}
	FFormatOrderedArguments Args;
	Args.Add(FText::AsNumber(Progress.Count));
	Args.Add(FText::AsNumber(Progress.Required));
	return FText::Format(Def.Text, Args);
}

bool UMDQuestRules::KillCountsForZone(int32 VictimZone, int32 QuestZone)
{
	return VictimZone > 0 && VictimZone == QuestZone;
}

uint8 UMDQuestRules::ToProHUDTaskState(EMDQuestState State)
{
	switch (State)
	{
	case EMDQuestState::Active:
		return 1;
	case EMDQuestState::Completed:
		return 2;
	default:
		return 0;
	}
}

FVector2D UMDQuestRules::WorldToMapUV(const FVector2D& World, const FBox2D& Bounds)
{
	const FVector2D Size = Bounds.Max - Bounds.Min;
	if (Size.X <= 0.0001 || Size.Y <= 0.0001)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(
		FMath::Clamp((World.X - Bounds.Min.X) / Size.X, 0.0, 1.0),
		FMath::Clamp((World.Y - Bounds.Min.Y) / Size.Y, 0.0, 1.0));
}
