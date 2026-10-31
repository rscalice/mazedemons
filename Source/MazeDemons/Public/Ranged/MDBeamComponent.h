// R5: hold-to-fire beams (Duskray laser pistol; the Ember Staff reuses it in V2). Lives on the PlayerState next to UMDRangedComponent.
//
// Server: the fire ability on the owning machine calls OwnerStartBeam, which asks the server (Server_BeamStart). While the beam is on
// the server ticks 10 Hz (AdvanceServer -> ServerBeamTickAt): one sweep from the muzzle along the player's aim, ApplyRangedHit
// (EMDHitKind::Beam) on each victim once per tick, friendly-fire rules as every ranged hit. Heat / charge rules are FMDBeamRules; the
// numbers live on UMDRangedAttributeSet (PlayerState ASC) and replicate to the owner, who predicts them (TickOwnerPrediction) and
// is corrected by each new server sample. Everyone gets bBeamActive, the quantised BeamEnd and bOverheated (State.Overheated tag).
//
// Cosmetics (every machine, never on a dedicated server): the Niagara laser (FreeStylizedLaserBeamVFX) from the muzzle to the end, plus
// the muzzle flash and hit effect while the beam is on, and a steam puff on overheat. The shooter's beam starts at once (prediction);
// other machines follow the replicated end point. A late joiner gets bBeamActive in the initial bunch and sees the beam (D5).
//
// Design deviation (ranged-design section 6): no one-sample rewind. Decision 2026-10-27: no lag compensation; the server traces where
// the target is now.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ranged/MDBeamRules.h"
#include "MDBeamComponent.generated.h"

class APawn;
class UMDRangedComponent;
class UMDRangedAttributeSet;
class UMDRangedWeaponData;
class UNiagaraComponent;
class UNiagaraSystem;
class AMDPlayerState;

UCLASS(ClassGroup = (MazeDemons), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDBeamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDBeamComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Accepts a pawn, controller or PlayerState.
	static UMDBeamComponent* Find(const AActor* Actor);

	// ---- State, readable on every machine ----
	// The server says a beam is firing (replicated to everyone).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	bool IsBeamActive() const { return bBeamActive; }

	// Where the beam ends (whole centimetres, replicated to everyone).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	FVector GetBeamEnd() const { return FVector(BeamEnd); }

	// Locked after overheating: the replicated flag, or on the owning client its own prediction.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	bool IsOverheatedView() const;

	// Heat / charge as this machine knows them: the server's own on the authority, the prediction on the owning client, 0 elsewhere.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	float GetHeatView() const;
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	float GetMaxHeatView() const;
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	float GetChargeView() const;
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	float GetMaxChargeView() const;

	// 0..1 of the overheat lock still to run (1 = just locked). Works on every machine (the others use the replicated end time).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Beam")
	float GetOverheatLockFraction() const;

	// The beam is shown on this machine (server flag or the shooter's own prediction).
	bool IsBeamShown() const { return bBeamActive || bLocalBeamOn; }

	// ---- Owner machine ----
	// Fire pressed / held on a beam weapon: gates, soft lock, prediction, then asks the server. False when something blocks the beam.
	bool OwnerStartBeam();
	void OwnerStopBeam();
	bool IsOwnerBeamOn() const { return bLocalBeamOn; }

	// Why the beam cannot fire now (None = it can). Uses the server's view on the authority, the prediction on the owning client.
	EMDBeamBlock GetBlockReason() const;

	// ---- Server ----
	UFUNCTION(Server, Reliable)
	void Server_BeamStart();

	UFUNCTION(Server, Reliable)
	void Server_BeamStop();

	// Authority: validates (FMDBeamRules::CanFire on the server's state) and turns the beam on. Returns the verdict.
	EMDBeamBlock ServerStartBeam();

	// Authority: turns the beam off (trigger released, overheat, death, stagger, mode or gun change). Flushes the damage numbers.
	void ServerStopBeam();

	// Authority: heat / charge by DeltaSeconds, the 10 Hz damage ticks while the beam is on, the number flush, the attributes. The component
	// tick calls it; tests call it directly (their world does not tick).
	void AdvanceServer(float DeltaSeconds);

	// Authority: one damage tick from Muzzle along Dir (unit). Sweeps up to the weapon's range, hurts each victim at most once, stops at
	// the first victim that took damage or at world geometry, sets BeamEnd. Returns the victims hurt. The 10 Hz timer builds the aim
	// from the shooter and calls this; tests pass their own origin.
	int32 ServerBeamTickAt(const FVector& Muzzle, const FVector& Dir);

	// Authority: sends every pending (rolled up) damage number now.
	void FlushNumbers(bool bAll);

	// ---- Debug / tests ----
	int32 GetServerTickCount() const { return ServerTicks; }
	int32 GetPendingNumberCount() const { return NumberBuckets.Num(); }
	const FMDHeatState& GetServerHeatState() const { return ServerHeat; }
	const FMDChargeState& GetServerChargeState() const { return ServerCharge; }
	float GetWearBanked() const { return WearAccumulator; }
	int32 GetNumbersSentCount() const { return NumbersSent; }
	// Tests: the same effects the replicated properties cause on a client.
	void Test_SetReplicatedBeam(bool bActive, const FVector& End);
	void Test_SetReplicatedOverheat(bool bOverheat, float EndServerTime);
	bool HasBeamFX() const { return BeamFXComp != nullptr; }

protected:
	UFUNCTION()
	void OnRep_BeamActive();

	UFUNCTION()
	void OnRep_Overheated();

	// Replicated to everyone (initial bunch for a late joiner).
	UPROPERTY(ReplicatedUsing = OnRep_BeamActive, BlueprintReadOnly, Category = "Maze Demons|Beam")
	bool bBeamActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Beam")
	FVector_NetQuantize BeamEnd = FVector::ZeroVector;

	// Locked after overheating; every machine adds State.Overheated from it.
	UPROPERTY(ReplicatedUsing = OnRep_Overheated, BlueprintReadOnly, Category = "Maze Demons|Beam")
	bool bOverheated = false;

	// Server clock when the lock ends (for the HUD radial on every machine).
	UPROPERTY(Replicated)
	float OverheatEndServerTime = 0.0f;

private:
	UMDRangedComponent* GetRanged() const;
	UMDRangedWeaponData* GetData() const;
	APawn* GetPawn() const;
	UMDRangedAttributeSet* GetAttributes() const;
	bool IsCurrentBeamWeapon() const;
	bool IsChargeStyle() const;
	double GetServerNow() const;
	FMDHeatParams BuildHeatParams() const;
	FMDChargeParams BuildChargeParams() const;
	int32 GetUpgradeLevel() const;
	EMDBeamBlock ComputeBlock(bool bOverheatedNow, bool bNoCharge) const;

	// Server.
	void SetOverheated(bool bNewOverheated);
	void PushAttributes(bool bForce);
	void ServerBeamTick();
	void BankWear();

	// Owner machine.
	void TickOwnerPrediction(float DeltaSeconds);
	void SyncFromServerSample();

	// Every machine.
	void ApplyOverheatTagLocally();
	void PlayOverheatCue(bool bStarting);
	void UpdateCosmetics(float DeltaSeconds);
	void StartCosmetics();
	void StopCosmetics();
	void WakeTick();
	bool NeedsTick() const;

	// ---- server state ----
	FMDHeatState ServerHeat;
	FMDChargeState ServerCharge;
	float TickAccumulator = 0.0f;
	int32 ServerTicks = 0;
	int32 NumbersSent = 0;
	float WearAccumulator = 0.0f;
	float LastPushedHeat = -100.0f;
	float LastPushedCharge = -100.0f;
	FMDBeamHitLog HitLog;
	TArray<FMDBeamNumberBucket> NumberBuckets;

	// ---- owner (predicted) state ----
	FMDHeatState PredHeat;
	FMDChargeState PredCharge;
	bool bLocalBeamOn = false;
	double LocalBeamStartTime = -1000.0;
	float LockNotifyTimer = 0.0f;
	float LastSeenServerHeat = -1.0f;
	float LastSeenServerCharge = -1.0f;

	// ---- cosmetics ----
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> BeamFXComp;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HitFXComp;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> MuzzleFXComp;

	FVector ShownEnd = FVector::ZeroVector;
	bool bShownEndValid = false;

	TWeakObjectPtr<AMDPlayerState> CachedState;
};
