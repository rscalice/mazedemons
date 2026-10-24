#include "MDPhysicalHitReactComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPhysicalReactions(TEXT("md.Combat.PhysicalReactions"), 1,
		TEXT("1 = hits play a short physical upper-body reaction (spine impulse, never a ragdoll). 0 = off."), ECVF_Default);
}

UMDPhysicalHitReactComponent::UMDPhysicalHitReactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(false);
}

float UMDPhysicalHitReactComponent::EvaluateBlend(float Elapsed, float BlendIn, float BlendOut, float PeakWeight)
{
	BlendIn = FMath::Max(BlendIn, KINDA_SMALL_NUMBER);
	BlendOut = FMath::Max(BlendOut, KINDA_SMALL_NUMBER);
	if (Elapsed <= 0.0f || Elapsed >= BlendIn + BlendOut)
	{
		return 0.0f;
	}
	const float Alpha = Elapsed < BlendIn ? Elapsed / BlendIn : 1.0f - (Elapsed - BlendIn) / BlendOut;
	return FMath::Clamp(PeakWeight, 0.0f, 1.0f) * FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f), 2.0f);
}

FVector UMDPhysicalHitReactComponent::ImpulseDirection(const FVector& Forward, const FVector& Right, EMDHitDirection Direction)
{
	switch (Direction)
	{
	case EMDHitDirection::Front: return -Forward;
	case EMDHitDirection::Back: return Forward;
	case EMDHitDirection::Left: return Right;
	case EMDHitDirection::Right: return -Right;
	}
	return FVector::ZeroVector;
}

bool UMDPhysicalHitReactComponent::EnsureSetup(USkeletalMeshComponent* Mesh)
{
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Mesh->GetPhysicsAsset() || Mesh->GetBoneIndex(RootBone) == INDEX_NONE)
	{
		return false;
	}
	const FBodyInstance* Body = Mesh->GetBodyInstance(RootBone);
	if (!Body || !Body->IsValidBodyInstance())
	{
		return false; // the physics asset has no body on the spine (or the physics state is not created)
	}

	if (!PhysicalAnimation)
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			return false;
		}
		PhysicalAnimation = NewObject<UPhysicalAnimationComponent>(Owner, TEXT("MDPhysicalAnimation"));
		PhysicalAnimation->RegisterComponent();
	}
	if (ConfiguredMesh.Get() != Mesh)
	{
		PhysicalAnimation->SetSkeletalMeshComponent(Mesh);
		FPhysicalAnimationData Data;
		Data.bIsLocalSimulation = true;
		Data.OrientationStrength = 1000.0f;
		Data.AngularVelocityStrength = 100.0f;
		Data.PositionStrength = 1000.0f;
		Data.VelocityStrength = 100.0f;
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(RootBone, Data, true);
		ConfiguredMesh = Mesh;
	}
	return true;
}

bool UMDPhysicalHitReactComponent::PlayHitReaction(USkeletalMeshComponent* Mesh, EMDHitDirection Direction, EMDHitReaction Reaction)
{
	if (CVarPhysicalReactions.GetValueOnGameThread() == 0 || !GetOwner() || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	if (bActive && ActiveMesh.Get() != Mesh)
	{
		Finish();
	}
	if (!EnsureSetup(Mesh))
	{
		return false;
	}

	if (!bActive)
	{
		// Physics only simulates with physics collision. A query-only mesh (the test dummy) is lifted for the duration and
		// stops reacting to anything in the world while it is, so the simulated bodies cannot shove the capsule.
		if (Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryOnly)
		{
			bRestoreCollision = true;
			SavedCollisionEnabled = Mesh->GetCollisionEnabled();
			SavedResponses = Mesh->GetCollisionResponseToChannels();
			Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		Mesh->SetAllBodiesBelowSimulatePhysics(RootBone, true, true);
		ActiveMesh = Mesh;
		bActive = true;
		Elapsed = 0.0f;
		SetComponentTickEnabled(true);
	}
	else
	{
		Elapsed = FMath::Min(Elapsed, BlendInTime); // retrigger at the peak, no restart from zero
	}

	const float Strength = static_cast<int32>(Reaction) > static_cast<int32>(EMDHitReaction::Flinch) ? 1.5f : 1.0f;
	const AActor* Owner = GetOwner();
	const FVector Impulse = ImpulseDirection(Owner->GetActorForwardVector(), Owner->GetActorRightVector(), Direction) * ImpulseSpeed * Strength;
	Mesh->AddImpulseToAllBodiesBelow(Impulse, RootBone, true);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(RootBone, EvaluateBlend(Elapsed + KINDA_SMALL_NUMBER, BlendInTime, BlendOutTime, PeakWeight), false, true);
	return true;
}

void UMDPhysicalHitReactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USkeletalMeshComponent* Mesh = ActiveMesh.Get();
	if (!bActive || !Mesh)
	{
		Finish();
		return;
	}
	Elapsed += DeltaTime;
	if (Elapsed >= BlendInTime + BlendOutTime)
	{
		Finish();
		return;
	}
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(RootBone, EvaluateBlend(Elapsed, BlendInTime, BlendOutTime, PeakWeight), false, true);
}

void UMDPhysicalHitReactComponent::Finish()
{
	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		// Death ragdolls the whole mesh (root body simulating): leave that alone.
		const FBodyInstance* Root = Mesh->GetBodyInstance(NAME_None);
		const bool bRagdolling = Root && Root->IsInstanceSimulatingPhysics();
		if (!bRagdolling)
		{
			Mesh->SetAllBodiesBelowPhysicsBlendWeight(RootBone, 0.0f, false, true);
			Mesh->SetAllBodiesBelowSimulatePhysics(RootBone, false, true);
		}
		if (bRestoreCollision && !bRagdolling)
		{
			Mesh->SetCollisionResponseToChannels(SavedResponses);
			Mesh->SetCollisionEnabled(SavedCollisionEnabled);
		}
	}
	bRestoreCollision = false;
	bActive = false;
	Elapsed = 0.0f;
	ActiveMesh.Reset();
	SetComponentTickEnabled(false);
}

void UMDPhysicalHitReactComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bActive)
	{
		Finish();
	}
	Super::EndPlay(EndPlayReason);
}
