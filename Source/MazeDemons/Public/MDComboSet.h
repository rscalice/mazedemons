#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MDCombatRules.h"
#include "MDCombatTypes.h"
#include "MDComboSet.generated.h"

class UAnimMontage;
class UCameraShakeBase;
class UMDImpactSet;

// Combo data for one weapon type (DA_Combo_Greatsword, DA_Combo_DualSword).
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDComboSet : public UDataAsset
{
	GENERATED_BODY()

public:
	// True for a dual-wield set: steps may use Hand = Off or Both. Used when the same weapon type is in both hands.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	bool bDualWield = false;

	// The light chain (3-5 steps). Pressing light inside a step's combo window plays the next one; after the last it wraps.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	TArray<FMDAttackStep> LightChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	FMDAttackStep Heavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	FMDAttackStep Charged;

	// Optional looping charge montage played while the heavy button is held.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	TObjectPtr<UAnimMontage> ChargeLoopMontage = nullptr;

	// Hold time that turns a heavy into the charged attack.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.1"))
	float ChargeTime = 0.6f;

	// Optional: a light press this soon after a dodge plays this instead of step 1.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	FMDAttackStep DodgeAttack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	float DodgeAttackWindow = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.05"))
	float InputBufferTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.1"))
	float ComboResetTime = 0.8f;

	// Multiplier on every step's hit-stop frames (still clamped to 120 ms).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0"))
	float HitStopScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<UCameraShakeBase> HeavyCameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UMDImpactSet> ImpactSet = nullptr;

	// Slide montages indexed by EMDDodgeDirection (Forward, Backward, Left, Right). Empty = the slide is driven in Blueprint.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge")
	TArray<TObjectPtr<UAnimMontage>> DodgeMontages;

	// Seconds of invulnerability at the start of the dodge slide.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeIFrames = 0.25f;

	FMDComboTuning MakeTuning() const;

	// Kind + index -> step. Index is only used for Light. False when the step does not exist.
	bool GetStep(EMDAttackKind Kind, int32 Index, FMDAttackStep& OutStep) const;

	UAnimMontage* GetDodgeMontage(EMDDodgeDirection Direction) const;

	// Data sanity check used by the ComboSetValidation test. True when there are no errors.
	bool Validate(TArray<FString>& OutErrors) const;
};
