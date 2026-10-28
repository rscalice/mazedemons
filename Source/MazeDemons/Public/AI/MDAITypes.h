#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MDAITypes.generated.h"

class UDataTable;

// V1 enemy framework types (v1-design 2.3 / 2.4 / 3.1). Pure data: no behaviour lives here.

// What an enemy is doing right now. The brain (V1 WP1) writes it; ReplicatedIntent mirrors it to clients. Idle = 0 so a
// zero-initialised value is a sensible default.
UENUM(BlueprintType)
enum class EMDIntent : uint8
{
	Idle = 0,
	Patrol,
	Investigate,
	Chase,
	HoldThreat,
	Attack,
	Feint,
	Bait,
	Kite,
	Flank,
	Retreat,
	Regroup,
	Guard,
	Ambush,
	Blink,
	Charge,
	Stagger,
	Dead
};

// Pack role (2.4). None = 0 (loose enemy, not yet assigned).
UENUM(BlueprintType)
enum class EMDRole : uint8
{
	None = 0,
	Striker,
	Flanker,
	Harasser,
	Bait,
	Guard,
	Leader
};

// Pack state (2.4). Engage = 0 so a loose enemy's default is the plain fight. Ambush-armed arrives with WP5.
//   Engage    the normal fight.
//   Pressing  the pack has all of its tokens on the focus target.
//   Regroup   retreat triggered: every member runs to the rally point, waits there, then comes back (state returns to Engage).
//   Routed    the pack is under its retreat threshold but cannot retreat (no rally point, or it already did): it fights on.
UENUM(BlueprintType)
enum class EMDPackState : uint8
{
	Engage = 0,
	Pressing,
	Regroup,
	Routed
};

// Intelligence tier T0 (basic) .. T4 (smartest). The tier table row is FMDIntelligenceTier (DT_IntelligenceTiers, V1 WP4).
UENUM(BlueprintType)
enum class EMDIntelligenceTier : uint8
{
	T0 = 0,
	T1,
	T2,
	T3,
	T4
};

// Per-archetype AI numbers. The brain (WP1) reads PreferredRange / ThreatRange; the pack director (WP2) reads AllowedRoles.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDArchetypeAIParams
{
	GENERATED_BODY()

	// Distance (cm) the archetype wants to fight from. Ghoul 180, Bone Archer 900, Cultist 1100.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float PreferredRange = 180.0f;

	// Distance (cm) an enemy without an attack token holds at (v1-design 1.3: approach and threaten, never swing).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float ThreatRange = 450.0f;

	// Roles the pack director may give this archetype.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TArray<EMDRole> AllowedRoles;
};

// Common tier parameters (v1-design 3.2). The numbers come from DT_IntelligenceTiers (FMDIntelligenceTier rows T0..T4) once
// UMDAISubsystem loaded it; until then, and for any missing row, the code defaults (DefaultForTier) apply.
// Distances in cm, angles in degrees, times in seconds.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDTierParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier")
	EMDIntelligenceTier Tier = EMDIntelligenceTier::T0;

	// Enemies never react to a player action faster than this (a delay on perception events reaching the brain).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float ReactionTime = 0.9f;

	// Multiplier on an attack's authored TellTime; the result never drops below TellTimeFloor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float TellTimeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	int32 MaxTokensPerPlayer = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float SightRadius = 1500.0f;

	// Full field of view.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0", ClampMax = "360"))
	float SightFOV = 110.0f;

	// Attack score bias.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float Aggression = 0.6f;

	// Seeded noise added to every intent score.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float NoiseAmplitude = 0.05f;

	// Pack-shared awareness delay; < 0 = no sharing (T0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier")
	float ShareDelay = -1.0f;

	// Tells never get shorter than this (1.3).
	static constexpr float TellTimeFloor = 0.35f;

	// The 3.2 row for a tier: the DT_IntelligenceTiers row when a table is set, else DefaultForTier.
	static FMDTierParams ForTier(EMDIntelligenceTier InTier);

	// The 3.2 numbers in code (the fallback, and what the tests use).
	static FMDTierParams DefaultForTier(EMDIntelligenceTier InTier);

	// Sets the table ForTier reads (UMDAISubsystem does it on initialize). Null = the code defaults. Held weakly.
	static void SetTierTable(const UDataTable* Table);

	// Row name of a tier in DT_IntelligenceTiers: "T0" .. "T4".
	static FName RowNameForTier(EMDIntelligenceTier InTier);

	// The authored tell scaled by the tier, floored.
	float ResolveTellTime(float BaseTellTime) const { return FMath::Max(BaseTellTime * TellTimeMultiplier, TellTimeFloor); }

	// Behaviours unlocked at this tier (3.2). HoldThreat is allowed at every tier: it is the fairness fallback of an enemy
	// without a token, not a tactic. Stagger / Dead are interrupts and always allowed.
	bool IsIntentUnlocked(EMDIntent Intent) const;
};

// One row of DT_IntelligenceTiers (V1 WP4, v1-design 3.2). Row names T0 .. T4. Same numbers as FMDTierParams; the table is
// where designers tune them. Distances in cm, angles in degrees, times in seconds.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDIntelligenceTier : public FTableRowBase
{
	GENERATED_BODY()

	// Enemies never react to a player action faster than this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float ReactionTime = 0.9f;

	// Multiplier on an attack's authored TellTime (1.0 at T0 .. 0.6 at T4); the result never drops below 0.35 s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float TellTimeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	int32 MaxTokensPerPlayer = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float SightRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0", ClampMax = "360"))
	float SightFOV = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float Aggression = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier", meta = (ClampMin = "0"))
	float NoiseAmplitude = 0.05f;

	// < 0 = no sharing (T0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tier")
	float ShareDelay = -1.0f;

	// The row as tier parameters (Tier set to InTier).
	FMDTierParams ToParams(EMDIntelligenceTier InTier) const
	{
		FMDTierParams P;
		P.Tier = InTier;
		P.ReactionTime = ReactionTime;
		P.TellTimeMultiplier = TellTimeMultiplier;
		P.MaxTokensPerPlayer = MaxTokensPerPlayer;
		P.SightRadius = SightRadius;
		P.SightFOV = SightFOV;
		P.Aggression = Aggression;
		P.NoiseAmplitude = NoiseAmplitude;
		P.ShareDelay = ShareDelay;
		return P;
	}
};

// One intent with its utility score (FMDUtilityScorer output).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDScoredIntent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI")
	EMDIntent Intent = EMDIntent::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "AI")
	float Score = 0.0f;
};

// Everything the scorer looks at for one decision (v1-design 2.3 step 2). Plain data, built by the brain each decision.
// Distances in cm. No pointers: the scorer runs in tests with no world.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDBrainContext
{
	GENERATED_BODY()

	// ---- Target ----
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bHasTarget = false;

	// The target is sensed right now (else only its last known position is).
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bTargetSensed = false;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	float DistanceToTarget = 0.0f;

	// ---- Self ----
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bHasToken = false;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bAttackInProgress = false;

	// Attack cooldown elapsed.
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bAttackReady = true;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bStunned = false;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bDead = false;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	float HealthFraction = 1.0f;

	// Longest attack reach of the archetype (an attack may start inside it).
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	float AttackRange = 220.0f;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	float ThreatRange = 450.0f;

	// ---- Pack (WP2) ----
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	EMDRole Role = EMDRole::None;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	EMDPackState PackState = EMDPackState::Engage;

	// The director assigned this enemy a ring slot around its target.
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bHasSlot = false;

	// A rally point exists and the pack is retreating to it.
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bHasRally = false;

	// Within reach of the rally point (Retreat is over, Regroup holds).
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	bool bAtRally = false;

	// ---- Determinism ----
	UPROPERTY(BlueprintReadWrite, Category = "AI")
	int32 RunSeed = 0;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	int32 EnemyId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "AI")
	int32 DecisionIndex = 0;
};

// Readable intent name for debug output.
MAZEDEMONS_API const TCHAR* MDIntentToString(EMDIntent Intent);

// One attack permission handed out by the pack director (V1 WP2). Struct only: nothing issues or checks tokens yet.
struct MAZEDEMONS_API FMDAttackToken
{
	TWeakObjectPtr<AActor> Holder;
	TWeakObjectPtr<AActor> Target;

	// World time (seconds) after which the token is void. 0 = not issued.
	double ExpireTime = 0.0;

	bool IsIssued() const { return ExpireTime > 0.0 && Holder.IsValid(); }
	bool IsValidAt(double Now) const { return IsIssued() && Now < ExpireTime; }
};
