#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LockOn/MDLockOnRules.h"
#include "MDLockOnComponent.generated.h"

class UMDLockOnReticleWidget;
class APlayerController;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDLockTargetChanged, AActor*, NewTarget, AActor*, OldTarget);

// V1 WP7 soft lock-on (v1-design 5.1). Lives on the player controller (AMDPlayerController creates it; input lives there too).
//
// Local controller: scans targets (UMDTargetableComponent, hostile, alive), scores them (FMDLockOnRules), toggles / cycles the lock,
// breaks it (dead / > 35 m / no LOS 2.5 s / menu), auto-retargets after a kill, nudges the camera (never fights input) and drives the
// reticle widget. The chosen target goes to the server with Server_SetLockTarget (validated) so the warp and the AI see it.
// Server: keeps the validated target for that controller; remote players' controllers only ever hold the server copy.
UCLASS(ClassGroup = (Gameplay))
class MAZEDEMONS_API UMDLockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDLockOnComponent();

	// The lock-on of a controller, or of a pawn's controller. Null when there is none.
	static UMDLockOnComponent* FindFor(const AActor* ControllerOrPawn);

	// ---- Getters (the R4 aim assist reads these) ----

	// The locked target: the local lock on the owning machine, the server's validated copy on the server. Null when not locked.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|LockOn")
	AActor* GetLockTarget() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|LockOn")
	bool HasLockTarget() const { return GetLockTarget() != nullptr; }

	// Chest point of the locked target (the aim assist's magnetism point); false when not locked.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|LockOn")
	bool GetLockAimPoint(FVector& OutPoint) const;

	// True when Target is this player's current lock (local or server copy).
	bool IsLockedOn(const AActor* Target) const { return Target && GetLockTarget() == Target; }

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|LockOn")
	FMDLockTargetChanged OnLockTargetChanged;

	// ---- Input entry points (the controller calls them; menus and death are gated there and again here) ----

	// Toggle mode: lock the best target or break the lock. Hold mode: OnLockPressed locks, OnLockReleased breaks.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|LockOn")
	void OnLockPressed();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|LockOn")
	void OnLockReleased();

	// Direction +1 = right, -1 = left (by screen X, wrapping). Does nothing while not locked.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|LockOn")
	void CycleTarget(int32 Direction);

	// Locks the best candidate now (false if none). Break with ClearLock.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|LockOn")
	bool AcquireBestTarget();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|LockOn")
	void ClearLock();

	// The combo component calls this when the local player starts a swing: opens the auto-retarget window and, with the
	// "auto-lock when attacking" option on, locks the nearest good target.
	void NotifyPlayerAttacked();

	// ---- Server ----

	// Client -> server: the target this player locked (null = unlocked). The server drops it unless the target is alive, hostile, within
	// range and seen recently (FMDLockOnRules::ValidateServerTarget). An invalid hint clears the server copy.
	UFUNCTION(Server, Reliable)
	void Server_SetLockTarget(AActor* Target);

	// Authority only (tests / debugging): the server-side check for a target seen from the controller's pawn.
	bool ValidateTargetOnServer(const AActor* Target) const;

	// The server-validated target (authority only; null on clients).
	AActor* GetServerLockTarget() const { return ServerTarget.Get(); }

	// ---- Warp (UMDComboComponent) ----

	// The locked target if it should override C1's cone pick for a step with WarpRange (see FMDLockOnRules::UseLockedTargetForWarp).
	AActor* GetWarpOverrideTarget(bool bHasMoveInput, const FVector& InputDir, float WarpRange) const;

	const FMDLockOnTuning& GetTuning() const { return Tuning; }

	// Reticle widget (created lazily on the local controller; never removed while the controller lives).
	UMDLockOnReticleWidget* GetReticle() const { return Reticle; }

	// Console helpers (md.LockOn.*).
	void DumpState() const;
	static UMDLockOnComponent* GetLocal(const UWorld* World);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	virtual void BeginPlay() override;

private:
	APlayerController* GetPC() const;
	bool IsLocal() const;
	bool IsMenuOpen() const;
	bool IsPlayerDead() const;
	APawn* GetPawn() const;

	// Hostile, targetable, alive pawn (candidate or lock).
	bool IsLockable(const AActor* Actor) const;
	bool HasLineOfSight(const AActor* Target, const FVector& From) const;
	static FVector ChestOf(const AActor* Actor);

	void ScanCandidates();
	void ApplyHighlight(AActor* Target);
	void ClearHighlight();
	void SetLockTarget(AActor* NewTarget, bool bSendToServer);
	void BreakLock(EMDLockBreak Reason);
	void UpdateBreakRules(float DeltaTime);
	void UpdateFlick(float DeltaTime);
	void UpdateCameraAssist(float DeltaTime);
	void UpdateReticle();
	void ServerUpkeep(float DeltaTime);
	float GetAssistStrength() const;
	bool CanProjectTarget(const AActor* Target, FVector2D& OutPixel) const;

	FMDLockOnTuning Tuning;

	// Local state.
	TWeakObjectPtr<AActor> LockTarget;
	float NoLOSSeconds = 0.0f;
	bool bTargetHasLOS = true;
	double LastAttackTime = -1000.0;
	double LastReassertTime = 0.0;
	double ScanTimer = 0.0;
	bool bHoldLocked = false;
	TArray<FMDLockOnCandidate> Candidates;
	TArray<TWeakObjectPtr<AActor>> CandidateActors; // index = FMDLockOnCandidate::Id
	FMDLockOnRules::FFlick Flick;
	struct FMDScanStats { int32 Pawns = 0; int32 NotLockable = 0; int32 OutOfRange = 0; int32 NoLOS = 0; };
	FString LastScanSummary;
	bool bSelfTested = false;
	bool bSelfShot = false;
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> HighlightMeshes;

	// Camera assist bookkeeping: the rotation we left the camera at, and when anything else last moved it.
	FRotator LastAssistedRotation = FRotator::ZeroRotator;
	bool bHaveAssistRotation = false;
	double LastInputTime = -1000.0;

	// Server state.
	TWeakObjectPtr<AActor> ServerTarget;
	mutable double ServerLastLOSTime = -1000.0; // ValidateTargetOnServer refreshes it
	float ServerUpkeepTimer = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMDLockOnReticleWidget> Reticle;

	// Subtle local-only overlay tint on the locked enemy (removed on unlock / death).
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> HighlightMID;
};
