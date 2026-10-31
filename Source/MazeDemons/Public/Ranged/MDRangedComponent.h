// R0: ranged-mode state of one player. Lives on the PlayerState next to the inventory (D1) so it survives respawn and
// replicates to late joiners (D5). Holds State.RangedMode, the aim flag, and the per-slot magazines (owner-only).
// No firing here: R1 adds the fire / reload abilities on top of this shell.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDRangedMath.h"
#include "Ranged/MDRangedTypes.h"
#include "MDRangedComponent.generated.h"

class APawn;
class APlayerController;
class UMeshComponent;
class USkeletalMeshComponent;
class UMDInventoryComponent;
class UMDRangedWeaponData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDRangedModeChanged, bool, bRangedMode);

// Rounds currently loaded in the gun equipped in Slot. Owner-only: remote players do not need the other players' ammo.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDSlotMag
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ranged")
	uint8 Slot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ranged")
	int32 Rounds = 0;
};

UCLASS(ClassGroup = (MazeDemons), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDRangedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDRangedComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Accepts a pawn, controller or PlayerState (same lookup order as UMDGameplayStatics::GetInventory).
	static UMDRangedComponent* Find(const AActor* Actor);

	// True when the actor (pawn / controller / PlayerState) is a player currently in ranged mode.
	static bool IsActorInRangedMode(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsRangedMode() const { return bRangedMode; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsAimed() const { return bAimed; }

	// Authority. Sets the replicated aim flag (only while in ranged mode), the loose State.Aiming tag and wakes the tick. Does not run the
	// FMDAimRules gates: the aim request paths (RequestAim / Server_SetAim) and EvaluateAimRules do.
	void SetAimed(bool bNewAimed);

	// ================= R4 aim =================
	// What the local view should use: the replicated flag, or on the owning client the predicted one (so the camera and spread react
	// before the server's answer arrives). Other machines see the replicated flag only.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsAimingLocal() const { return bAimed || (bAimPredicted && IsOwnerLocal()); }

	// The aim rules (FMDAimRules::CanAim) for the pawn right now, with the sprint state given (tests pass it; the tick measures it).
	bool CanAimNow(bool bSprinting) const;

	// True when the pawn moves fast enough to count as sprinting (UMDStaminaComponent's rule), false without a stamina component.
	bool IsPawnSprintingNow() const;

	// Owner machine: RMB / left trigger. bPressed true/false; with bToggle (UMDLocalSettings::bAimToggle) only presses count and they flip.
	// Keeps the wish (bAimWanted) so a hold-aim resumes after a sprint and a stun.
	void SetAimInput(bool bPressed, bool bToggle);
	bool IsAimWanted() const { return bAimWanted; }

	// Owner machine: activates the LocalPredicted UMDAimAbility when the ASC has it, else asks directly.
	bool TryActivateAim();

	// Owner machine (and the ability): aim on / off. The host sets the flag; a client predicts it and sends Server_SetAim.
	void RequestAim(bool bAim);

	// Server: validates (FMDAimRules::CanAim with the server's own sprint / stagger / death view) and sets the replicated flag. Aiming
	// is refused, never trusted: the flag other clients render is the server's.
	UFUNCTION(Server, Reliable)
	void Server_SetAim(bool bAim);

	// Authority: the validation Server_SetAim runs (tests call it directly). Returns the aim flag afterwards.
	bool ProcessAimRequest(bool bAim);

	// Runs the aim rules once with the given sprint state: the authority drops the aim when CanAim turned false (sprint, stagger,
	// death, holster); the owner machine updates its wish and prediction. The tick calls it with the measured sprint state; tests call it
	// directly.
	void EvaluateAimRules(bool bSprinting);

	// ---- Anim BP hooks (every machine; replicated inputs only) ----
	// Seconds since a shot of this player was seen on this machine (huge before the first).
	float GetSecondsSinceShot() const;

	// Aim offset layer inputs: (yaw, pitch) of the aim direction relative to the pawn, clamped to +-90 / +-60.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	FVector2D GetAimOffset() const;

	// 1 while aimed, or for 1.5 s after a shot in ranged mode; 0 otherwise. Blend the AimOffset layer by it.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetAimOffsetAlpha() const;

	// True when the character should face the camera (aimed strafe locomotion): aimed, or just shot. Anim BP / movement hook.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool WantsAimedStrafe() const { return bRangedMode && (bAimed || (bAimPredicted && IsOwnerLocal())); }

	// Data asset of the gun equipped in the Ranged slot, or null when none (or the row has no RangedData).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	UMDRangedWeaponData* GetEquippedWeaponData() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool HasGunEquipped() const { return GetEquippedWeaponData() != nullptr; }

	// Stance of the held gun (Unarmed when none).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	EMDWeaponStance GetRangedStance() const;

	// The stance the anim BP reads: the gun's stance in ranged mode, else MeleeStance.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	EMDWeaponStance ResolveActiveStance(EMDWeaponStance MeleeStance) const;

	// Authority: Q / D-pad Up. Validates (dead, mid-swing, stagger, cooldown, needs a gun to enter) and flips the mode.
	// Returns true when the mode changed.
	bool TrySwapMode();

	// Authority: forced mode change (equip rules, tests). Updates the replicated flag, the loose tag and local visuals.
	void SetRangedMode(bool bNewMode);

	// Rounds loaded for a ranged slot (0 when never loaded).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	int32 GetMagRounds(uint8 Slot) const;

	// Authority.
	void SetMagRounds(uint8 Slot, int32 Rounds);

	const TArray<FMDSlotMag>& GetMags() const { return Mags; }

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Ranged")
	FMDRangedModeChanged OnRangedModeChanged;

	// Minimum seconds between two accepted swaps.
	UPROPERTY(EditDefaultsOnly, Category = "Maze Demons|Ranged", meta = (ClampMin = "0"))
	float SwapCooldown = 0.4f;

	// ---- Held gun visual + support-hand IK (local, every machine) ----
	// The skeletal mesh that is actually seen (the one running ABP_GenericRetarget when present, else the character mesh).
	static USkeletalMeshComponent* FindVisibleMesh(const APawn* Pawn);

	// Creates / updates / destroys the local gun mesh so it matches the equipped gun and the pawn. Cheap when nothing changed.
	void RefreshWeaponMesh();

	UMeshComponent* GetWeaponMesh() const { return WeaponMeshComp; }

	// World transform the support (left) hand should reach: the weapon's Grip_L socket, else SupportHandOffset on the weapon.
	bool GetSupportGripWorldTransform(FTransform& OutTransform) const;

	// Support-hand IK weight now (see FMDRangedRules::SupportHandIKAlpha). DisableCurve = the DisableLHandIK anim curve.
	float GetSupportHandAlpha(float DisableCurve) const;

#if !UE_BUILD_SHIPPING
	// Debug (md.Ranged.Tune / TuneSupport): live overrides of the data asset offsets, to find grip numbers in a running game.
	// HandOffset = the weapon mesh transform relative to the hand socket; SupportOffset = the support grip in weapon-mesh space.
	// Dropped again when the gun mesh is rebuilt.
	void DebugSetHandOffset(const FTransform& HandOffset, FName Socket = NAME_None);
	void DebugSetSupportOffset(const FTransform& SupportOffset);
#endif

	// Recomputes "gun equipped / melee equipped" after the loadout changed (bound to the inventory's OnEquipmentChanged).
	UFUNCTION()
	void HandleEquipmentChanged();

	// ================= R1 fire pipeline =================
	// True on the machine of the player this component belongs to (listen host for player 0, the owning client otherwise).
	bool IsOwnerLocal() const;

	// ---- Input (owner machine; AMDPlayerController forwards LMB / right trigger) ----
	void SetFireHeld(bool bHeld);
	bool IsFireHeld() const { return bFireHeld; }

	// Activates the LocalPredicted UMDRangedFireAbility when the ASC has it, else fires directly (tests, early frames).
	bool TryActivateFire();

	// Owner machine: one shot attempt. Gates (ranged mode, local cadence, predicted mag), aim from the camera, spread roll, request
	// ids, cosmetics (muzzle flash, recoil, weapon kick, twin bolt, predicted mag) and then Server_Fire (or ProcessShot on the host).
	// Returns true when a request was made.
	bool FireShotLocal();

	// Magazine as the owner sees it: replicated rounds minus shots still in flight to the server. The server's own value on the host.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	int32 GetViewMagRounds() const;

	// Spread half-angle now (degrees): hip / aimed base, moving multiplier, bloom. bServer = the server's own bloom estimate.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetCurrentSpread(bool bServer = false) const;

	// Bloom (degrees) after lazy decay.
	float GetBloomNow(bool bServer) const;

	// Muzzle in world space (mesh socket, else the data's MuzzleLocal on the weapon mesh). False without a weapon mesh.
	bool GetMuzzleWorld(FVector& OutMuzzle) const;

	// ---- Server ----
	UFUNCTION(Server, Reliable)
	void Server_Fire(const FMDShotRequest& Request);

	// Server: validates and, when accepted, consumes a round and spawns the projectile (fast-forwarded by min(RTT / 2, 100 ms)).
	// Every request is acknowledged. A rejected shot spawns nothing and resyncs the owner's magazine (Client_CorrectMag).
	// RTTOverrideSeconds < 0 = the owner's real ping.
	EMDShotReject ProcessShot(const FMDShotRequest& Request, float RTTOverrideSeconds = -1.0f);

	// Owner machine: the server refused shot ShotId; Rounds is the real magazine.
	UFUNCTION(Client, Reliable)
	void Client_CorrectMag(uint8 Slot, int32 Rounds, uint16 ShotId);

	// Everyone but the shooter: muzzle flash at the shot origin (cosmetic, unreliable).
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShotFX(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir);

	// Server: a gun projectile of this player landed a hit. Guns wear WearPerHit (0.5) per landed hit; whole points go to the inventory.
	void NotifyShotLanded();

	// Server clock estimate (listen host: the real one).
	double GetServerNow() const;

	// Last request id the server acknowledged (owner-only replicated).
	uint16 GetShotAck() const { return ShotAck; }

	int32 GetPendingShotCount() const { return PendingShotIds.Num(); }
	EMDShotReject GetLastReject() const { return LastReject; }
	int32 GetAcceptedShotCount() const { return AcceptedShots; }
	float GetPendingRecoilPitch() const { return RecoilPitch; }
	float GetKickAlpha() const { return KickAlpha; }
	int32 GetMagCorrectionCount() const { return MagCorrections; }
	int32 GetLastCorrectedRounds() const { return LastCorrectedRounds; }

	// ================= R3 reload, reserves, pity =================
	// Reload state replicates to everyone (tag + visuals); the progress numbers are readable on the owner's HUD.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsReloading() const { return bReloading; }

	// 0..1 while reloading (server clock), else 0.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetReloadProgress() const;

	// Rounds of the equipped gun's ammo in the bag (0 without a gun, an ammo row or an inventory).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	int32 GetReserveAmmo() const;

	// Magazine size of the equipped gun (0 without a gun).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	int32 GetMagSize() const;

	// Auto-reload when the last round leaves the magazine, and when the trigger is pulled on an empty one. Default on.
	// md.Ranged.AutoReload 0 turns it off for everybody (dev / options hook).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsAutoReloadEnabled() const;

	// ================= R7 HUD bindings =================
	// Beam weapons (R5): the HUD heat / charge meter reads these. STUBS until R5: every getter returns 0 / false, so the meter stays
	// empty. R5 only has to return the real attribute values (Heat / MaxHeat on the player ASC, Charge / MaxCharge for staves).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetHeat() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetMaxHeat() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetCharge() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	float GetMaxCharge() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsOverheated() const;

	// The equipped gun is a beam weapon (data driven; true for the Duskray / Ember rows once R5 gives them a Beam fire mode).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsBeamWeapon() const;

	// A beam weapon that drains a charge (staff) rather than building heat.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Ranged")
	bool IsChargeStyleBeam() const;

	// Owner -> server: the player's auto-reload option (the server decides when to reload). Replicated so everyone agrees.
	UFUNCTION(Server, Reliable)
	void Server_SetAutoReload(bool bAuto);

	// Owner machine: R / D-pad Down. Activates the LocalPredicted UMDReloadAbility when the ASC has it, else asks the server directly.
	bool TryActivateReload();

	// Any machine: authority starts the reload, a client sends Server_Reload.
	void RequestReload();

	// Authority: validates (FMDAmmoRules::CanReload) and starts the reload timer (the data's ReloadSeconds). Returns the verdict.
	EMDReloadReject BeginReloadServer();

	// The verdict BeginReloadServer would give right now (authority state; the owner's replicated view elsewhere).
	EMDReloadReject GetReloadReject() const;

	// Authority: the reload notify. Moves min(MagSize - Mag, Reserve) from the bag into the magazine and ends the reload.
	// Returns the rounds moved. Called by the timer; tests call it directly (their world does not tick).
	int32 CompleteReload();

	// Authority: aborts a reload in progress. Nothing was moved yet, so no ammo is lost. Called on dodge, stagger, death, a mode
	// or gun change.
	void CancelReload();

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	// Pity: shots fired since this player's last ammo drop (server).
	int32 GetShotsSinceAmmoDrop() const { return ShotsSinceAmmoDrop; }
	void ResetShotsSinceAmmoDrop() { ShotsSinceAmmoDrop = 0; }

	// Rounds remembered for a gun that is not equipped (server). 0 when the gun was never seen.
	int32 GetStashedMag(FName GunRow) const;

	// Tests: a pawn to act for when the PlayerState has none (no controller possesses it in a unit-test world).
	void SetPawnForTests(APawn* InPawn) { TestPawn = InPawn; }
	// Tests: a weapon data asset used instead of the inventory's equipped gun.
	void SetWeaponDataForTests(UMDRangedWeaponData* InData) { TestData = InData; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UFUNCTION()
	void OnRep_RangedMode();

	// Replicated to everyone: remote pawns need it for their stance and for the IK.
	UPROPERTY(ReplicatedUsing = OnRep_RangedMode, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	bool bRangedMode = false;

	// Replicated to everyone: remote pawns need it for the aimed pose, the support-hand IK and the State.Aiming tag.
	UPROPERTY(ReplicatedUsing = OnRep_Aimed, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	bool bAimed = false;

	UFUNCTION()
	void OnRep_Aimed();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	TArray<FMDSlotMag> Mags;

	// The newest shot request the server handled (accepted or not). The owner drops its predicted shots up to it.
	UPROPERTY(ReplicatedUsing = OnRep_ShotAck)
	uint16 ShotAck = 0;

	UFUNCTION()
	void OnRep_ShotAck();

	// R3: a reload is running (server decides). Everyone gets the State.Reloading tag from it.
	UPROPERTY(ReplicatedUsing = OnRep_Reloading, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	bool bReloading = false;

	// Server clock at the start of the reload and its length, for the owner's progress bar.
	UPROPERTY(Replicated)
	float ReloadStartServerTime = 0.0f;

	UPROPERTY(Replicated)
	float ReloadDuration = 0.0f;

	UFUNCTION()
	void OnRep_Reloading();

	// The owner's auto-reload option (default on). IsAutoReloadEnabled() = cvar && this.
	UPROPERTY(Replicated)
	bool bAutoReloadPref = true;

private:
	void ApplyAimTagLocally();
	// Owner machine: the player wants to aim (button held, or toggled on). Survives sprints and stuns in hold mode.
	bool bAimWanted = false;
	bool bAimToggleMode = false;
	// Owner machine: the client's guess that the server will accept the aim; dropped when the server says no or the rules turn false.
	bool bAimPredicted = false;
	double AimPredictedAt = 0.0;
	// World time (this machine) of the last shot seen from this player.
	double LastShotSeenTime = -1000.0;

	void SyncAutoReloadPref();
	void ApplyReloadTagLocally();
	void ClearReloadState();

	FTimerHandle ReloadTimer;
	double NextReloadRequestTime = 0.0;
	int32 ShotsSinceAmmoDrop = 0;
	TMap<FName, int32> StashedMags;

	// ---- R1 helpers ----
	bool ComputeAimLocal(FVector& OutMuzzle, FVector& OutAimDir, float& OutAimDistance) const;
	bool ComputeAimServer(const FVector& ClaimedOrigin, FVector& OutAimDir, float& OutAimDistance) const;
	APawn* GetPawn() const;
	APlayerController* GetOwnerController() const;
	void TrimPendingShots();
	void PlayShotCosmetics(const FVector& Origin, const FVector& Dir);
	void AddRecoil(const UMDRangedWeaponData& Data, int32 Seed);
	void ApplyWeaponKick();
	void UpdateReticle();
	void FinishShotServer(const FMDShotRequest& Request, EMDShotReject Reason);

	// Local (owner machine) state.
	bool bFireHeld = false;
	bool bSemiPending = false;
	double SemiBufferUntil = 0.0;
	double NextLocalShotTime = 0.0;
	uint16 NextShotId = 0;
	TArray<uint16> PendingShotIds;
	float LocalBloom = 0.0f;
	double LocalBloomTime = 0.0;
	float RecoilPitch = 0.0f; // unrecovered camera kick (degrees), subtracted again over RecoilRecovery seconds
	float RecoilYaw = 0.0f;
	float KickAlpha = 0.0f;   // weapon kick 0..1
	FTransform WeaponBaseRelative = FTransform::Identity;
	bool bKickApplied = false;

	// Server state.
	float ServerBloom = 0.0f;
	double ServerBloomTime = 0.0;
	double LastAcceptedClientTime = -1000.0;
	int32 AcceptedShots = 0;
	EMDShotReject LastReject = EMDShotReject::None;
	float WearAccumulator = 0.0f;
	int32 MagCorrections = 0;
	int32 LastCorrectedRounds = -1;
	TWeakObjectPtr<APawn> TestPawn;

	UPROPERTY(Transient)
	TObjectPtr<UMDRangedWeaponData> TestData;

	UPROPERTY(Transient)
	TObjectPtr<class UMDRangedReticleWidget> Reticle;

	UMDInventoryComponent* GetInventory() const;

	// Applies the loose tag on the ASC and shows / hides the melee weapon actors (local visuals, every machine).
	void ApplyModeLocally();
	void SetMeleeWeaponsHidden(bool bHidden);

	TWeakObjectPtr<UMDInventoryComponent> BoundInventory;

	// Local gun visual (never replicated; every machine builds it from the replicated Equipped list).
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> WeaponMeshComp;

	FName WeaponMeshRow = NAME_None;

#if !UE_BUILD_SHIPPING
	TOptional<FTransform> DebugSupportOffset;
#endif

	// Row of the gun seen at the last loadout change (server): a new row means "a gun was just equipped".
	FName LastGunRow = NAME_None;

	float LastSwapTime = -1000.0f;
};
