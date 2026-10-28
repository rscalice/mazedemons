#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/MDEnemyArchetype.h"
#include "MDCombatTypes.h"
#include "MDEnemyAttackComponent.generated.h"

class UAnimMontage;
class USkeletalMeshComponent;

// A blade-like segment swept by a weaponless attacker (a claw): Top = tip, Bottom = base, in world space.
struct FMDTraceSegment
{
	FName Id;
	FVector Top = FVector::ZeroVector;
	FVector Bottom = FVector::ZeroVector;
};

// Server: the attack ended (the montage finished or was interrupted by a stagger / cancel).
DECLARE_MULTICAST_DELEGATE_OneParam(FMDEnemyAttackEndedNative, bool /*bInterrupted*/);

// Plays one FMDEnemyAttack of the owner's archetype (v1-design 2.3 "Attack execution"), the C1 way for enemies:
//   - the server plays the montage and multicasts it (clients play the same montage at the same wind-up rate);
//   - the server alone moves the attacker (MDEnemyAttackRules::OwnsAttackMovement): it faces the target and motion-warps
//     toward it ("AttackTarget", within the step's WarpRange / WarpStopDistance), exactly as the player's combo steps do,
//     so a target that slides out of reach is missed honestly; the capsule replicates through the movement component.
//     A simulated proxy plays the montage in place (UMDEnemyAnimInstance puts it in IgnoreRootMotion) and never warps,
//     so it cannot fight the replicated movement with its own root motion;
//   - the wind-up is readable: the montage runs at WindupRate until its first MD Weapon Trace window so that window starts
//     TellTime after the swing began (tier-scaled by the brain); the trace notify restores rate 1 on every machine;
//   - the trace window sweeps the archetype's claw sockets through UMDCombatStatics (hit once per window, debug draw,
//     ApplyMeleeHit with this attack's step: damage, poise, knockback, feedback on the victim's machine).
// Server-only state; clients only play what they are told.
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDEnemyAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDEnemyAttackComponent();

	// Server. Starts Attacks[AttackIndex] of the owner's archetype at Target with the given (already tier-scaled) tell. False
	// when an attack is in progress, the index is bad, the montage is missing or there is no mesh to play it on.
	bool StartAttack(int32 AttackIndex, AActor* Target, float TellTime);

	// Server. Stops the attack montage (the end event fires with bInterrupted = true). No-op when idle.
	void CancelAttack();

	UFUNCTION(BlueprintPure, Category = "Enemy|Attack")
	bool IsAttacking() const { return bAttacking; }

	int32 GetActiveAttackIndex() const { return bAttacking ? ActiveAttackIndex : INDEX_NONE; }

	// The attack playing now (null when idle).
	const FMDEnemyAttack* GetActiveAttack() const;

	// The hit numbers of the attack playing now (ApplyMeleeHit reads these when the attacker has no combo component).
	bool GetActiveStep(FMDAttackStep& OutStep) const;

	// Claw segments of the active attack from its TraceSockets (world space, from the owner's mesh). Empty when idle or when
	// the attack swings weapon actors instead.
	void GetBodyTraceSegments(TArray<FMDTraceSegment>& OutSegments) const;

	// Every machine: the first trace window of the attack montage started, the wind-up is over -> play rate 1.
	void NotifyTraceWindowBegan();
	static void NotifyTraceWindowBegan(const AActor* Attacker);

	// Server world time the last attack ended (-1000 = never).
	double GetLastAttackEndTime() const { return LastAttackEndTime; }

	FMDEnemyAttackEndedNative OnAttackEnded;

	// Trigger time (s) of the first MD Weapon Trace window in Montage, or -1 when it has none.
	static float FindFirstTraceWindowStart(const UAnimMontage* Montage);

	static UMDEnemyAttackComponent* FindFor(const AActor* Actor);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttack(uint8 AttackIndex, float WindupRate, AActor* Target);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopAttack();

	// Local playback on this machine: montage at WindupRate on every machine; face + warp on the authority only.
	void PlayLocal(const FMDEnemyAttack& Attack, float WindupRate, AActor* Target);
	void ApplyWarp(const FMDEnemyAttack& Attack, AActor* Target);
	void ClearWarp();

	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void FinishAttack(bool bInterrupted);

	const UMDEnemyArchetype* GetArchetype() const;
	USkeletalMeshComponent* GetMesh() const;

	bool bAttacking = false;
	int32 ActiveAttackIndex = INDEX_NONE;
	TWeakObjectPtr<UAnimMontage> ActiveMontage;
	TWeakObjectPtr<AActor> ActiveTarget;
	bool bWarpApplied = false;
	bool bRateRestored = false;
	double LastAttackEndTime = -1000.0;
};
