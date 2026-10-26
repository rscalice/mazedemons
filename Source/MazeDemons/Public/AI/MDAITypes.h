#pragma once

#include "CoreMinimal.h"
#include "MDAITypes.generated.h"

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

// Intelligence tier T0 (basic) .. T4 (smartest). The tier table struct (FMDIntelligenceTier) arrives with V1 WP4.
UENUM(BlueprintType)
enum class EMDIntelligenceTier : uint8
{
	T0 = 0,
	T1,
	T2,
	T3,
	T4
};

// Per-archetype AI numbers. Placeholder for V1 WP1+: the brain reads PreferredRange and AllowedRoles; tier overrides come later.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDArchetypeAIParams
{
	GENERATED_BODY()

	// Distance (cm) the archetype wants to fight from. Ghoul 180, Bone Archer 900, Cultist 1100.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float PreferredRange = 180.0f;

	// Roles the pack director may give this archetype.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TArray<EMDRole> AllowedRoles;
};

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
