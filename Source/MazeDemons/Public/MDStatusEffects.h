#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayTagContainer.h"
#include "MDStatusTypes.h"
#include "MDStatusEffects.generated.h"

class AActor;

// ---- Numbers (v1-design 3.4; Q11 approved) ---------------------------------------------------------------------------------------
// Bleed   5 ticks x 2% max HP over 5 s, stacks to 3 (damage scales with stacks), a new hit adds a stack and refreshes.
// Frost   -25% move speed, +20% stamina drain, 4 s, refreshes.
// Poison  8 ticks x 1.5% max HP over 8 s, blocks stamina regen, refreshes.
// Burn    3 ticks x 3% max HP over 3 s (reserved; no source yet).
// DoT damage is a fraction of the victim's max health per tick, then goes through the normal Defense reduction.
struct FMDStatusTuning
{
	float Duration = 0.0f;           // seconds from the last application
	int32 Ticks = 0;                 // damage ticks per application (0 = no damage)
	float TickFraction = 0.0f;       // of max health per tick, per stack
	int32 MaxStacks = 1;
	float MoveSpeedScale = 1.0f;     // < 1 slows (Frost)
	float StaminaDrainScale = 1.0f;  // > 1 makes stamina drain faster (Frost)
	bool bBlocksStaminaRegen = false; // Poison

	float TickInterval() const { return Ticks > 0 ? Duration / static_cast<float>(Ticks) : 0.0f; }
};

// Server-only state of one active status. The replicated mirror is FMDStatusRep.
struct FMDStatusRuntime
{
	EMDStatusType Type = EMDStatusType::None;
	int32 Stacks = 1;
	float Remaining = 0.0f;
	float Duration = 0.0f;
	float TickAccum = 0.0f;
	int32 TicksLeft = 0;
	TWeakObjectPtr<AActor> Instigator;
};

// One DoT tick due, produced by FMDStatusRules::Advance.
struct FMDStatusTick
{
	EMDStatusType Type = EMDStatusType::None;
	int32 Stacks = 1;
	float Fraction = 0.0f; // of max health: TickFraction x Stacks
	TWeakObjectPtr<AActor> Instigator;
};

// The status rules as pure functions, so they are unit-tested without a world. UMDStatusEffectComponent is the thin shell around them.
struct MAZEDEMONS_API FMDStatusRules
{
	static const FMDStatusTuning& GetTuning(EMDStatusType Type);

	// Adds the status or stacks it (up to MaxStacks) and refreshes its duration and tick budget. Returns true when this created the entry.
	static bool Apply(TArray<FMDStatusRuntime>& Active, EMDStatusType Type, AActor* Instigator);

	// Moves the clock by DeltaSeconds: collects the ticks that came due (possibly several for a long step), expires finished entries
	// (appending their type to OutExpired when given). A final tick that lands exactly on expiry still fires.
	static void Advance(TArray<FMDStatusRuntime>& Active, float DeltaSeconds, TArray<FMDStatusTick>& OutTicks, TArray<EMDStatusType>* OutExpired = nullptr);

	// Raw damage of one tick (before Defense): max health x fraction x stacks.
	static float TickDamage(float MaxHealth, EMDStatusType Type, int32 Stacks);

	// Combined modifiers of the active set (the lowest speed scale wins, drain scales multiply).
	static float MoveSpeedScale(const TArray<FMDStatusRep>& Active);
	static float StaminaDrainScale(const TArray<FMDStatusRep>& Active);
	static bool BlocksStaminaRegen(const TArray<FMDStatusRep>& Active);

	// Tags: Status.<Name> granted while active, Damage.Type.* of its ticks (Burn is Fire).
	static FGameplayTag GetStatusTag(EMDStatusType Type);
	static FGameplayTag GetDamageType(EMDStatusType Type);
	static FText GetDisplayName(EMDStatusType Type);
	static FLinearColor GetColor(EMDStatusType Type);
};

// ---- Gameplay effects --------------------------------------------------------------------------------------------------------

// The one effect every DoT tick is applied with: instant, runs UMDDoTExecCalc. Carries SetByCaller Data.Damage (raw amount) and
// Data.IsDoT (1). UMDAbilitySystemComponent::ApplyDoTDamage builds it.
UCLASS()
class MAZEDEMONS_API UMDDoTEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMDDoTEffect();
};

// Damage execution for DoT ticks. Unlike the hit path it has no critical hit and no Executioner, and the effect it lives in is not the one
// the armor-wear listeners hear about (ApplyDoTDamage broadcasts OnDoTDamageApplied instead of OnDamageApplied). It refuses to run
// without the Data.IsDoT flag, so a plain damage effect can never be mistaken for a tick. Output goes to IncomingDamage, which applies
// the victim's Defense like any other damage.
UCLASS()
class MAZEDEMONS_API UMDDoTExecCalc : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UMDDoTExecCalc() = default;

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

	// The rule in plain data: damage passes only with the DoT flag set, is never multiplied and never negative.
	static float ComputeDamage(float IsDoTFlag, float RawDamage);
};

// Status effect GEs. Infinite effects that grant the Status.<Name> tag to the target for as long as the status is active; the component
// applies one on the first application and removes it when the status ends (so AI, immunities and other systems can read the tag).
// The timing, stacking and ticking live in FMDStatusRules / UMDStatusEffectComponent, which keeps them testable.
UCLASS(Abstract)
class MAZEDEMONS_API UMDStatusEffectBase : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMDStatusEffectBase();

protected:
	void GrantTag(const FGameplayTag& Tag);
};

UCLASS()
class MAZEDEMONS_API UMDStatusEffect_Bleed : public UMDStatusEffectBase
{
	GENERATED_BODY()

public:
	UMDStatusEffect_Bleed();
};

UCLASS()
class MAZEDEMONS_API UMDStatusEffect_Frost : public UMDStatusEffectBase
{
	GENERATED_BODY()

public:
	UMDStatusEffect_Frost();
};

UCLASS()
class MAZEDEMONS_API UMDStatusEffect_Poison : public UMDStatusEffectBase
{
	GENERATED_BODY()

public:
	UMDStatusEffect_Poison();
};

UCLASS()
class MAZEDEMONS_API UMDStatusEffect_Burn : public UMDStatusEffectBase
{
	GENERATED_BODY()

public:
	UMDStatusEffect_Burn();
};
