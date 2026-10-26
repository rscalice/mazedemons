#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "MDCombatTypes.h"
#include "MDCombatNotifies.generated.h"

class UNiagaraSystem;

// Weapon trace window. On the server it calls AC_Combat's trace function (default DoWeaponTrace, which must take no parameters)
// every tick of the window; AC_Combat keeps the per-swing hit list and calls UMDCombatStatics::ApplyMeleeHit for each target.
// Begin / End functions (optional, no parameters) let AC_Combat reset its hit list.
UCLASS(meta = (DisplayName = "MD Weapon Trace"))
class MAZEDEMONS_API UMDANS_WeaponTrace : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	FName BeginFunctionName = NAME_None;

	// True (default): the trace runs fully in C++ (UMDCombatStatics::TraceEquippedWeapons) on the server and the Begin / Tick / End
	// function names below are ignored. False: legacy path, calls the named AC_Combat functions instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	bool bUseCppTrace = true;

	// Which weapon this window traces. Auto (default, so old notifies are unchanged) follows the active combo step and the loadout;
	// Main / Off / Both force it. Use two windows in one montage (one Off, one Main) when a single montage swings both blades in turn.
	// Only used by the C++ trace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	EMDTraceHandOverride TraceHand = EMDTraceHandOverride::Auto;

	// Cosmetic, every non-dedicated machine: also show the combo set's sword trail on the weapons this window swings.
	// Skipped when the montage has an MD Sword Trail window. Respects md.Combat.Trails and the Reduced effects level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	bool bSpawnTrail = false; // trails are authored per montage with Timed Niagara Effect notifies (Robert, 2026-10-24)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	FName TickFunctionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	FName EndFunctionName = NAME_None;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Weapon Trace"); }
};

// Cosmetic sword trail (every non-dedicated machine, never authority-gated). On begin it attaches the active combo set's TrailVFX
// (default NS_Trail_01) to each weapon actor the swing uses, at the blade's midpoint; on end it deactivates the systems so the
// ribbons fade out. The Fab trail systems have no user parameters: the ribbon is drawn by the movement of the attached component.
// Place it next to (same range as) the MD Weapon Trace window. Respects md.Combat.Trails and the Reduced effects level (thinner).
UCLASS(meta = (DisplayName = "MD Sword Trail"))
class MAZEDEMONS_API UMDANS_SwordTrail : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	// Which weapon(s) get a trail. Same meaning as the weapon trace window (Auto follows the active combo step and loadout).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sword Trail")
	EMDTraceHandOverride TraceHand = EMDTraceHandOverride::Auto;

	// Used when the combo set has no TrailVFX. Empty = NS_Trail_01 from the Sword Trail pack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sword Trail")
	TSoftObjectPtr<UNiagaraSystem> FallbackTrail;

	// Rotation of the trail relative to the weapon (tune once if the ribbon faces the wrong way for the sword mesh).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sword Trail")
	FRotator RotationOffset = FRotator::ZeroRotator;

	// Added on top of the trail location (weapon local space, from the blade midpoint).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sword Trail")
	FVector LocationOffset = FVector::ZeroVector;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Sword Trail"); }
};

// Drop-in replacement for the stock Timed Niagara Effect notify that works when the montage plays on a different skeleton than the
// one carrying the hand sockets (BP_Player plays montages on the GASP/UEFN mesh; MainHand / OffHand / TwoHanded exist only on the
// retargeted visible mesh the weapons attach to). It finds the weapon actor attached to the owner at SocketName and attaches the
// system to the weapon's root, so the offsets are relative to the weapon actor (as when previewed with the weapon snapped to the
// socket). With no such weapon (editor montage preview) it falls back to the playing mesh's own socket, like the stock notify.
// Every non-dedicated machine, never authority-gated. Respects md.Combat.Trails.
UCLASS(meta = (DisplayName = "MD Weapon Niagara"))
class MAZEDEMONS_API UMDANS_WeaponNiagara : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	TObjectPtr<UNiagaraSystem> Template = nullptr;

	// Weapon slot socket: MainHand / OffHand / TwoHanded.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	FVector Scale = FVector(1.0);

	// True: destroy the component at the end of the window. False: deactivate it so ribbons fade out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Niagara")
	bool bDestroyAtEnd = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Weapon Niagara"); }
};

// The next light / heavy may be chained from here (buffered input fires inside the window).
UCLASS(meta = (DisplayName = "MD Combo Window"))
class MAZEDEMONS_API UMDANS_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Combo Window"); }
};

// A dodge may cancel the swing from here on.
UCLASS(meta = (DisplayName = "MD Cancel Window"))
class MAZEDEMONS_API UMDANS_CancelWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Cancel Window"); }
};

// The swing ignores stagger (flinch at most) while this state runs. Server only; expires on its own if the end never comes.
UCLASS(meta = (DisplayName = "MD Hyper Armor"))
class MAZEDEMONS_API UMDANS_HyperArmor : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Hyper Armor"); }
};

// Light / heavy input is locked while this state runs. It clears on end or interrupt and expires by duration, so it cannot stick.
UCLASS(meta = (DisplayName = "MD Input Lock"))
class MAZEDEMONS_API UMDANS_InputLock : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Input Lock"); }
};
