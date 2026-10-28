#include "AI/MDPerceptionRules.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Perception/AISense_Hearing.h"

// ---- Noise ----

MDNoise::FProfile MDNoise::ProfileFor(FName Tag)
{
	FProfile P;
	if (Tag == FName(Gunshot))
	{
		P.Loudness = 2.5f;
		P.MaxRange = 5000.0f;
	}
	else if (Tag == FName(Sprint))
	{
		P.Loudness = 0.5f;
		P.MaxRange = 1500.0f;
	}
	else if (Tag == FName(Attack))
	{
		P.Loudness = 1.0f;
		P.MaxRange = 3000.0f;
	}
	else
	{
		P.Loudness = 1.0f; // any other tag is a plain noise
		P.MaxRange = 3000.0f;
	}
	return P;
}

bool MDNoise::IsLoud(FName Tag)
{
	return Tag == FName(Gunshot);
}

void MDNoise::Report(const UObject* WorldContext, const FVector& Location, AActor* Source, FName Tag)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !Source || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const FProfile P = ProfileFor(Tag);
	UAISense_Hearing::ReportNoiseEvent(const_cast<UWorld*>(World), Location, P.Loudness, Source, P.MaxRange, Tag);
}

// ---- Perception ----

float MDPerception::HearingRange(EMDIntelligenceTier Tier)
{
	return 1200.0f + 300.0f * static_cast<float>(FMath::Clamp(static_cast<int32>(Tier), 0, 4));
}

bool MDPerception::ShouldShare(EMDStimulusKind Kind, FName Tag)
{
	switch (Kind)
	{
	case EMDStimulusKind::Sight:
		return true;
	case EMDStimulusKind::Hearing:
		return MDNoise::IsLoud(Tag);
	default:
		return false; // a shared stimulus is never re-shared; damage is personal
	}
}

bool MDPerception::IsLive(EMDStimulusKind Kind)
{
	return Kind == EMDStimulusKind::Sight;
}

double MDPerception::SharedArrivalTime(double SightTime, float ShareDelay, float ReactionTime)
{
	if (ShareDelay < 0.0f)
	{
		return -1.0;
	}
	return SightTime + static_cast<double>(FMath::Max(ShareDelay, ReactionTime));
}

double MDPerception::SharedQueueTime(double SightTime, float ShareDelay, float ReactionTime)
{
	if (ShareDelay < 0.0f)
	{
		return -1.0;
	}
	return SightTime + static_cast<double>(FMath::Max(ShareDelay - ReactionTime, 0.0f));
}

bool MDPerception::IsInNearSense(float Distance2D, float Range)
{
	return Distance2D >= 0.0f && Range > 0.0f && Distance2D <= Range;
}

// ---- Dormancy ----

bool MDDormancy::ShouldSleep(float NearestPlayerDistance, double SecondsSinceDamage)
{
	if (NearestPlayerDistance < 0.0f)
	{
		return false;
	}
	if (SecondsSinceDamage >= 0.0 && SecondsSinceDamage < DamageAwakeTime)
	{
		return false;
	}
	return NearestPlayerDistance > SleepDistance;
}

float MDDormancy::NetUpdateFrequencyFor(float NearestPlayerDistance, float DefaultHz)
{
	if (NearestPlayerDistance < 0.0f || NearestPlayerDistance <= NetReduceDistance)
	{
		return DefaultHz;
	}
	return FMath::Min(DefaultHz, ReducedNetFrequency);
}

// ---- Navigation ----

bool MDNav::ShouldRepath(bool bMoving, const FVector& LastGoal, const FVector& NewGoal, float Threshold)
{
	return !bMoving || FVector::Dist2D(LastGoal, NewGoal) > Threshold;
}

bool MDNav::FPathBudget::TryConsume(uint64 FrameNumber, int32 Limit)
{
	if (FrameNumber != Frame)
	{
		Frame = FrameNumber;
		Used = 0;
	}
	if (Used >= Limit)
	{
		++Denied;
		return false;
	}
	++Used;
	return true;
}
