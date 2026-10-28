#include "AI/MDBehaviourRules.h"

#include "AI/MDPackRules.h"

namespace MDBehaviourLocal
{
	FVector Flat(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0);
	}

	FVector FlatDir(const FVector& From, const FVector& To)
	{
		FVector D = Flat(To - From);
		return D.GetSafeNormal();
	}
}

bool MDBehaviour::CanHoldAttackToken(EMDRole Role)
{
	return Role != EMDRole::Bait && Role != EMDRole::Guard;
}

bool MDBehaviour::BarkAllowed(double Now, double LastBark, double Interval)
{
	return LastBark < 0.0 || Now - LastBark >= Interval;
}

bool MDBehaviour::ShouldSwarm(EMDIntelligenceTier Tier, int32 PackAlive, bool bHasSlot, float DistanceToTarget, float ThreatRange)
{
	return static_cast<int32>(Tier) >= static_cast<int32>(EMDIntelligenceTier::T1) && PackAlive >= SwarmMinPack && bHasSlot
		&& DistanceToTarget > ThreatRange + 100.0f;
}

void MDBehaviour::EvenSlotSet(int32 NumAttackers, int32 Rotation, TArray<int32>& OutSlots)
{
	OutSlots.Reset();
	const int32 Ring = MDPack::NumRingSlots;
	const int32 N = FMath::Clamp(NumAttackers, 0, Ring);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Base = FMath::RoundToInt(static_cast<float>(i) * static_cast<float>(Ring) / static_cast<float>(N));
		OutSlots.AddUnique(((Base + Rotation) % Ring + Ring) % Ring);
	}
}

bool MDBehaviour::IsWithinBaitLeash(const FVector& BaitLocation, const FVector& PackCentre)
{
	return FVector::Dist2D(BaitLocation, PackCentre) <= BaitLeash + KINDA_SMALL_NUMBER;
}

bool MDBehaviour::ComputeBaitStep(const FVector& BaitLocation, const FVector& PlayerLocation, const FVector& PackCentre, FVector& OutPoint)
{
	using namespace MDBehaviourLocal;
	OutPoint = BaitLocation;
	const bool bOutside = !IsWithinBaitLeash(BaitLocation, PackCentre);
	if (!bOutside && FVector::Dist2D(BaitLocation, PlayerLocation) > BaitApproachDistance)
	{
		return false; // nothing to back away from
	}
	FVector Desired = BaitLocation;
	if (FVector::Dist2D(BaitLocation, PlayerLocation) <= BaitApproachDistance)
	{
		Desired = BaitLocation - FlatDir(BaitLocation, PlayerLocation) * BaitHalfStep;
	}
	// Pull the step back inside the leash (a bait that already strayed walks back toward the pack).
	const FVector Offset = Flat(Desired - PackCentre);
	if (Offset.Size() > BaitLeash * 0.95f)
	{
		const FVector Clamped = PackCentre + Offset.GetSafeNormal() * (BaitLeash * 0.95f);
		Desired = FVector(Clamped.X, Clamped.Y, BaitLocation.Z);
	}
	if (FVector::Dist2D(Desired, BaitLocation) < 10.0f)
	{
		return false; // cornered by the leash: stay put
	}
	OutPoint = Desired;
	return true;
}

bool MDBehaviour::CanBaitAcross(const FMDQueryEnvironment& Env, const FVector& BaitLocation, const FVector& PlayerLocation)
{
	return Env.Revealed(BaitLocation) && Env.Revealed(PlayerLocation);
}

bool MDBehaviour::FakeFlinchUnlocked(EMDIntelligenceTier Tier)
{
	return static_cast<int32>(Tier) >= static_cast<int32>(EMDIntelligenceTier::T3);
}

float MDBehaviour::FeintChance(EMDIntelligenceTier Tier)
{
	switch (Tier)
	{
	case EMDIntelligenceTier::T3: return 0.45f;
	case EMDIntelligenceTier::T4: return 0.6f;
	default: return 0.0f; // never on T0 - T2
	}
}

float MDBehaviour::FeintDelay(float Roll01)
{
	return FMath::Lerp(FeintMinDelay, FeintMaxDelay, FMath::Clamp(Roll01, 0.0f, 1.0f));
}

bool MDBehaviour::FMDFeintOpportunity::ShouldFeintNow(EMDIntelligenceTier Tier, bool bHasToken, float Roll01)
{
	if (!bHasToken || FeintChance(Tier) <= 0.0f || bUsed)
	{
		return false;
	}
	if (!bEvaluated)
	{
		bEvaluated = true;
		bWillFeint = Roll01 < FeintChance(Tier);
	}
	return bWillFeint;
}

bool MDBehaviour::BlinkOffCooldown(double Now, double LastBlinkTime)
{
	return Now - LastBlinkTime >= BlinkCooldown;
}

bool MDBehaviour::IsBlinkArrivalValid(const FMDBlinkParams& Params, const FMDQueryEnvironment& Env, const FVector& Candidate, FVector& OutProjected)
{
	if (!Env.Project(Candidate, OutProjected))
	{
		return false; // not on the navmesh
	}
	if (FVector::Dist2D(OutProjected, Params.TargetLocation) < Params.MinTargetDistance)
	{
		return false; // too close to the target
	}
	return Env.Revealed(OutProjected);
}

bool MDBehaviour::FindBlinkArrival(const FMDBlinkParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint)
{
	FMDFlankParams Flank;
	Flank.TargetLocation = Params.TargetLocation;
	Flank.TargetFacing = Params.TargetFacing;
	Flank.FromLocation = Params.FromLocation;
	Flank.Radius = Params.Radius;
	Flank.MinAngle = 90.0f; // behind and to the sides
	Flank.MaxAngle = 180.0f;
	TArray<FVector> Candidates;
	MDAIQueries::FlankCandidates(Flank, Candidates);

	bool bFound = false;
	float BestAngle = -1.0f;
	for (const FVector& Candidate : Candidates)
	{
		FVector Projected;
		if (!IsBlinkArrivalValid(Params, Env, Candidate, Projected))
		{
			continue;
		}
		const float Angle = MDAIQueries::AngleFromFacing(Params.TargetLocation, Params.TargetFacing, Projected);
		if (Angle > BestAngle)
		{
			BestAngle = Angle;
			OutPoint = Projected;
			bFound = true;
		}
	}
	return bFound;
}

MDBehaviour::FMDAmbushTimeline MDBehaviour::MakeAmbushTimeline(double TriggerTime)
{
	FMDAmbushTimeline T;
	T.TriggerTime = TriggerTime;
	T.CueTime = TriggerTime;
	T.ArriveTime = TriggerTime + AmbushCueLead;
	T.GraceEnd = T.ArriveTime + AmbushArrivalGrace;
	return T;
}

bool MDBehaviour::IsInArrivalGrace(double Now, double GraceEnd)
{
	return Now < GraceEnd;
}

bool MDBehaviour::IsNearSafeSpot(const FVector& Location, const TArray<FVector>& SafeSpots, float Clearance)
{
	for (const FVector& Spot : SafeSpots)
	{
		if (FVector::Dist2D(Location, Spot) < Clearance)
		{
			return true;
		}
	}
	return false;
}

bool MDBehaviour::CanAmbush(EMDIntelligenceTier Tier, bool bIgnoreTier, bool bNearSafeSpot)
{
	if (bNearSafeSpot)
	{
		return false;
	}
	return bIgnoreTier || static_cast<int32>(Tier) >= static_cast<int32>(EMDIntelligenceTier::T2);
}

bool MDBehaviour::KiteRepositionReady(double Now, double LastReposition)
{
	return Now - LastReposition >= KiteRepositionCooldown;
}

MDBehaviour::EKiteAction MDBehaviour::DecideKite(float Distance, float PreferredRange, bool bLineOfSight, bool bRepositionReady, bool bCanFire)
{
	const bool bTooClose = Distance < PreferredRange * KiteMinRangeFraction;
	const bool bTooFar = Distance > PreferredRange + KiteMaxRangeSlack;
	if ((bTooClose || bTooFar || !bLineOfSight) && bRepositionReady)
	{
		return EKiteAction::Reposition;
	}
	// Caught inside the cooldown: it can still shoot (the reposition cooldown is what makes a kiter catchable).
	if (bLineOfSight && !bTooFar && bCanFire)
	{
		return EKiteAction::Fire;
	}
	return EKiteAction::Hold;
}

float MDBehaviour::KiteDrawTimeFor(const FMDTierParams& Tier)
{
	return Tier.ResolveTellTime(KiteDrawTime);
}

FVector MDBehaviour::ChargeLineUpPoint(const FVector& TargetLocation, const FVector& FromLocation)
{
	const FVector Away = MDBehaviourLocal::FlatDir(TargetLocation, FromLocation);
	const FVector Dir = Away.IsNearlyZero() ? FVector::BackwardVector : Away;
	FVector P = TargetLocation + Dir * ChargeLineUpDistance;
	P.Z = FromLocation.Z;
	return P;
}

float MDBehaviour::ChargeLateralOffset(const FVector& Origin, const FVector& Dir, const FVector& Point)
{
	const FVector D = MDBehaviourLocal::Flat(Dir).GetSafeNormal();
	const FVector Rel = MDBehaviourLocal::Flat(Point - Origin);
	return static_cast<float>(FMath::Abs(Rel.X * D.Y - Rel.Y * D.X));
}

bool MDBehaviour::IsChargeCancelledBySidestep(const FVector& Origin, const FVector& Dir, const FVector& TargetNow)
{
	return ChargeLateralOffset(Origin, Dir, TargetNow) > ChargeSidestepLateral;
}

bool MDBehaviour::IsChargeDistanceOk(float Distance)
{
	return Distance >= ChargeMinDistance && Distance <= ChargeMaxDistance;
}

void MDBehaviour::FMDGreedTracker::RegisterAbsorbedHit(double Now)
{
	if (Now - LastHitTime > GreedWindow)
	{
		Hits = 0;
	}
	++Hits;
	LastHitTime = Now;
}

bool MDBehaviour::FMDGreedTracker::ShouldBrace(EMDIntelligenceTier Tier, int32 Threshold) const
{
	return static_cast<int32>(Tier) >= static_cast<int32>(EMDIntelligenceTier::T3) && Threshold > 0 && Hits >= Threshold;
}

void MDBehaviour::ComputeGuardLine(const FVector& CasterLocation, const FVector& PlayerLocation, int32 NumGuards, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (NumGuards <= 0)
	{
		return;
	}
	const FVector Flat = MDBehaviourLocal::Flat(PlayerLocation - CasterLocation);
	const float Dist = static_cast<float>(Flat.Size());
	const FVector Dir = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat / Dist;
	const FVector Side(-Dir.Y, Dir.X, 0.0);
	const float Along = FMath::Min(FMath::Clamp(Dist * 0.35f, GuardMinDistance, GuardMaxDistance), Dist * 0.6f);
	const FVector Centre = CasterLocation + Dir * Along;
	for (int32 i = 0; i < NumGuards; ++i)
	{
		const float Offset = (static_cast<float>(i) - static_cast<float>(NumGuards - 1) * 0.5f) * GuardSpacing;
		OutPoints.Add(FVector(Centre.X, Centre.Y, CasterLocation.Z) + Side * Offset);
	}
}

bool MDBehaviour::ParseForcedIntent(const FString& Text, EMDIntent& OutIntent)
{
	FString T = Text.TrimStartAndEnd();
	if (T.IsEmpty() || T.Equals(TEXT("none"), ESearchCase::IgnoreCase) || T.Equals(TEXT("off"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	if (const UEnum* Enum = StaticEnum<EMDIntent>())
	{
		const int64 Value = Enum->GetValueByNameString(T, EGetByNameFlags::CaseSensitive);
		if (Value != INDEX_NONE)
		{
			OutIntent = static_cast<EMDIntent>(Value);
			return true;
		}
		// Case-insensitive pass (console users).
		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			if (Enum->GetNameStringByIndex(i).Equals(T, ESearchCase::IgnoreCase))
			{
				OutIntent = static_cast<EMDIntent>(Enum->GetValueByIndex(i));
				return true;
			}
		}
	}
	return false;
}
