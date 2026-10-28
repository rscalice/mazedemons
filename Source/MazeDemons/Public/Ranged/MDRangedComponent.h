// R0: ranged-mode state of one player. Lives on the PlayerState next to the inventory (D1) so it survives respawn and
// replicates to late joiners (D5). Holds State.RangedMode, the aim flag, and the per-slot magazines (owner-only).
// No firing here: R1 adds the fire / reload abilities on top of this shell.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ranged/MDRangedTypes.h"
#include "MDRangedComponent.generated.h"

class APawn;
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

	// Authority. R1 sets it from the aim input.
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

private:
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
