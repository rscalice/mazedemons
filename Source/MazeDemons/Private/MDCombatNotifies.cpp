#include "MDCombatNotifies.h"

#include "MDComboComponent.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace MDCombatNotifyLocal
{
	// Calls a parameterless Blueprint function on AC_Combat. Anything with parameters or a return value is refused.
	void CallCombatFunction(const AActor* Owner, FName FunctionName)
	{
		if (FunctionName == NAME_None)
		{
			return;
		}
		UActorComponent* Combat = UMDGameplayStatics::FindCombatComponent(Owner);
		UFunction* Function = Combat ? Combat->FindFunction(FunctionName) : nullptr;
		if (Function && Function->NumParms == 0)
		{
			Combat->ProcessEvent(Function, nullptr);
		}
	}
}

void UMDANS_WeaponTrace::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (Owner && Owner->HasAuthority())
	{
		MDCombatNotifyLocal::CallCombatFunction(Owner, BeginFunctionName);
	}
}

void UMDANS_WeaponTrace::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (Owner && Owner->HasAuthority())
	{
		MDCombatNotifyLocal::CallCombatFunction(Owner, TickFunctionName);
	}
}

void UMDANS_WeaponTrace::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (Owner && Owner->HasAuthority())
	{
		MDCombatNotifyLocal::CallCombatFunction(Owner, EndFunctionName);
	}
}

void UMDANS_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetComboWindow(true);
	}
}

void UMDANS_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetComboWindow(false);
	}
}

void UMDANS_CancelWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetCancelWindow(true);
	}
}

void UMDANS_CancelWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetCancelWindow(false);
	}
}

void UMDANS_HyperArmor::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Owner))
	{
		if (Owner->HasAuthority())
		{
			Poise->SetHyperArmor(TotalDuration + 0.1f); // safety expiry if NotifyEnd never arrives
		}
	}
}

void UMDANS_HyperArmor::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Owner))
	{
		if (Owner->HasAuthority())
		{
			Poise->SetHyperArmor(0.0f);
		}
	}
}

void UMDANS_InputLock::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetInputLocked(true, TotalDuration);
	}
}

void UMDANS_InputLock::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetInputLocked(false);
	}
}
