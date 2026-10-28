#pragma once

#include "CoreMinimal.h"

class AActor;
class UEnvQuery;
class UWorld;
struct FMDFogGrid;

// V1 WP3 spatial queries (v1-design 2.6): cheap, testable, no per-tick EQS. The point generation and picking are plain
// functions over an FMDQueryEnvironment (navmesh projection, line of sight and the room-reveal gate as callbacks), so they run in
// MazeDemons.AI.Queries.* with a mock environment and no level. The *For wrappers build the environment from the world
// (navmesh, visibility traces, the shared fog of war) and are what the WP5 behaviours call.
//
//   ring slots    the melee ring around a target (reuses WP2's MDPackRules ring)
//   flank points  navmesh points in the angle band behind the target (>= 120 degrees off its facing); points the target
//                 cannot see (a wall between) win over open ground
//   kite points   PreferredRange +- 1.5 m from the target, line of sight to it, >= 3 m from any player in melee
//   rally points  AMDRallyPoint actors, else a room entry (actor tagged "MDRoomEntry")
//   ambush        ONE EQS query, run once when an ambusher spawns (UMDAIAmbushQuery), never per tick
// Every query rejects positions in rooms the players have not revealed (the shared fog of war, U4): IsRevealed.
// Distances in cm, angles in degrees.

struct MAZEDEMONS_API FMDQueryEnvironment
{
	// Navmesh projection: true and Out = the nearest navmesh point, false when there is none. Null = no navmesh (identity).
	TFunction<bool(const FVector& In, FVector& Out)> ProjectToNav;

	// True when nothing blocks the view from From to To. Null = open ground.
	TFunction<bool(const FVector& From, const FVector& To)> HasLineOfSight;

	// True when the room / zone at Point has been revealed by the players. Null = everything is revealed (no fog).
	TFunction<bool(const FVector& Point)> IsRevealed;

	bool Project(const FVector& In, FVector& Out) const { Out = In; return ProjectToNav ? ProjectToNav(In, Out) : true; }
	bool LineOfSight(const FVector& From, const FVector& To) const { return HasLineOfSight ? HasLineOfSight(From, To) : true; }
	bool Revealed(const FVector& Point) const { return IsRevealed ? IsRevealed(Point) : true; }
};

struct MAZEDEMONS_API FMDFlankParams
{
	FVector TargetLocation = FVector::ZeroVector;
	// The direction the target faces (any length; only XY counts).
	FVector TargetFacing = FVector::ForwardVector;
	// The enemy's own position: closer candidates win ties.
	FVector FromLocation = FVector::ZeroVector;
	float Radius = 350.0f;
	// The band behind the target, measured from its facing: 120..180 degrees, on both sides.
	float MinAngle = 120.0f;
	float MaxAngle = 180.0f;
	int32 SamplesPerSide = 5;
};

struct MAZEDEMONS_API FMDKiteParams
{
	FVector TargetLocation = FVector::ZeroVector;
	FVector FromLocation = FVector::ZeroVector;
	float PreferredRange = 900.0f;
	// PreferredRange +- Tolerance (1.5 m).
	float Tolerance = 150.0f;
	int32 NumAngles = 16;
	// Players currently in melee: a kite point keeps MinMeleeDistance (3 m) from each.
	TArray<FVector> MeleePlayers;
	float MinMeleeDistance = 300.0f;
};

namespace MDAIQueries
{
	// Eye height used by the line-of-sight checks.
	constexpr float EyeHeight = 60.0f;

	// ---- Room reveal gate ----
	// True when the fog cell at Point is revealed. A grid with no layout, and points outside its bounds, count as revealed:
	// the gate only ever blocks rooms the fog knows are hidden.
	MAZEDEMONS_API bool IsPointRevealed(const FMDFogGrid& Grid, const FVector& Point);

	// ---- Ring slots ----
	// The 6 melee ring points around Target (MDPackRules::SlotLocation), in slot order.
	MAZEDEMONS_API void RingSlots(const FVector& TargetLocation, float BaseYawDeg, float Radius, TArray<FVector>& Out);

	// The ring points that are navmesh-valid and in a revealed room (Out keeps slot order; rejected slots are skipped).
	MAZEDEMONS_API void ValidRingSlots(const FVector& TargetLocation, float BaseYawDeg, float Radius, const FMDQueryEnvironment& Env, TArray<FVector>& Out);

	// ---- Flank ----
	// Angle (0..180) between the target's facing and the direction from the target to Point. 180 = straight behind.
	MAZEDEMONS_API float AngleFromFacing(const FVector& TargetLocation, const FVector& TargetFacing, const FVector& Point);

	// Raw candidate points on the ring of Radius, in the band, on both sides.
	MAZEDEMONS_API void FlankCandidates(const FMDFlankParams& Params, TArray<FVector>& Out);

	// Best flank point: navmesh-projected, revealed, still in the band after projection. A point the target cannot see beats
	// open ground; then the closer to FromLocation and the further behind win. False when nothing qualifies.
	MAZEDEMONS_API bool FindFlankPoint(const FMDFlankParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint);

	// ---- Kite ----
	MAZEDEMONS_API void KiteCandidates(const FMDKiteParams& Params, TArray<FVector>& Out);

	// Best kite point: navmesh-projected, revealed, within PreferredRange +- Tolerance of the target, line of sight to it, at
	// least MinMeleeDistance from every melee player. The closest to FromLocation wins (least running). False when none.
	MAZEDEMONS_API bool FindKitePoint(const FMDKiteParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint);

	// ---- Rally ----
	// The nearest revealed rally point to Near; with none, the nearest revealed room entry. False when neither exists.
	MAZEDEMONS_API bool FindRallyPoint(const TArray<FVector>& RallyPoints, const TArray<FVector>& RoomEntries, const FVector& Near, const FMDQueryEnvironment& Env, FVector& OutPoint);

	// ---- World wrappers (the WP5 behaviours call these) ----
	// Navmesh + visibility traces (ignoring the given actors) + the shared fog gate (UMDAISubsystem::IsRoomRevealed).
	MAZEDEMONS_API FMDQueryEnvironment MakeWorldEnvironment(UWorld* World, const AActor* IgnoreA = nullptr, const AActor* IgnoreB = nullptr);

	MAZEDEMONS_API bool FindFlankPointFor(const AActor* Enemy, const AActor* Target, float Radius, FVector& OutPoint);
	MAZEDEMONS_API bool FindKitePointFor(const AActor* Enemy, const AActor* Target, float PreferredRange, FVector& OutPoint);
	MAZEDEMONS_API bool FindRallyPointFor(UWorld* World, const FVector& Near, FVector& OutPoint);
}

// The ambush position query (v1-design 2.6: "EQS only for ambush pick positions at spawn time, one-off"). The template is built
// in code (no asset): a grid around the ambusher's spawn point, projected to the navmesh, keeping the points no player can
// see (trace) that are at least MinPlayerDistance from every player. Run it once when the ambusher spawns.
class MAZEDEMONS_API UMDAIAmbushQuery
{
public:
	static constexpr float GridHalfSize = 1200.0f;
	static constexpr float GridSpacing = 200.0f;
	static constexpr float MinPlayerDistance = 800.0f;

	// The query template (transient, built on first use and kept).
	static UEnvQuery* GetTemplate();

	// Runs the query now (instant) with Querier at the centre. True and OutPositions = the matching points, best first.
	static bool Run(AActor* Querier, TArray<FVector>& OutPositions);
};
