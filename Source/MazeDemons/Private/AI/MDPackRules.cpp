#include "AI/MDPackRules.h"

#include "AI/MDBehaviourRules.h"
#include "Math/RandomStream.h"

namespace MDPackLocal
{
	int32 TierIndex(EMDIntelligenceTier Tier)
	{
		return static_cast<int32>(Tier);
	}

	float NormalizeAngle(float Deg)
	{
		float A = FMath::Fmod(Deg, 360.0f);
		return A < 0.0f ? A + 360.0f : A;
	}

	// Roles an archetype may take, with the Striker fallback for an empty list.
	uint32 EffectiveMask(uint32 Allowed)
	{
		return Allowed != 0 ? Allowed : MDPack::RoleBit(EMDRole::Striker);
	}

	bool Allows(uint32 Allowed, EMDRole Role)
	{
		return (EffectiveMask(Allowed) & MDPack::RoleBit(Role)) != 0;
	}

	// The role a member takes before the tier-specific ones: Striker when allowed, else the first allowed combat role.
	EMDRole BaseRole(uint32 Allowed)
	{
		static const EMDRole Order[] = { EMDRole::Striker, EMDRole::Flanker, EMDRole::Harasser, EMDRole::Guard, EMDRole::Bait, EMDRole::Leader };
		for (const EMDRole Role : Order)
		{
			if (Allows(Allowed, Role))
			{
				return Role;
			}
		}
		return EMDRole::Striker;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Tier table
// ---------------------------------------------------------------------------------------------------------------------

int32 MDPackRules::FlankersFor(EMDIntelligenceTier Tier)
{
	switch (Tier)
	{
	case EMDIntelligenceTier::T2:
	case EMDIntelligenceTier::T3:
		return 1;
	case EMDIntelligenceTier::T4:
		return 2;
	default:
		return 0;
	}
}

float MDPackRules::RetargetInterval(EMDIntelligenceTier Tier)
{
	switch (Tier)
	{
	case EMDIntelligenceTier::T1: return 6.0f;
	case EMDIntelligenceTier::T2: return 3.0f;
	case EMDIntelligenceTier::T3: return 2.0f;
	case EMDIntelligenceTier::T4: return 1.5f;
	case EMDIntelligenceTier::T0:
	default:
		return -1.0f;
	}
}

bool MDPackRules::CanRetarget(EMDIntelligenceTier Tier, double SecondsSinceLastRetarget, bool bFocusValid)
{
	if (!bFocusValid)
	{
		return true; // nothing to keep: always pick
	}
	const float Interval = RetargetInterval(Tier);
	return Interval >= 0.0f && SecondsSinceLastRetarget >= static_cast<double>(Interval);
}

float MDPackRules::RetreatFraction(EMDIntelligenceTier Tier)
{
	switch (Tier)
	{
	case EMDIntelligenceTier::T2: return 0.4f;
	case EMDIntelligenceTier::T3:
	case EMDIntelligenceTier::T4:
		return 0.5f;
	default:
		return -1.0f;
	}
}

bool MDPackRules::ShouldRetreat(EMDIntelligenceTier Tier, int32 Alive, int32 SpawnCount, bool bHasRally)
{
	const float Fraction = RetreatFraction(Tier);
	if (Fraction < 0.0f || !bHasRally || Alive <= 0 || SpawnCount <= 0)
	{
		return false;
	}
	return static_cast<float>(Alive) <= static_cast<float>(SpawnCount) * Fraction + KINDA_SMALL_NUMBER;
}

bool MDPackRules::ShouldMerge(EMDIntelligenceTier Tier, float MinMemberDistance, bool bSharedTarget)
{
	return MDPackLocal::TierIndex(Tier) >= MDPackLocal::TierIndex(EMDIntelligenceTier::T2) && bSharedTarget && MinMemberDistance <= MDPack::MergeDistance;
}

EMDPackState MDPackRules::ResolvePackState(EMDPackState Current, EMDIntelligenceTier Tier, int32 Alive, int32 SpawnCount, bool bHasRally,
	bool bRetreatDone, bool bRegroupElapsed, int32 TokensHeldOnFocus, int32 MaxTokens)
{
	const float Fraction = RetreatFraction(Tier);
	const bool bBelow = Fraction >= 0.0f && Alive > 0 && SpawnCount > 0
		&& static_cast<float>(Alive) <= static_cast<float>(SpawnCount) * Fraction + KINDA_SMALL_NUMBER;

	if (bBelow)
	{
		if (Current == EMDPackState::Regroup)
		{
			return bRegroupElapsed ? EMDPackState::Routed : EMDPackState::Regroup; // came back still under the threshold: fight on
		}
		if (bHasRally && !bRetreatDone)
		{
			return EMDPackState::Regroup;
		}
		return EMDPackState::Routed;
	}
	// Above the threshold again (reinforcements, a merge) or never below: the plain fight.
	return (MaxTokens > 0 && TokensHeldOnFocus >= MaxTokens) ? EMDPackState::Pressing : EMDPackState::Engage;
}

// ---------------------------------------------------------------------------------------------------------------------
// Roles
// ---------------------------------------------------------------------------------------------------------------------

int32 MDPackRules::RoleRank(EMDRole Role)
{
	switch (Role)
	{
	case EMDRole::Striker: return 3;
	case EMDRole::Flanker: return 2;
	default: return 1;
	}
}

void MDPackRules::AssignRoles(const TArray<FMDRoleCandidate>& Candidates, EMDIntelligenceTier Tier, TArray<EMDRole>& OutRoles)
{
	using namespace MDPackLocal;
	const int32 Num = Candidates.Num();
	OutRoles.Reset();
	OutRoles.Init(EMDRole::None, Num);
	if (Num == 0)
	{
		return;
	}

	// Rank by Id so the result does not depend on array order.
	TArray<int32> Order;
	Order.Reserve(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		Order.Add(i);
	}
	Order.Sort([&Candidates](int32 A, int32 B) { return Candidates[A].Id < Candidates[B].Id; });

	for (int32 i = 0; i < Num; ++i)
	{
		OutRoles[i] = BaseRole(Candidates[i].AllowedMask);
	}

	// V1 WP5 protect the caster (T2+): the members that allow Guard (and are not the caster) screen it, at most MaxGuards.
	const bool bHasCaster = Candidates.ContainsByPredicate([](const FMDRoleCandidate& C) { return C.bIsCaster; });
	if (bHasCaster && TierIndex(Tier) >= TierIndex(EMDIntelligenceTier::T2))
	{
		int32 Guards = 0;
		for (const int32 Index : Order)
		{
			if (Guards < MDBehaviour::MaxGuards && !Candidates[Index].bIsCaster && Allows(Candidates[Index].AllowedMask, EMDRole::Guard))
			{
				OutRoles[Index] = EMDRole::Guard;
				++Guards;
			}
		}
	}

	const bool bPackTactics = TierIndex(Tier) >= TierIndex(EMDIntelligenceTier::T2) && Num >= 3; // 2 or fewer: everyone strikes

	if (bPackTactics)
	{
		// Leader: one per pack, the first member (by Id) that allows it, unless a Leader-only archetype already is one.
		if (!OutRoles.Contains(EMDRole::Leader))
		{
			for (const int32 Index : Order)
			{
				if (OutRoles[Index] != EMDRole::Guard && Allows(Candidates[Index].AllowedMask, EMDRole::Leader))
				{
					OutRoles[Index] = EMDRole::Leader;
					break;
				}
			}
		}

		// Bait: only one, only with 3 or more alive. The last member in Id order that allows it (the newest spawn is the
		// least engaged), never the Leader.
		for (int32 k = Order.Num() - 1; k >= 0; --k)
		{
			const int32 Index = Order[k];
			if (OutRoles[Index] != EMDRole::Leader && OutRoles[Index] != EMDRole::Guard && Allows(Candidates[Index].AllowedMask, EMDRole::Bait))
			{
				OutRoles[Index] = EMDRole::Bait;
				break;
			}
		}

		// Flankers: FlankersFor(Tier) of the remaining Strikers that allow it, lowest Id first. At least one Striker stays.
		int32 Wanted = FlankersFor(Tier);
		int32 Strikers = 0;
		for (const EMDRole Role : OutRoles)
		{
			Strikers += Role == EMDRole::Striker ? 1 : 0;
		}
		for (const int32 Index : Order)
		{
			if (Wanted <= 0 || Strikers <= 1)
			{
				break;
			}
			if (OutRoles[Index] == EMDRole::Striker && Allows(Candidates[Index].AllowedMask, EMDRole::Flanker))
			{
				OutRoles[Index] = EMDRole::Flanker;
				--Wanted;
				--Strikers;
			}
		}
	}
}

float MDPackRules::TokenPriority(EMDRole Role, float DistanceToTarget, int32 RunSeed, int32 EnemyId)
{
	const float Closeness = FMath::Clamp(1.0f - DistanceToTarget / 2000.0f, 0.0f, 1.0f);
	FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(RunSeed), GetTypeHash(EnemyId))));
	const float Noise = Stream.FRandRange(0.0f, 10.0f);
	return static_cast<float>(RoleRank(Role)) * 1000.0f + Closeness * 100.0f + Noise;
}

// ---------------------------------------------------------------------------------------------------------------------
// Focus target
// ---------------------------------------------------------------------------------------------------------------------

float MDPackRules::NormalizeIsolation(float DistanceToNearestAlly)
{
	if (DistanceToNearestAlly < 0.0f)
	{
		return 1.0f; // no other player at all: as isolated as it gets
	}
	return FMath::Clamp(DistanceToNearestAlly / 1500.0f, 0.0f, 1.0f);
}

float MDPackRules::FocusScore(const FMDFocusInputs& I)
{
	return MDPack::WeightIsolation * FMath::Clamp(I.Isolation, 0.0f, 1.0f)
		+ MDPack::WeightHealth * (1.0f - FMath::Clamp(I.HealthFraction, 0.0f, 1.0f))
		+ MDPack::WeightStaggered * (I.bStaggered ? 1.0f : 0.0f)
		+ MDPack::WeightThreat * FMath::Clamp(I.Threat, 0.0f, 1.0f)
		+ MDPack::WeightCrowding * FMath::Clamp(I.Crowding, 0.0f, 1.0f);
}

int32 MDPackRules::PickFocus(const TArray<float>& Scores, int32 CurrentIndex, float Margin)
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Scores.Num(); ++i)
	{
		if (Best == INDEX_NONE || Scores[i] > Scores[Best])
		{
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	if (Scores.IsValidIndex(CurrentIndex) && Best != CurrentIndex && Scores[Best] < Scores[CurrentIndex] + Margin)
	{
		return CurrentIndex;
	}
	return Best;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ring slots
// ---------------------------------------------------------------------------------------------------------------------

float MDPackRules::SlotAngle(int32 Slot, float BaseYawDeg)
{
	return MDPackLocal::NormalizeAngle(BaseYawDeg + static_cast<float>(Slot) * (360.0f / static_cast<float>(MDPack::NumRingSlots)));
}

FVector MDPackRules::SlotLocation(const FVector& TargetLocation, int32 Slot, float BaseYawDeg, float Radius)
{
	const float Rad = FMath::DegreesToRadians(SlotAngle(Slot, BaseYawDeg));
	return TargetLocation + FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f) * Radius;
}

float MDPackRules::SlotRadiusFor(float PreferredRange, float AttackRange)
{
	float Radius = FMath::Clamp(PreferredRange, MDPack::SlotRadiusMin, MDPack::SlotRadiusMax);
	if (AttackRange > 0.0f)
	{
		Radius = FMath::Min(Radius, AttackRange * 0.9f);
	}
	return Radius;
}

float MDPackRules::ThreatRadiusFor(float ThreatRange)
{
	return FMath::Clamp(ThreatRange, MDPack::ThreatRadiusMin, MDPack::ThreatRadiusMax);
}

float MDPackRules::AngleDiff(float A, float B)
{
	const float D = FMath::Abs(MDPackLocal::NormalizeAngle(A) - MDPackLocal::NormalizeAngle(B));
	return D > 180.0f ? 360.0f - D : D;
}

float MDPackRules::SlotAngleFromFacing(int32 Slot, float BaseYawDeg, float FacingYawDeg)
{
	return AngleDiff(SlotAngle(Slot, BaseYawDeg), FacingYawDeg);
}

void MDPackRules::AssignSlots(const TArray<FMDSlotMember>& Members, float BaseYawDeg, float FacingYawDeg, TArray<int32>& OutSlots, bool bEvenSpread)
{
	OutSlots.Reset();
	OutSlots.Init(-1, Members.Num());

	// Flankers first, then Strikers, then the rest; Id breaks ties.
	TArray<int32> Order;
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		Order.Add(i);
	}
	auto Phase = [&Members](int32 Index)
	{
		switch (Members[Index].Role)
		{
		case EMDRole::Flanker: return 0;
		case EMDRole::Striker: return 1;
		default: return 2;
		}
	};
	Order.Sort([&](int32 A, int32 B)
	{
		const int32 PA = Phase(A);
		const int32 PB = Phase(B);
		return PA != PB ? PA < PB : Members[A].Id < Members[B].Id;
	});

	bool bTaken[MDPack::NumRingSlots] = {};
	if (bEvenSpread && Members.Num() > 0 && Members.Num() < MDPack::NumRingSlots)
	{
		// Swarm: block every slot outside the evenly spaced set that costs the pack the least walking.
		TArray<int32> BestSet;
		float BestCost = TNumericLimits<float>::Max();
		for (int32 Rotation = 0; Rotation < MDPack::NumRingSlots; ++Rotation)
		{
			TArray<int32> Set;
			MDBehaviour::EvenSlotSet(Members.Num(), Rotation, Set);
			float Cost = 0.0f;
			for (const FMDSlotMember& M : Members)
			{
				float Nearest = 360.0f;
				for (const int32 Slot : Set)
				{
					Nearest = FMath::Min(Nearest, AngleDiff(M.AngleDeg, SlotAngle(Slot, BaseYawDeg)));
				}
				Cost += Nearest;
			}
			if (Cost < BestCost)
			{
				BestCost = Cost;
				BestSet = Set;
			}
		}
		for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
		{
			bTaken[Slot] = !BestSet.Contains(Slot);
		}
	}
	for (const int32 Index : Order)
	{
		const FMDSlotMember& M = Members[Index];
		const bool bFlanker = M.Role == EMDRole::Flanker;

		int32 Best = -1;
		float BestCost = TNumericLimits<float>::Max();
		// Two passes for a flanker: behind-the-player slots only, then anything free.
		for (int32 Pass = 0; Pass < (bFlanker ? 2 : 1) && Best < 0; ++Pass)
		{
			for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
			{
				if (bTaken[Slot])
				{
					continue;
				}
				if (bFlanker && Pass == 0 && SlotAngleFromFacing(Slot, BaseYawDeg, FacingYawDeg) < MDPack::FlankMinAngle)
				{
					continue;
				}
				float Cost = AngleDiff(M.AngleDeg, SlotAngle(Slot, BaseYawDeg));
				if (Slot == M.CurrentSlot)
				{
					Cost -= 25.0f;
				}
				if (Cost < BestCost)
				{
					BestCost = Cost;
					Best = Slot;
				}
			}
		}
		if (Best >= 0)
		{
			bTaken[Best] = true;
			OutSlots[Index] = Best;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FMDPackTokenPool
// ---------------------------------------------------------------------------------------------------------------------

FMDPackTokenPool::FGrantResult FMDPackTokenPool::TryGrant(int32 Holder, int32 Target, double Now, int32 MaxPerTarget, float Priority, double Timeout)
{
	FGrantResult Result;
	Purge(Now);
	for (FEntry& E : Entries)
	{
		if (E.Holder == Holder)
		{
			// Already holding: keep it (retarget to the new target, refresh the timeout and the priority). A committed token
			// keeps its expiry: the attack in progress owns it.
			if (E.Target != Target)
			{
				E.bCommitted = false;
			}
			E.Target = Target;
			E.Priority = Priority;
			if (!E.bCommitted)
			{
				E.ExpireTime = Now + Timeout;
			}
			Result.bGranted = true;
			return Result;
		}
	}
	if (MaxPerTarget <= 0)
	{
		return Result;
	}
	if (CountForTarget(Target, Now) >= MaxPerTarget)
	{
		// Full: take the lowest priority uncommitted token if this requester beats it by the margin.
		FEntry* Victim = nullptr;
		for (FEntry& E : Entries)
		{
			if (E.Target == Target && !E.bCommitted && Now < E.ExpireTime && (!Victim || E.Priority < Victim->Priority))
			{
				Victim = &E;
			}
		}
		if (!Victim || Priority < Victim->Priority + MDPack::TokenEvictMargin)
		{
			return Result;
		}
		Result.EvictedHolder = Victim->Holder;
		Entries.RemoveAll([Victim](const FEntry& E) { return &E == Victim; });
	}
	FEntry& New = Entries.AddDefaulted_GetRef();
	New.Holder = Holder;
	New.Target = Target;
	New.ExpireTime = Now + Timeout;
	New.Priority = Priority;
	Result.bGranted = true;
	return Result;
}

bool FMDPackTokenPool::Holds(int32 Holder, double Now) const
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

int32 FMDPackTokenPool::GetTarget(int32 Holder, double Now) const
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

int32 FMDPackTokenPool::CountForTarget(int32 Target, double Now) const
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

void FMDPackTokenPool::Commit(int32 Holder)
{
	for (FEntry& E : Entries)
	{
		if (E.Holder == Holder)
		{
			E.bCommitted = true;
		}
	}
}

void FMDPackTokenPool::Release(int32 Holder)
{
	Entries.RemoveAll([Holder](const FEntry& E) { return E.Holder == Holder; });
}

void FMDPackTokenPool::ReleaseAfter(int32 Holder, double Now, double Delay)
{
	for (FEntry& E : Entries)
	{
		if (E.Holder == Holder)
		{
			E.ExpireTime = FMath::Min(E.ExpireTime, Now + FMath::Max(Delay, 0.0));
			E.bCommitted = true; // it attacked: nobody takes the token during the 0.5 s hold either
		}
	}
}

void FMDPackTokenPool::Purge(double Now)
{
	Entries.RemoveAll([Now](const FEntry& E) { return Now >= E.ExpireTime; });
}

void FMDPackTokenPool::ForEachValid(double Now, TFunctionRef<void(int32, int32, double, bool)> Fn) const
{
	for (const FEntry& E : Entries)
	{
		if (Now < E.ExpireTime)
		{
			Fn(E.Holder, E.Target, E.ExpireTime - Now, E.bCommitted);
		}
	}
}

double FMDPackTokenPool::MaxSecondsLeft(double Now) const
{
	double Max = 0.0;
	for (const FEntry& E : Entries)
	{
		Max = FMath::Max(Max, E.ExpireTime - Now);
	}
	return Max;
}
