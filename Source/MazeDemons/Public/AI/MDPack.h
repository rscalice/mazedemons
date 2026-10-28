#pragma once

#include "CoreMinimal.h"
#include "AI/MDAITypes.h"

// V1 WP2 pack data (v1-design 2.4). Plain structs owned by UMDPackDirectorSubsystem; server only, nothing here replicates
// (the pawn's ReplicatedIntent is what clients see; the debug overlay runs on the server).

// What an enemy tells the director each decision (the brain fills it from its pawn and archetype). Read-only input.
struct FMDMemberReport
{
	TWeakObjectPtr<AActor> Target;

	// Every player this enemy currently knows about (sensed or remembered): the pack's shared knowledge is the union.
	TArray<TWeakObjectPtr<AActor>> Known;

	// Bit mask of MDPack::RoleBit(EMDRole): the archetype's AllowedRoles.
	uint32 AllowedMask = 0;

	float HealthFraction = 1.0f;
	float PreferredRange = 180.0f;
	float AttackRange = 220.0f;
	float ThreatRange = 450.0f;

	// In a stagger / stun reaction (recovering).
	bool bRecovering = false;
};

struct FMDPackMember
{
	TWeakObjectPtr<AActor> Enemy;
	int32 EnemyId = 0;
	EMDRole Role = EMDRole::None;

	// Ring slot index around the pack's focus target, -1 = none (not targeting the focus, or more than 6 members).
	int32 Slot = -1;

	FMDMemberReport Report;
};

struct FMDPackDamageRecord
{
	TWeakObjectPtr<AActor> Instigator;
	float Amount = 0.0f;
	double Time = 0.0;
};

struct FMDPack
{
	int32 PackId = 0;

	// The spawn group's key (the level-placed ghouls of one group share it); NAME_None for a loose enemy.
	FName GroupKey;

	// The room the group spawned in (PDG spawn groups pass it; used to pick the nearest rally point later).
	FName RoomRef;

	// Members the pack spawned with, never decreased by deaths: the retreat threshold is a share of this.
	int32 SpawnCount = 0;

	TArray<FMDPackMember> Members;

	TWeakObjectPtr<AActor> FocusTarget;
	float FocusScore = 0.0f;
	double LastRetargetTime = -1000.0;

	EMDPackState State = EMDPackState::Engage;
	double RegroupEndTime = 0.0;

	// This pack already retreated and has not recovered above the threshold since (a pack retreats once per low point).
	bool bRetreatDone = false;

	bool bRolesDirty = true;

	// The ring's rotation in the world (degrees), fixed per pack so the ring does not spin as the target turns.
	float RingYawDeg = 0.0f;
	float SlotRadius = 200.0f;
	float ThreatRadius = 450.0f;

	TArray<FMDPackDamageRecord> Damage;

	// The rally point the retreat runs to (chosen when the retreat starts).
	FVector RallyPoint = FVector::ZeroVector;
	bool bHasRally = false;

	int32 NumAlive() const { return Members.Num(); }

	FMDPackMember* FindMember(const AActor* Enemy)
	{
		return Members.FindByPredicate([Enemy](const FMDPackMember& M) { return M.Enemy.Get() == Enemy; });
	}
	const FMDPackMember* FindMember(const AActor* Enemy) const
	{
		return Members.FindByPredicate([Enemy](const FMDPackMember& M) { return M.Enemy.Get() == Enemy; });
	}
};

// What the director hands a brain: everything it needs to position its enemy. Computed on read from the live target position.
struct FMDEnemyAssignment
{
	int32 PackId = 0;
	EMDRole Role = EMDRole::None;
	EMDPackState PackState = EMDPackState::Engage;
	TWeakObjectPtr<AActor> FocusTarget;

	int32 Slot = -1;
	// Valid when Slot >= 0 and the focus target is known: the melee ring point and the 4-6 m threat ring point on the same angle.
	bool bHasSlotPoint = false;
	FVector SlotPoint = FVector::ZeroVector;
	FVector ThreatPoint = FVector::ZeroVector;

	bool bHasRally = false;
	FVector RallyPoint = FVector::ZeroVector;
};
