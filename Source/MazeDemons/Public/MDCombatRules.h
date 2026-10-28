#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDCombatTypes.h"
#include "MDCombatRules.generated.h"

// Combo state machine data (plain; owned by UMDComboComponent, tested without a world).
struct FMDComboState
{
	int32 LightStep = -1;                    // last light step played, -1 = none
	bool bAttacking = false;                 // a step is playing
	bool bInComboWindow = false;             // chaining the next step is allowed
	bool bInCancelWindow = false;            // a dodge may cancel the swing
	EMDAttackKind ActiveKind = EMDAttackKind::Light;
	double LastAttackEndTime = -1000.0;
	double LastDodgeTime = -1000.0;
	EMDComboInput Buffered = EMDComboInput::None;
	double BufferedTime = -1000.0;
	float BufferedHold = 0.0f;               // heavy: seconds the button was held

	void Reset()
	{
		*this = FMDComboState();
	}
};

struct FMDComboTuning
{
	int32 LightCount = 3;
	float InputBufferTime = 0.35f;
	float ComboResetTime = 0.8f;
	float ChargeTime = 0.6f;
	float DodgeAttackWindow = 0.6f;
};

enum class EMDComboEvent : uint8
{
	Poll = 0,       // evaluate the buffered input
	AttackEnded,    // the step montage finished
	Interrupted     // stagger / death / forced stop: full reset
};

struct FMDReactionInput
{
	bool bDead = false;
	bool bHyperArmor = false;
	bool bImmune = false;       // inside the post-stun stagger immunity window
	bool bStunned = false;
	bool bFlinchOnly = false;   // bosses
	EMDHitReaction StepReaction = EMDHitReaction::Flinch;
	EMDHitReaction MaxReaction = EMDHitReaction::Knockdown; // R1: per-weapon cap on the final reaction (poise break included)
	float Poise = 50.0f;
	float MaxPoise = 50.0f;
	float PoiseDamage = 0.0f;
};

struct FMDReactionResult
{
	EMDHitReaction Reaction = EMDHitReaction::None;
	float PoiseAfter = 0.0f;
	bool bStartStun = false;
};

struct FMDPoiseTickInput
{
	float Poise = 0.0f;
	float MaxPoise = 50.0f;
	float RegenDelay = 2.0f;
	float RegenPerSec = 25.0f;
	float TimeSinceHit = 0.0f; // at the start of this step
	float DeltaTime = 0.0f;
};

// Pure combat rules (no world, no actors). Components hold state and call these.
UCLASS()
class MAZEDEMONS_API UMDCombatRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Stores Input in the single buffer slot (latest wins). None is ignored. HoldSeconds matters for Heavy.
	static void BufferInput(FMDComboState& State, EMDComboInput Input, double Now, float HoldSeconds = 0.0f);

	// Runs one event. Poll: drops an expired buffer, else tries to act on it:
	//  Dodge: when idle or in the cancel window. Light: when idle or in the combo window; next step (wraps), step 0 after
	//  ComboResetTime, or the dodge attack right after a dodge. Heavy: when idle or in the combo window; Charged when held for
	//  ChargeTime, else Heavy. Not yet allowed = stays buffered until it expires.
	static EMDComboAction Advance(FMDComboState& State, const FMDComboTuning& Tuning, EMDComboEvent Event, double Now);

	// Marks a dodge as started outside the buffer path (the dodge ability). Ends any swing.
	static void ApplyDodge(FMDComboState& State, double Now);

	// Poise and reaction.
	static FMDReactionResult ResolveReaction(const FMDReactionInput& In);
	static float TickPoise(const FMDPoiseTickInput& In);

	// Which side of the victim the attacker is on: Front within 45 degrees of forward, Back beyond 135, else Left / Right.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static EMDHitDirection HitDirection(const FVector& VictimForward, const FVector& VictimLocation, const FVector& AttackerLocation);

	// Push velocity away from the attacker. Horizontal always; enemies (bPlayer false) also get an upward launch of
	// LaunchUpRatio * strength. Players get a flat slide (Z = 0). Resist (0..1) scales the strength down.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static FVector KnockbackVelocity(float Strength, const FVector& AttackerLocation, const FVector& VictimLocation, bool bPlayer, float Resist = 0.0f, float LaunchUpRatio = 0.4f);

	// Critical hits break poise 1.5x faster.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static float CritPoiseBonus(float PoiseDamage, bool bCrit);

	// Hit-stop length in seconds: Frames at 60 fps times Scale, clamped to [0, MaxSeconds] (120 ms by default).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static float HitStopSeconds(int32 Frames, float Scale = 1.0f, float MaxSeconds = 0.12f);

	// Hit-stop frames actually used: the step's frames raised to the per-kind floor (heavy / charged use HeavyMin, others LightMin).
	// A step with 0 frames stays 0 (the step opted out).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static int32 EffectiveHitStopFrames(int32 StepFrames, EMDAttackKind Kind, int32 LightMin, int32 HeavyMin);

	// Hit flash strength over normalised time: 1 at T=0 falling quadratically to 0 at T=1 (0 outside 0..1).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static float HitFlashEnvelope(float NormalizedTime);

	// Dodge direction from the movement input relative to the facing. No input = Backward.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	static EMDDodgeDirection DodgeDirectionFromInput(const FVector& WorldInput, const FVector& Forward);

	static constexpr float CritPoiseMultiplier = 1.5f;
	static constexpr float MaxHitStopSeconds = 0.12f;
};
