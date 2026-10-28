// R0: ranged-mode state of one player. Lives on the PlayerState next to the inventory (D1) so it survives respawn and
// replicates to late joiners (D5). Holds State.RangedMode, the aim flag, and the per-slot magazines (owner-only).
// No firing here: R1 adds the fire / reload abilities on top of this shell.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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

	// Authority. R4 sets it from the aim input.
	void SetAimed(bool bNewAimed);

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

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	bool bAimed = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Ranged")
	TArray<FMDSlotMag> Mags;

	// The newest shot request the server handled (accepted or not). The owner drops its predicted shots up to it.
	UPROPERTY(ReplicatedUsing = OnRep_ShotAck)
	uint16 ShotAck = 0;

	UFUNCTION()
	void OnRep_ShotAck();

private:
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
