#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDCombatRules.h"
#include "MDCombatTypes.h"
#include "MDComboComponent.generated.h"

class UAnimMontage;
class UMDComboSet;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMDComboStepSignature, const FMDAttackStep&, Step, EMDAttackKind, Kind, int32, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDComboSimpleSignature);

// The combo driver on the pawn (owning client and server). It receives input and the window notifies, runs the pure state machine
// (UMDCombatRules::Advance), plays the step montage and tells Blueprint through OnStepStarted. It never decides hits: the
// server's weapon trace goes through UMDCombatStatics::ApplyMeleeHit, which reads GetActiveStep from here.
//
// Networking: the owning client predicts (plays the step at once) and sends the input to the server, which repeats the same step
// logic, plays it and multicasts the step to the other machines. Hit-stop and shake are separate (UMDCombatFeedbackComponent).
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDComboComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDComboComponent();

	// The current weapon's combo data. Swap it when the weapon changes. Null = the component does nothing (old DoLightAttack path).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	TObjectPtr<UMDComboSet> ComboSet;

	// True: this component plays the step montage itself on every machine. False: Blueprint plays it from OnStepStarted.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	bool bPlayMontagesDirectly = true;

	// Used when a step has no montage: how long the swing counts as playing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo", meta = (ClampMin = "0.05"))
	float FallbackStepDuration = 0.5f;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void SetComboSet(UMDComboSet* NewSet);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool HasComboSet() const { return ComboSet != nullptr; }

	// ---- Input (owning client or server). RequestInput sends the input to the server when called on a client. ----
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void RequestInput(EMDComboInput Input, float HeldSeconds = 0.0f);

	// Local processing only (no RPC): used by GA_PlayerMelee on the owning client and by the server RPCs. Gated.
	void ProcessInput(EMDComboInput Input, float HeldSeconds);

	// Heavy button: press starts the charge, release plays heavy (tap) or charged (held for ChargeTime). Sends both to the server.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void HeavyPressed();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void HeavyReleased();

	// ---- Window notifies (from the UMDANS_* notify states, on whichever machine plays the montage) ----
	void SetComboWindow(bool bOpen);
	void SetCancelWindow(bool bOpen);

	// Locks light / heavy input until the notify ends or Duration (+ a small margin) passes, whichever is first: it cannot stick.
	void SetInputLocked(bool bLocked, float Duration = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsInputLocked() const;

	// Stagger / stun / death: resets the combo, drops buffered input, clears locks.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void NotifyInterrupted();

	// ---- Dodge (called by UMDDodgeAbility) ----
	// A dodge may start when no swing is playing or the swing is in its cancel window.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool CanDodgeNow() const;

	// Ends the swing, plays the slide montage from the set if there is one, and tells the other machines.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void BeginDodge(EMDDodgeDirection Direction, float SlideDuration);

	// ---- Queries / swing bookkeeping (server side hit pipeline) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsAttacking() const { return State.bAttacking; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsInComboWindow() const { return State.bInComboWindow; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsInCancelWindow() const { return State.bInCancelWindow; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	int32 GetLightStep() const { return State.LightStep; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	EMDAttackKind GetActiveKind() const { return ActiveKind; }

	// The swing in progress (or just finished within a short grace, so a late trace still counts). False when idle.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool GetActiveStep(FMDAttackStep& OutStep) const;

	// Which hand(s) the swing in progress uses (Main when idle). AC_Combat traces these.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	EMDWeaponHand GetActiveStepHand() const;

	// Counts a hit on Target for the current swing. False when the step's MaxHitsPerTarget is already used up.
	bool TryRegisterSwingHit(const AActor* Target);

	// One-line debug summary for md.Combat.Debug.
	FString GetDebugString() const;

	// ---- Events (every machine that plays the step) ----
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboStepSignature OnStepStarted;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboSimpleSignature OnStepEnded;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboSimpleSignature OnComboReset;

	// The state machine produced a dodge (RequestInput(Dodge)); the dodge ability is the normal way to dodge.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboSimpleSignature OnDodgeRequested;

	// Heavy button went down / up (play and stop the charge loop in Blueprint when bPlayMontagesDirectly is off).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboSimpleSignature OnChargeBegan;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDComboSimpleSignature OnChargeEnded;

	static UMDComboComponent* FindFor(const AActor* Actor);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION(Server, Reliable)
	void Server_RequestInput(EMDComboInput Input, float HeldSeconds);

	UFUNCTION(Server, Reliable)
	void Server_HeavyPressed();

	UFUNCTION(Server, Reliable)
	void Server_HeavyReleased(float HeldSeconds);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayStep(EMDAttackKind Kind, int32 Index);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Dodge(EMDDodgeDirection Direction, float SlideDuration);

	bool IsOwnerLocal() const;
	bool IsAuthority() const;
	// This machine runs the state machine: the server, or the owning client (prediction).
	bool RunsStateMachine() const;
	bool IsGateOpen(EMDComboInput Input) const;
	double Now() const;

	void Execute(EMDComboAction Action);
	void StartStep(EMDAttackKind Kind, int32 Index);
	void PlayDodgeCosmetics(EMDDodgeDirection Direction, float SlideDuration);
	void FinishStep();
	void UpdateFallbackWindows();
	USkeletalMeshComponent* GetMesh() const;

	FMDComboState State;
	EMDAttackKind ActiveKind = EMDAttackKind::Light;
	int32 ActiveIndex = 0;
	FMDAttackStep ActiveStep;
	bool bHasActiveStep = false;
	bool bNotifyWindows = false; // a window notify state ran this step: it wins over the data fractions
	double StepStartTime = 0.0;
	double StepFallbackEnd = 0.0;
	double StepEndedTime = -1000.0;
	double InputLockUntil = 0.0;
	TMap<TWeakObjectPtr<const AActor>, int32> SwingHits;
	TWeakObjectPtr<UAnimMontage> CurrentMontage;
	TWeakObjectPtr<UAnimMontage> ChargeMontage;

	// Heavy hold measurement.
	bool bHeavyDown = false;
	double HeavyPressTime = 0.0;       // local
	double ServerHeavyPressTime = -1.0; // server's own measure
};
