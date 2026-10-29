#include "AI/MDEnemyAttackComponent.h"

#include "AI/MDAIRules.h"
#include "AI/MDEnemyCharacter.h"
#include "MDCombatNotifies.h"
#include "MDWarpTargeting.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"

// "log LogMDEnemyAttack Verbose" prints each machine's playback (role, rate, warp) for 2P checks.
DEFINE_LOG_CATEGORY_STATIC(LogMDEnemyAttack, Log, All);

namespace MDEnemyAttackLocal
{
	const FName WarpTargetName(TEXT("AttackTarget"));

	const TCHAR* RoleName(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Authority");
		case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
		case ROLE_SimulatedProxy: return TEXT("SimulatedProxy");
		default: return TEXT("None");
		}
	}
}

UMDEnemyAttackComponent::UMDEnemyAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // for the multicasts only; no replicated state
}

UMDEnemyAttackComponent* UMDEnemyAttackComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDEnemyAttackComponent>() : nullptr;
}

const UMDEnemyArchetype* UMDEnemyAttackComponent::GetArchetype() const
{
	const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(GetOwner());
	return Enemy ? Enemy->Archetype.Get() : nullptr;
}

USkeletalMeshComponent* UMDEnemyAttackComponent::GetMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

const FMDEnemyAttack* UMDEnemyAttackComponent::GetActiveAttack() const
{
	const UMDEnemyArchetype* Archetype = GetArchetype();
	if (!bAttacking || !Archetype || !Archetype->Attacks.IsValidIndex(ActiveAttackIndex))
	{
		return nullptr;
	}
	return &Archetype->Attacks[ActiveAttackIndex];
}

bool UMDEnemyAttackComponent::GetActiveStep(FMDAttackStep& OutStep) const
{
	if (const FMDEnemyAttack* Attack = GetActiveAttack())
	{
		OutStep = Attack->Step;
		OutStep.Montage = Attack->Montage;
		return true;
	}
	return false;
}

float UMDEnemyAttackComponent::FindFirstTraceWindowStart(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return -1.0f;
	}
	float First = -1.0f;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UMDANS_WeaponTrace>())
		{
			const float Time = Event.GetTriggerTime();
			if (First < 0.0f || Time < First)
			{
				First = Time;
			}
		}
	}
	return First;
}

bool UMDEnemyAttackComponent::StartAttack(int32 AttackIndex, AActor* Target, float TellTime)
{
	AActor* Owner = GetOwner();
	const UMDEnemyArchetype* Archetype = GetArchetype();
	if (!Owner || !Owner->HasAuthority() || bAttacking || !Archetype || !Archetype->Attacks.IsValidIndex(AttackIndex))
	{
		return false;
	}
	const FMDEnemyAttack& Attack = Archetype->Attacks[AttackIndex];
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Attack.Montage || !Mesh || !Mesh->GetAnimInstance())
	{
		return false;
	}

	bAttacking = true;
	ActiveAttackIndex = AttackIndex;
	ActiveMontage = Attack.Montage;
	ActiveTarget = Target;
	bRateRestored = false;

	const float Rate = MDEnemyAttackRules::WindupRate(FindFirstTraceWindowStart(Attack.Montage), TellTime);
	Multicast_PlayAttack(static_cast<uint8>(AttackIndex), Rate, Target); // runs here too (listen server / standalone)
	return bAttacking; // PlayLocal may have failed and finished the attack already
}

void UMDEnemyAttackComponent::Multicast_PlayAttack_Implementation(uint8 AttackIndex, float WindupRate, AActor* Target)
{
	const UMDEnemyArchetype* Archetype = GetArchetype();
	if (!Archetype || !Archetype->Attacks.IsValidIndex(AttackIndex))
	{
		return;
	}
	if (!GetOwner()->HasAuthority())
	{
		// Clients mirror the state for the trace-window rate restore; they never trace or end attacks.
		bAttacking = true;
		ActiveAttackIndex = AttackIndex;
		ActiveMontage = Archetype->Attacks[AttackIndex].Montage;
		ActiveTarget = Target;
		bRateRestored = false;
	}
	PlayLocal(Archetype->Attacks[AttackIndex], WindupRate, Target);
}

void UMDEnemyAttackComponent::PlayLocal(const FMDEnemyAttack& Attack, float WindupRate, AActor* Target)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	AActor* Owner = GetOwner();
	if (!Anim || !Attack.Montage || !Owner)
	{
		if (Owner && Owner->HasAuthority())
		{
			FinishAttack(true);
		}
		return;
	}

	// Only the authority moves the attacker (MDEnemyAttackRules::OwnsAttackMovement): it faces the target and sets the warp
	// before the montage so its first root-motion frame is already warped (as C1 does). A simulated proxy plays the montage
	// in place (its anim instance ignores root motion) on a capsule driven by the replicated movement, so it neither warps
	// nor turns here; its facing and travel arrive from the server.
	const bool bOwnsMovement = MDEnemyAttackRules::OwnsAttackMovement(Owner->GetLocalRole());
	if (bOwnsMovement)
	{
		ApplyWarp(Attack, Target);
	}

	const float Length = Anim->Montage_Play(Attack.Montage, FMath::Clamp(WindupRate, 0.05f, 1.0f) * FMath::Max(Attack.Step.PlayRate, 0.1f));
	UE_LOG(LogMDEnemyAttack, Verbose, TEXT("%s [%s] plays %s at rate %.2f on %s: %s, root motion mode %d, rotation %s"), *Owner->GetName(),
		MDEnemyAttackLocal::RoleName(Owner->GetLocalRole()), *Attack.Name.ToString(), WindupRate, *GetNameSafe(Target),
		bOwnsMovement ? (Target ? TEXT("faces + warps") : TEXT("moves, no warp")) : TEXT("cosmetic (no root motion, no warp)"),
		static_cast<int32>(Anim->RootMotionMode.GetValue()), *Owner->GetActorRotation().ToCompactString());
	if (Length <= 0.0f)
	{
		if (Owner->HasAuthority())
		{
			FinishAttack(true);
		}
		return;
	}
	if (Owner->HasAuthority())
	{
		FOnMontageEnded Ended;
		Ended.BindUObject(this, &UMDEnemyAttackComponent::HandleMontageEnded);
		Anim->Montage_SetEndDelegate(Ended, Attack.Montage);
	}
}

void UMDEnemyAttackComponent::ApplyWarp(const FMDEnemyAttack& Attack, AActor* Target)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	const FVector Origin = Character->GetActorLocation();
	if (Target && !MDWarpTargeting::IsTargetWithinReach(Origin, Target->GetActorLocation(), Attack.Step.WarpRange, 0.0f))
	{
		Target = nullptr;
	}

	// Face the target (or keep the current facing) at swing start, upright: the capsule must never carry a pitch or roll.
	if (Target)
	{
		Character->SetActorRotation(MDEnemyAttackRules::FacingRotation(MDWarpTargeting::YawDegrees(Target->GetActorLocation() - Origin)));
	}

	UMotionWarpingComponent* Warping = Character->FindComponentByClass<UMotionWarpingComponent>();
	if (Target && Warping)
	{
		const UCapsuleComponent* Capsule = Target->FindComponentByClass<UCapsuleComponent>();
		// V1 WP7: both capsule radii, so the warp never ends with the two bodies interpenetrating.
		const float Radius = (Capsule ? Capsule->GetScaledCapsuleRadius() : 0.0f) + Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Point = MDWarpTargeting::ComputeWarpPoint(Origin, Target->GetActorLocation(), Radius, Attack.Step.WarpStopDistance);
		// The warp target's rotation is the facing just set; the montage's SkewWarp windows use it as their rotation target
		// (RotationType Default), so the ghoul turns to this fixed yaw and never computes a facing from its own position.
		Warping->AddOrUpdateWarpTargetFromLocationAndRotation(MDEnemyAttackLocal::WarpTargetName, Point, MDEnemyAttackRules::FacingRotation(Character->GetActorRotation().Yaw));
		Character->SetAnimRootMotionTranslationScale(1.0f);
	}
	else
	{
		if (Warping)
		{
			Warping->RemoveWarpTarget(MDEnemyAttackLocal::WarpTargetName);
		}
		Character->SetAnimRootMotionTranslationScale(FMath::Max(Attack.Step.RootMotionScale, 0.0f));
	}
	bWarpApplied = true;
}

void UMDEnemyAttackComponent::ClearWarp()
{
	if (!bWarpApplied)
	{
		return;
	}
	bWarpApplied = false;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->SetAnimRootMotionTranslationScale(1.0f);
		if (UMotionWarpingComponent* Warping = Character->FindComponentByClass<UMotionWarpingComponent>())
		{
			Warping->RemoveWarpTarget(MDEnemyAttackLocal::WarpTargetName);
		}
		// Safety net: root motion rotation is applied to the capsule in full 3D, and nothing in the movement component
		// re-rights a tilted capsule (RotationRate has no pitch/roll). A swing must leave the attacker upright.
		const FRotator Rotation = Character->GetActorRotation();
		if (MDEnemyAttackRules::IsTilted(Rotation))
		{
			UE_LOG(LogMDEnemyAttack, Warning, TEXT("%s ended an attack tilted (%s); re-righted"), *Character->GetName(), *Rotation.ToCompactString());
			Character->SetActorRotation(MDEnemyAttackRules::FacingRotation(Rotation.Yaw));
		}
	}
}

void UMDEnemyAttackComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!bAttacking || (ActiveMontage.IsValid() && Montage != ActiveMontage.Get()))
	{
		return;
	}
	FinishAttack(bInterrupted);
}

void UMDEnemyAttackComponent::FinishAttack(bool bInterrupted)
{
	if (!bAttacking)
	{
		return;
	}
	bAttacking = false;
	ActiveAttackIndex = INDEX_NONE;
	ActiveMontage.Reset();
	ActiveTarget.Reset();
	ClearWarp();
	if (const UWorld* World = GetWorld())
	{
		LastAttackEndTime = World->GetTimeSeconds();
	}
	OnAttackEnded.Broadcast(bInterrupted);
}

void UMDEnemyAttackComponent::CancelAttack()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !bAttacking)
	{
		return;
	}
	UAnimMontage* Montage = ActiveMontage.Get();
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim && Montage && Anim->Montage_IsPlaying(Montage))
	{
		Anim->Montage_Stop(0.15f, Montage); // the end delegate fires with bInterrupted = true -> FinishAttack
	}
	Multicast_StopAttack();
	FinishAttack(true); // no-op if the delegate already ran
}

void UMDEnemyAttackComponent::Multicast_StopAttack_Implementation()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return; // CancelAttack already stopped it here
	}
	UAnimMontage* Montage = ActiveMontage.Get();
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim && Montage && Anim->Montage_IsPlaying(Montage))
	{
		Anim->Montage_Stop(0.15f, Montage);
	}
	bAttacking = false;
	ActiveAttackIndex = INDEX_NONE;
	ActiveMontage.Reset();
	ActiveTarget.Reset();
	ClearWarp();
}

void UMDEnemyAttackComponent::NotifyTraceWindowBegan()
{
	if (!bAttacking || bRateRestored)
	{
		return;
	}
	bRateRestored = true;
	// The swing is committed: stop tracking the target so it goes where it was aimed (server; the rotation replicates).
	if (AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(GetOwner()); Enemy && Enemy->HasAuthority())
	{
		Enemy->FaceMovement();
	}
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = ActiveMontage.Get();
	if (Anim && Montage && Anim->Montage_IsPlaying(Montage))
	{
		const FMDEnemyAttack* Attack = GetActiveAttack();
		Anim->Montage_SetPlayRate(Montage, Attack ? FMath::Max(Attack->Step.PlayRate, 0.1f) : 1.0f);
	}
}

void UMDEnemyAttackComponent::NotifyTraceWindowBegan(const AActor* Attacker)
{
	if (UMDEnemyAttackComponent* Component = FindFor(Attacker))
	{
		Component->NotifyTraceWindowBegan();
	}
}

void UMDEnemyAttackComponent::GetBodyTraceSegments(TArray<FMDTraceSegment>& OutSegments) const
{
	OutSegments.Reset();
	const FMDEnemyAttack* Attack = GetActiveAttack();
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (!Attack || !Mesh)
	{
		return;
	}
	for (const FName& Socket : Attack->TraceSockets)
	{
		if (Socket == NAME_None || !Mesh->DoesSocketExist(Socket))
		{
			continue;
		}
		const FTransform Xf = Mesh->GetSocketTransform(Socket, RTS_World);
		const FVector Axis = Xf.GetUnitAxis(EAxis::X);
		FMDTraceSegment& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.Id = Socket;
		Segment.Top = Xf.GetLocation() + Axis * Attack->ClawLength;
		Segment.Bottom = Xf.GetLocation() - Axis * Attack->ClawBack;
	}
}

void UMDEnemyAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAttacking)
	{
		bAttacking = false;
		ClearWarp();
	}
	Super::EndPlay(EndPlayReason);
}
