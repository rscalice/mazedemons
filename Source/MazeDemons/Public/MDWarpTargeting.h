#pragma once

#include "CoreMinimal.h"

// Pure helpers for attack motion-warp target selection (no world access, unit tested: MazeDemons.Combat.WarpTarget).
// All distances / angles are planar (Z ignored): the warp notify ignores Z as well.
namespace MDWarpTargeting
{
	struct FCandidate
	{
		int32 Id = 0;
		FVector Location = FVector::ZeroVector;
	};

	inline FVector Planar(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0);
	}

	inline double PlanarDistance(const FVector& A, const FVector& B)
	{
		return Planar(B - A).Size();
	}

	// True when TargetLoc is within Range (planar) of Origin and inside the cone of HalfAngleDeg around Facing.
	// A target on top of the origin counts as in the cone; a zero facing disables the cone check.
	inline bool IsInRangeAndCone(const FVector& Origin, const FVector& Facing, const FVector& TargetLoc, float Range, float HalfAngleDeg)
	{
		const FVector To = Planar(TargetLoc - Origin);
		const double Dist = To.Size();
		if (Dist > Range)
		{
			return false;
		}
		if (Dist < 1.0)
		{
			return true;
		}
		const FVector F = Planar(Facing).GetSafeNormal();
		if (F.IsNearlyZero())
		{
			return true;
		}
		return FVector::DotProduct(F, To / Dist) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 0.0f, 180.0f)));
	}

	// Ids of the candidates that pass IsInRangeAndCone, nearest (planar) first; ties keep the lower Id first.
	inline TArray<int32> RankTargets(const FVector& Origin, const FVector& Facing, float Range, float HalfAngleDeg, const TArray<FCandidate>& Candidates)
	{
		TArray<TPair<double, int32>> Valid;
		for (const FCandidate& C : Candidates)
		{
			if (IsInRangeAndCone(Origin, Facing, C.Location, Range, HalfAngleDeg))
			{
				Valid.Emplace(PlanarDistance(Origin, C.Location), C.Id);
			}
		}
		Valid.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
		{
			return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value);
		});
		TArray<int32> Out;
		for (const TPair<double, int32>& P : Valid)
		{
			Out.Add(P.Value);
		}
		return Out;
	}

	// Where the warp ends: on the line to the target, (TargetRadius + StopDistance) short of the target centre. When the
	// attacker is already that close, the warp point is the attacker's own position (no forward motion), with the target's Z.
	inline FVector ComputeWarpPoint(const FVector& Origin, const FVector& TargetLoc, float TargetRadius, float StopDistance)
	{
		const FVector To = Planar(TargetLoc - Origin);
		const double Dist = To.Size();
		const double Keep = FMath::Max(TargetRadius, 0.0f) + FMath::Max(StopDistance, 0.0f);
		if (Dist <= Keep || Dist < KINDA_SMALL_NUMBER)
		{
			return FVector(Origin.X, Origin.Y, TargetLoc.Z);
		}
		const FVector Dir = To / Dist;
		return FVector(TargetLoc.X - Dir.X * Keep, TargetLoc.Y - Dir.Y * Keep, TargetLoc.Z);
	}

	// Yaw in degrees of a planar direction; 0 when the direction is degenerate.
	inline float YawDegrees(const FVector& Dir)
	{
		const FVector P = Planar(Dir);
		return P.IsNearlyZero() ? 0.0f : static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X)));
	}

	// Server check of a client-sent target: within Range + Tolerance (planar).
	inline bool IsTargetWithinReach(const FVector& Origin, const FVector& TargetLoc, float Range, float Tolerance)
	{
		return PlanarDistance(Origin, TargetLoc) <= Range + FMath::Max(Tolerance, 0.0f);
	}
}
