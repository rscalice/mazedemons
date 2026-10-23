#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "MDCombatNotifies.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	FName TickFunctionName = TEXT("DoWeaponTrace");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Trace")
	FName EndFunctionName = NAME_None;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Weapon Trace"); }
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
