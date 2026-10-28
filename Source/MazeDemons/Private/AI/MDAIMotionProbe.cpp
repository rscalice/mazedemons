#include "AI/MDAIMotionProbe.h"

#include "AI/MDAITypes.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "MDPhysicalHitReactComponent.h"
#include "Misc/OutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDAIMotion, Log, All);

namespace MDAIMotionProbeLocal
{
	TAutoConsoleVariable<float> CVarThreshold(TEXT("md.AI.DebugMotion"), 0.0f,
		TEXT("> 0: log (LogMDAIMotion) every enemy whose capsule moved more than this many cm in one frame, whose pelvis sits further than this from its capsule, whose mesh offset changed, or which tilted; with intent, attack, montage, velocity, net role and physical-reaction state. 0 = off."),
		ECVF_Cheat);

	const TCHAR* RoleName(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Auth");
		case ROLE_AutonomousProxy: return TEXT("Auto");
		case ROLE_SimulatedProxy: return TEXT("Sim");
		default: return TEXT("None");
		}
	}

	FString V(const FVector& In)
	{
		return FString::Printf(TEXT("(%.0f %.0f %.0f)"), In.X, In.Y, In.Z);
	}
}

UMDAIMotionProbe* UMDAIMotionProbe::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMDAIMotionProbe>() : nullptr;
}

bool UMDAIMotionProbe::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

float UMDAIMotionProbe::GetThreshold()
{
	return MDAIMotionProbeLocal::CVarThreshold.GetValueOnGameThread();
}

bool UMDAIMotionProbe::IsTickable() const
{
	return GetThreshold() > 0.0f && GetWorld() && !IsTemplate();
}

TStatId UMDAIMotionProbe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDAIMotionProbe, STATGROUP_Tickables);
}

FString UMDAIMotionProbe::Describe(const AMDEnemyCharacter& Enemy) const
{
	const USkeletalMeshComponent* Mesh = Enemy.GetMesh();
	const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	const UMDPhysicalHitReactComponent* Physical = Enemy.FindComponentByClass<UMDPhysicalHitReactComponent>();
	const UCharacterMovementComponent* Move = Enemy.GetCharacterMovement();
	return FString::Printf(TEXT("%s [%s] intent=%s attacking=%d montage=%s@%.2f rm=%d vel=%.0f mode=%d physReact=%d"), *Enemy.GetName(),
		MDAIMotionProbeLocal::RoleName(Enemy.GetLocalRole()), MDIntentToString(Enemy.GetIntent()),
		Enemy.AttackComponent && Enemy.AttackComponent->IsAttacking() ? 1 : 0,
		Montage ? *Montage->GetName() : TEXT("-"), Montage && Anim ? Anim->Montage_GetPosition(Montage) : 0.0f,
		Anim ? static_cast<int32>(Anim->RootMotionMode.GetValue()) : -1,
		Enemy.GetVelocity().Size(), Move ? static_cast<int32>(Move->MovementMode.GetValue()) : -1,
		Physical && Physical->IsReacting() ? 1 : 0);
}

void UMDAIMotionProbe::Tick(float DeltaTime)
{
	const float Threshold = GetThreshold();
	UWorld* World = GetWorld();
	if (Threshold <= 0.0f || !World)
	{
		return;
	}
	++Counters.Frames;
	const double Now = World->GetTimeSeconds();
	for (const TWeakObjectPtr<AMDEnemyCharacter>& Weak : AMDEnemyCharacter::GetAllEnemies())
	{
		const AMDEnemyCharacter* Enemy = Weak.Get();
		if (!Enemy || Enemy->GetWorld() != World || Enemy->IsEnemyDead())
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		const USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		if (!Capsule || !Mesh)
		{
			continue;
		}
		FSample& S = Samples.FindOrAdd(Enemy);
		const FVector CapsuleLoc = Capsule->GetComponentLocation();
		const FVector MeshRel = Mesh->GetRelativeLocation();
		TArray<FString> Problems;
		if (S.bValid)
		{
			const float Delta = static_cast<float>(FVector::Dist(S.Capsule, CapsuleLoc));
			if (Delta > Threshold)
			{
				++Counters.CapsulePops;
				Problems.Add(FString::Printf(TEXT("CAPSULE POP %.0f cm in %.0f ms %s -> %s"), Delta, DeltaTime * 1000.0f, *MDAIMotionProbeLocal::V(S.Capsule), *MDAIMotionProbeLocal::V(CapsuleLoc)));
			}
			if (!MeshRel.Equals(S.MeshRelative, 1.0f))
			{
				++Counters.MeshOffsets;
				Problems.Add(FString::Printf(TEXT("MESH OFFSET %s -> %s"), *MDAIMotionProbeLocal::V(S.MeshRelative), *MDAIMotionProbeLocal::V(MeshRel)));
			}
		}
		const FName PelvisBone = Mesh->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE ? FName(TEXT("pelvis")) : NAME_None;
		if (PelvisBone != NAME_None)
		{
			const FVector Pelvis = Mesh->GetSocketLocation(PelvisBone);
			const float Away = static_cast<float>(FVector::Dist2D(Pelvis, CapsuleLoc));
			if (Away > Threshold)
			{
				++Counters.PelvisAway;
				Problems.Add(FString::Printf(TEXT("PELVIS AWAY %.0f cm from capsule (pelvis %s)"), Away, *MDAIMotionProbeLocal::V(Pelvis)));
			}
		}
		const FRotator ActorRot = Enemy->GetActorRotation();
		const FRotator MeshRot = Mesh->GetRelativeRotation();
		if (FMath::Abs(ActorRot.Pitch) > 2.0f || FMath::Abs(ActorRot.Roll) > 2.0f || FMath::Abs(MeshRot.Pitch) > 2.0f || FMath::Abs(MeshRot.Roll) > 2.0f)
		{
			++Counters.Tilts;
			Problems.Add(FString::Printf(TEXT("TILT actor (P%.1f R%.1f) mesh (P%.1f R%.1f)"), ActorRot.Pitch, ActorRot.Roll, MeshRot.Pitch, MeshRot.Roll));
		}
		if (Problems.Num() > 0 && Now - S.LastLogTime >= 0.0)
		{
			S.LastLogTime = Now;
			UE_LOG(LogMDAIMotion, Warning, TEXT("[MDMotion] t=%.2f %s | %s"), Now, *FString::Join(Problems, TEXT(" | ")), *Describe(*Enemy));
		}
		S.Capsule = CapsuleLoc;
		S.MeshRelative = MeshRel;
		S.bValid = true;
	}
	for (auto It = Samples.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UMDAIMotionProbe::PrintSummary(FOutputDevice& Ar) const
{
	Ar.Logf(TEXT("md.AI.DebugMotion: %d frames, %d capsule pops, %d pelvis-away frames, %d mesh offset changes, %d tilt frames (threshold %.0f cm)"),
		Counters.Frames, Counters.CapsulePops, Counters.PelvisAway, Counters.MeshOffsets, Counters.Tilts, GetThreshold());
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GMDMotionSummary(
	TEXT("md.AI.DebugMotionSummary"),
	TEXT("Prints the md.AI.DebugMotion counters."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (const UMDAIMotionProbe* Probe = UMDAIMotionProbe::Get(World))
		{
			Probe->PrintSummary(Ar);
		}
	}));
