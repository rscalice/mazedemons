#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MDQuestTypes.generated.h"

UENUM(BlueprintType)
enum class EMDObjectiveType : uint8
{
	Reach = 0,
	Kill,
	Collect,
	Interact,
	Survive // banked: enum value and ReportEvent hook only, no timer in U4
};

UENUM(BlueprintType)
enum class EMDQuestState : uint8
{
	Locked = 0,
	Active,
	Completed
};

UENUM(BlueprintType)
enum class EMDMapMarkerType : uint8
{
	None = 0,
	Objective,
	Gate,
	Station,
	Checkpoint,
	Exit,
	Player
};

// Authoring data for one zone objective.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDObjectiveDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	EMDObjectiveType Type = EMDObjectiveType::Reach;

	// Tracker text; {0} is the count and {1} the required amount ("Slay ghouls {0}/{1}").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FText Text;

	// Matched by tag (a child tag of TargetTag matches too), never by level reference. Empty matches any event of Type.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTag TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (ClampMin = "1"))
	int32 Required = 1;

	// Survive objectives only (unused in U4).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (ClampMin = "0"))
	float SurviveSeconds = 0.0f;
};

// Replicated state of one zone's quest. ZoneIndex is 1-based (Zone 1..4).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDQuestProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 ZoneIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	EMDQuestState State = EMDQuestState::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 Required = 1;
};

// The level fog as replicated by UMDFogComponent: the grid layout plus one bit per cell (row-major, max 128x128 cells =
// 2 KB). Clients build their grid from this layout, so they never depend on their own copy of the level map actor.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDFogLevelBits
{
	GENERATED_BODY()

	UPROPERTY()
	FVector2D BoundsMin = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D BoundsMax = FVector2D::ZeroVector;

	UPROPERTY()
	float CellSize = 0.0f;

	UPROPERTY()
	TArray<uint8> Bits;

	bool HasLayout() const { return CellSize > 0.0f && BoundsMax.X > BoundsMin.X && BoundsMax.Y > BoundsMin.Y; }
};

// Plain fog-of-war grid: reveal-only bits over a 2D world box. World-free and pure, so it is unit tested.
struct MAZEDEMONS_API FMDFogGrid
{
	// 128 x 128 bits = 2 KB, replicated only when a bit changes. A 15k-unit level at 200 units per cell is 75 x 75.
	static constexpr int32 MaxCellsPerAxis = 128;

	// Any bounds are accepted: a zero, negative or non-finite size becomes 1 unit, and the cell size is raised so that
	// neither axis exceeds MaxCellsPerAxis cells.
	void Init(const FBox2D& InBounds, float InCellSize);

	// Index of the cell containing Pos (the max edge counts as the last cell), or -1 outside the bounds / uninitialised.
	int32 WorldToCell(const FVector2D& Pos) const;

	// Reveals every cell whose centre is within Radius of Pos (and the cell containing Pos). True when a bit changed.
	bool RevealRadius(const FVector2D& Pos, float Radius);

	// Reveals every cell. True when a bit changed.
	bool RevealAll();

	// Hides every cell (debug / reset only). True when a bit changed.
	bool Clear();

	bool IsRevealed(int32 CellIndex) const;
	bool IsRevealedAt(int32 X, int32 Y) const;

	// 0..1
	float GetRevealedFraction() const;

	// ORs Other into this grid; never hides a cell. Only the overlapping byte range is merged. True when a bit changed.
	bool MergeBits(const TArray<uint8>& Other);

	// Same cell count, cell size and bounds (to 1 unit), i.e. bits of one grid line up with the other.
	bool HasSameLayout(const FMDFogGrid& Other) const;

	bool IsValid() const { return Width > 0 && Height > 0; }
	int32 GetNumCells() const { return Width * Height; }

	FBox2D Bounds = FBox2D(FVector2D::ZeroVector, FVector2D::UnitVector);
	float CellSize = 400.0f;
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint8> Bits;
};

// Pure quest and map rules (world-free, unit tested). A plain static class, not a Blueprint library.
class MAZEDEMONS_API UMDQuestRules
{
public:
	// Adds Delta to an Active quest. Delta <= 0 and non-Active quests are ignored; Count is clamped to [0, Required] and the
	// quest becomes Completed at Required. True when anything changed.
	static bool ApplyProgress(FMDQuestProgress& Progress, int32 Delta);

	// Number of Completed quests (0..4).
	static int32 CountZonesCompleted(const TArray<FMDQuestProgress>& Quests);

	// The first Locked quest becomes Active when every quest before it is Completed. True when one was unlocked.
	static bool UnlockNext(TArray<FMDQuestProgress>& Quests);

	// Type must match and the event must carry TargetTag (or a child of it). An empty TargetTag matches any event of that type.
	static bool MatchesObjective(const FMDObjectiveDef& Def, EMDObjectiveType EventType, const FGameplayTagContainer& EventTags);

	// Def.Text with {0} = count and {1} = required; "3/5" when Def.Text is empty.
	static FText FormatObjective(const FMDObjectiveDef& Def, const FMDQuestProgress& Progress);

	// Kill credit: the victim must stand inside the active quest's zone. VictimZone <= 0 (no zone found) never counts.
	static bool KillCountsForZone(int32 VictimZone, int32 QuestZone);

	// Index of the matching ProHUD ETaskState entry: Locked 0, Active 1, Completed 2 (verify the enum order in WP2).
	static uint8 ToProHUDTaskState(EMDQuestState State);

	// World (X, Y) to 0..1 UV inside Bounds, clamped. U follows X and V follows Y (matches the zone capture orientation).
	// Zero-size bounds give (0, 0).
	static FVector2D WorldToMapUV(const FVector2D& World, const FBox2D& Bounds);
};
