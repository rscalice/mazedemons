#include "AI/MDAIRules.h"

#include "MDGameplayTags.h"
#include "Math/RandomStream.h"

FGameplayTag MDIntentToTag(EMDIntent Intent)
{
	switch (Intent)
	{
	case EMDIntent::Idle: return TAG_AI_Intent_Idle.GetTag();
	case EMDIntent::Chase: return TAG_AI_Intent_Chase.GetTag();
	case EMDIntent::HoldThreat: return TAG_AI_Intent_HoldThreat.GetTag();
	case EMDIntent::Attack: return TAG_AI_Intent_Attack.GetTag();
	case EMDIntent::Retreat: return TAG_AI_Intent_Retreat.GetTag();
	case EMDIntent::Regroup: return TAG_AI_Intent_Regroup.GetTag();
	case EMDIntent::Stagger: return TAG_AI_Intent_Stagger.GetTag();
	case EMDIntent::Dead: return TAG_AI_Intent_Dead.GetTag();
	default: return FGameplayTag();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// MDAITypes implementations (the header is data only)
// ---------------------------------------------------------------------------------------------------------------------

const TCHAR* MDIntentToString(EMDIntent Intent)
{
	switch (Intent)
	{
	case EMDIntent::Idle: return TEXT("Idle");
	case EMDIntent::Patrol: return TEXT("Patrol");
	case EMDIntent::Investigate: return TEXT("Investigate");
	case EMDIntent::Chase: return TEXT("Chase");
	case EMDIntent::HoldThreat: return TEXT("HoldThreat");
	case EMDIntent::Attack: return TEXT("Attack");
	case EMDIntent::Feint: return TEXT("Feint");
	case EMDIntent::Bait: return TEXT("Bait");
	case EMDIntent::Kite: return TEXT("Kite");
	case EMDIntent::Flank: return TEXT("Flank");
	case EMDIntent::Retreat: return TEXT("Retreat");
	case EMDIntent::Regroup: return TEXT("Regroup");
	case EMDIntent::Guard: return TEXT("Guard");
	case EMDIntent::Ambush: return TEXT("Ambush");
	case EMDIntent::Blink: return TEXT("Blink");
	case EMDIntent::Charge: return TEXT("Charge");
	case EMDIntent::Stagger: return TEXT("Stagger");
	case EMDIntent::Dead: return TEXT("Dead");
	default: return TEXT("?");
	}
}

FMDTierParams FMDTierParams::ForTier(EMDIntelligenceTier InTier)
{
	// v1-design 3.2 (common parameters). Sight radius / FOV, reaction, tell, tokens, aggression, noise, share delay.
	FMDTierParams P;
	P.Tier = InTier;
	switch (InTier)
	{
	case EMDIntelligenceTier::T1:
		P.ReactionTime = 0.7f; P.TellTimeMultiplier = 0.9f; P.MaxTokensPerPlayer = 2; P.SightRadius = 2000.0f; P.SightFOV = 120.0f;
		P.Aggression = 0.8f; P.NoiseAmplitude = 0.08f; P.ShareDelay = 2.0f;
		break;
	case EMDIntelligenceTier::T2:
		P.ReactionTime = 0.5f; P.TellTimeMultiplier = 0.8f; P.MaxTokensPerPlayer = 2; P.SightRadius = 2500.0f; P.SightFOV = 140.0f;
		P.Aggression = 1.0f; P.NoiseAmplitude = 0.12f; P.ShareDelay = 1.0f;
		break;
	case EMDIntelligenceTier::T3:
		P.ReactionTime = 0.4f; P.TellTimeMultiplier = 0.7f; P.MaxTokensPerPlayer = 3; P.SightRadius = 3000.0f; P.SightFOV = 150.0f;
		P.Aggression = 1.1f; P.NoiseAmplitude = 0.15f; P.ShareDelay = 0.5f;
		break;
	case EMDIntelligenceTier::T4:
		P.ReactionTime = 0.3f; P.TellTimeMultiplier = 0.6f; P.MaxTokensPerPlayer = 3; P.SightRadius = 3000.0f; P.SightFOV = 160.0f;
		P.Aggression = 1.2f; P.NoiseAmplitude = 0.15f; P.ShareDelay = 0.25f;
		break;
	case EMDIntelligenceTier::T0:
	default:
		P.ReactionTime = 0.9f; P.TellTimeMultiplier = 1.0f; P.MaxTokensPerPlayer = 1; P.SightRadius = 1500.0f; P.SightFOV = 110.0f;
		P.Aggression = 0.6f; P.NoiseAmplitude = 0.05f; P.ShareDelay = -1.0f;
		break;
	}
	return P;
}

bool FMDTierParams::IsIntentUnlocked(EMDIntent Intent) const
{
	const int32 T = static_cast<int32>(Tier);
	switch (Intent)
	{
	case EMDIntent::Idle:
	case EMDIntent::Chase:
	case EMDIntent::Attack:
	case EMDIntent::Charge:
	case EMDIntent::HoldThreat: // fairness fallback, every tier (see the header)
	case EMDIntent::Stagger:
	case EMDIntent::Dead:
		return true;
	case EMDIntent::Kite:
		return T >= 1;
	case EMDIntent::Bait:
	case EMDIntent::Flank:
	case EMDIntent::Retreat:
	case EMDIntent::Regroup:
	case EMDIntent::Ambush:
	case EMDIntent::Guard:
		return T >= 2;
	case EMDIntent::Feint:
	case EMDIntent::Blink:
		return T >= 3;
	case EMDIntent::Patrol:
	case EMDIntent::Investigate:
	default:
		return false; // not in V1 WP1
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FMDUtilityScorer
// ---------------------------------------------------------------------------------------------------------------------

namespace MDScorerLocal
{
	// Scoring slots, in a fixed order so the noise draw per intent is stable.
	// New intents are appended so the noise draw of the existing ones stays the same.
	constexpr EMDIntent Scored[] = { EMDIntent::Idle, EMDIntent::Chase, EMDIntent::HoldThreat, EMDIntent::Attack, EMDIntent::Retreat, EMDIntent::Regroup };
	constexpr int32 NumScored = UE_ARRAY_COUNT(Scored);

	float Base(EMDIntent Intent, const FMDBrainContext& C, const FMDTierParams& Tier)
	{
		switch (Intent)
		{
		case EMDIntent::Idle:
			return C.bHasTarget ? 0.05f : 0.3f;

		case EMDIntent::Chase:
		{
			if (!C.bHasTarget)
			{
				return 0.0f;
			}
			if (C.DistanceToTarget <= C.AttackRange)
			{
				return 0.1f; // nothing to close; Attack or HoldThreat take over
			}
			if (!C.bHasToken && C.DistanceToTarget <= C.ThreatRange)
			{
				return 0.2f; // inside the threat ring with no token: hold instead
			}
			// Further away = stronger pull, saturating 15 m beyond the attack range.
			const float Beyond = FMath::Clamp((C.DistanceToTarget - C.AttackRange) / 1500.0f, 0.0f, 1.0f);
			return 0.5f + 0.3f * Beyond;
		}

		case EMDIntent::HoldThreat:
			if (!C.bHasTarget || C.bHasToken)
			{
				return 0.0f;
			}
			return C.DistanceToTarget <= C.ThreatRange + 100.0f ? 0.6f : 0.0f;

		case EMDIntent::Attack:
			if (!C.bHasTarget || !C.bHasToken || !C.bAttackReady || !C.bTargetSensed)
			{
				return 0.0f; // the token gate is hard: no token, no swing (1.3)
			}
			if (C.DistanceToTarget > C.AttackRange)
			{
				return 0.0f;
			}
			return 0.45f + 0.5f * Tier.Aggression; // T0 0.75, T4 1.05

		// The pack is retreating (pack state Regroup, T2+ via the tier gate): run to the rally point, then wait there. Both
		// outrank everything that is not already committed (an attack in progress is never re-scored).
		case EMDIntent::Retreat:
			return (C.PackState == EMDPackState::Regroup && C.bHasRally && !C.bAtRally) ? 1.3f : 0.0f;

		case EMDIntent::Regroup:
			return (C.PackState == EMDPackState::Regroup && C.bHasRally && C.bAtRally) ? 1.3f : 0.0f;

		default:
			return 0.0f;
		}
	}
}

float FMDUtilityScorer::Noise(int32 RunSeed, int32 EnemyId, int32 DecisionIndex, int32 Slot, float Amplitude)
{
	if (Amplitude <= 0.0f)
	{
		return 0.0f;
	}
	const uint32 Hash = HashCombine(HashCombine(GetTypeHash(RunSeed), GetTypeHash(EnemyId)), GetTypeHash(DecisionIndex));
	FRandomStream Stream(static_cast<int32>(Hash));
	float Value = 0.0f;
	for (int32 i = 0; i <= Slot; ++i)
	{
		Value = Stream.FRandRange(-Amplitude, Amplitude);
	}
	return Value;
}

MDAI::FScores FMDUtilityScorer::Score(const FMDBrainContext& Context, const FMDArchetypeAIParams& Archetype, const FMDTierParams& Tier)
{
	MDAI::FScores Out;
	if (Context.bDead)
	{
		return Out; // nothing to choose
	}
	for (int32 Slot = 0; Slot < MDScorerLocal::NumScored; ++Slot)
	{
		const EMDIntent Intent = MDScorerLocal::Scored[Slot];
		FMDScoredIntent& S = Out.AddDefaulted_GetRef();
		S.Intent = Intent;
		if (!Tier.IsIntentUnlocked(Intent))
		{
			S.Score = 0.0f;
			continue;
		}
		const float Base = MDScorerLocal::Base(Intent, Context, Tier);
		// Noise only perturbs live options: a hard gate (score 0) stays 0.
		S.Score = Base > 0.0f ? FMath::Max(Base + Noise(Context.RunSeed, Context.EnemyId, Context.DecisionIndex, Slot, Tier.NoiseAmplitude), 0.0001f) : 0.0f;
	}
	return Out;
}

const FMDScoredIntent* FMDUtilityScorer::Find(const MDAI::FScores& Scores, EMDIntent Intent)
{
	for (const FMDScoredIntent& S : Scores)
	{
		if (S.Intent == Intent)
		{
			return &S;
		}
	}
	return nullptr;
}

EMDIntent FMDUtilityScorer::Select(const MDAI::FScores& Scores, EMDIntent Current, bool bCurrentFinished, float SwitchMargin)
{
	const FMDScoredIntent* Best = nullptr;
	for (const FMDScoredIntent& S : Scores)
	{
		if (!Best || S.Score > Best->Score)
		{
			Best = &S;
		}
	}
	if (!Best)
	{
		return EMDIntent::Idle;
	}
	if (bCurrentFinished || Best->Intent == Current)
	{
		return Best->Intent;
	}
	const FMDScoredIntent* Cur = Find(Scores, Current);
	if (Cur && Cur->Score > 0.0f && Best->Score < Cur->Score + SwitchMargin)
	{
		return Current; // not convincing enough to switch
	}
	return Best->Intent;
}

void FMDUtilityScorer::SortDescending(MDAI::FScores& Scores)
{
	Scores.Sort([](const FMDScoredIntent& A, const FMDScoredIntent& B) { return A.Score > B.Score; });
}

// ---------------------------------------------------------------------------------------------------------------------
// FMDDelayedStimulusQueue
// ---------------------------------------------------------------------------------------------------------------------

int32 FMDDelayedStimulusQueue::Pop(double Now, double Delay, TFunctionRef<void(const FMDStimulus&)> Fn)
{
	// Deliver in order of arrival; the queue is short (a few events per enemy), so a stable pass is fine.
	int32 Delivered = 0;
	for (int32 i = 0; i < Pending.Num();)
	{
		if (Pending[i].Time + Delay <= Now)
		{
			const FMDStimulus S = Pending[i];
			Pending.RemoveAt(i, EAllowShrinking::No);
			Fn(S);
			++Delivered;
		}
		else
		{
			++i;
		}
	}
	return Delivered;
}

// ---------------------------------------------------------------------------------------------------------------------
// FMDAttackTokenPool
// ---------------------------------------------------------------------------------------------------------------------

bool FMDAttackTokenPool::TryGrant(int32 Holder, int32 Target, double Now, int32 MaxPerTarget, double Timeout)
{
	Purge(Now);
	for (FEntry& E : Entries)
	{
		if (E.Holder == Holder)
		{
			// Already holding: keep it (retarget to the new target, refresh the timeout).
			E.Target = Target;
			E.ExpireTime = Now + Timeout;
			return true;
		}
	}
	if (CountForTarget(Target, Now) >= MaxPerTarget)
	{
		return false;
	}
	FEntry& New = Entries.AddDefaulted_GetRef();
	New.Holder = Holder;
	New.Target = Target;
	New.ExpireTime = Now + Timeout;
	return true;
}

bool FMDAttackTokenPool::Holds(int32 Holder, double Now) const
{
	for (const FEntry& E : Entries)
	{
		if (E.Holder == Holder && Now < E.ExpireTime)
		{
			return true;
		}
	}
	return false;
}

int32 FMDAttackTokenPool::GetTarget(int32 Holder, double Now) const
{
	for (const FEntry& E : Entries)
	{
		if (E.Holder == Holder && Now < E.ExpireTime)
		{
			return E.Target;
		}
	}
	return 0;
}

int32 FMDAttackTokenPool::CountForTarget(int32 Target, double Now) const
{
	int32 Count = 0;
	for (const FEntry& E : Entries)
	{
		if (E.Target == Target && Now < E.ExpireTime)
		{
			++Count;
		}
	}
	return Count;
}

void FMDAttackTokenPool::Release(int32 Holder)
{
	Entries.RemoveAll([Holder](const FEntry& E) { return E.Holder == Holder; });
}

void FMDAttackTokenPool::ReleaseAfter(int32 Holder, double Now, double Delay)
{
	for (FEntry& E : Entries)
	{
		if (E.Holder == Holder)
		{
			E.ExpireTime = FMath::Min(E.ExpireTime, Now + FMath::Max(Delay, 0.0));
		}
	}
}

void FMDAttackTokenPool::Purge(double Now)
{
	Entries.RemoveAll([Now](const FEntry& E) { return Now >= E.ExpireTime; });
}

// ---------------------------------------------------------------------------------------------------------------------
// Scheduling and attack timing
// ---------------------------------------------------------------------------------------------------------------------

int32 MDBrainScheduling::BatchSize(int32 NumBrains, float Hz, float DeltaTime, float& Accumulator)
{
	if (NumBrains <= 0 || Hz <= 0.0f || DeltaTime <= 0.0f)
	{
		return 0;
	}
	Accumulator += static_cast<float>(NumBrains) * Hz * DeltaTime;
	// Never more than one full round per frame (a hitch should not run every brain several times).
	const int32 Batch = FMath::Min(FMath::FloorToInt(Accumulator), NumBrains);
	Accumulator -= static_cast<float>(Batch);
	if (Accumulator > static_cast<float>(NumBrains))
	{
		Accumulator = static_cast<float>(NumBrains);
	}
	return Batch;
}

float MDEnemyAttackRules::WindupRate(float TraceWindowStart, float TellTime, float MinRate)
{
	if (TraceWindowStart <= 0.0f || TellTime <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(TraceWindowStart / TellTime, FMath::Clamp(MinRate, 0.05f, 1.0f), 1.0f);
}

bool MDEnemyAttackRules::OwnsAttackMovement(ENetRole LocalRole)
{
	// Enemies are server-owned pawns: the authority is the only machine whose movement component may consume root motion.
	// (An autonomous proxy would predict its own; enemies never are one.)
	return LocalRole == ROLE_Authority;
}

ERootMotionMode::Type MDEnemyAttackRules::RootMotionModeFor(ENetRole LocalRole)
{
	return OwnsAttackMovement(LocalRole) ? ERootMotionMode::RootMotionFromMontagesOnly : ERootMotionMode::IgnoreRootMotion;
}

FRotator MDEnemyAttackRules::FacingRotation(float TargetYaw)
{
	return FRotator(0.0, FRotator::NormalizeAxis(TargetYaw), 0.0);
}

bool MDEnemyAttackRules::IsTilted(const FRotator& Rotation, float ToleranceDegrees)
{
	return FMath::Abs(FRotator::NormalizeAxis(Rotation.Pitch)) > ToleranceDegrees || FMath::Abs(FRotator::NormalizeAxis(Rotation.Roll)) > ToleranceDegrees;
}
