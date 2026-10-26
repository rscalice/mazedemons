#include "MDAfterImageComponent.h"

#include "MDUISettings.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UMDAfterImageComponent::UMDAfterImageComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // runs only while ghosts or a trail are alive
}

UMDAfterImageComponent* UMDAfterImageComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDAfterImageComponent>() : nullptr;
}

double UMDAfterImageComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UMDAfterImageComponent::IsEnabledHere() const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	const UMDLocalSettings* Settings = GetDefault<UMDLocalSettings>();
	return Settings && Settings->bAfterImagesEnabled;
}

USkeletalMeshComponent* UMDAfterImageComponent::GetSourceMesh() const
{
	AActor* Owner = GetOwner();
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		return Character->GetMesh();
	}
	return Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

int32 UMDAfterImageComponent::GetActiveGhostCount() const
{
	int32 Count = 0;
	for (const FMDAfterImageGhost& Ghost : Pool)
	{
		Count += Ghost.bActive ? 1 : 0;
	}
	return Count;
}

int32 UMDAfterImageComponent::AcquireGhostIndex()
{
	const int32 Cap = FMath::Clamp(MaxGhosts, 1, 32);
	for (int32 Index = 0; Index < Pool.Num(); ++Index)
	{
		if (!Pool[Index].bActive && Pool[Index].Mesh)
		{
			return Index;
		}
	}
	if (Pool.Num() < Cap)
	{
		return Pool.AddDefaulted();
	}
	// Pool full: recycle the oldest.
	int32 Oldest = 0;
	for (int32 Index = 1; Index < Pool.Num(); ++Index)
	{
		if (Pool[Index].SpawnTime < Pool[Oldest].SpawnTime)
		{
			Oldest = Index;
		}
	}
	return Oldest;
}

bool UMDAfterImageComponent::SpawnGhost(const FVector* LocationOverride)
{
	if (!IsEnabledHere() || !GhostMaterial)
	{
		return false;
	}
	const double Time = Now();
	if (Time - LastSpawnTime < MinInterval)
	{
		return false;
	}
	USkeletalMeshComponent* Source = GetSourceMesh();
	if (!Source || !Source->GetSkinnedAsset() || !Source->IsVisible())
	{
		return false;
	}

	const int32 Index = AcquireGhostIndex();
	FMDAfterImageGhost& Ghost = Pool[Index];
	if (!Ghost.Mesh)
	{
		UPoseableMeshComponent* Mesh = NewObject<UPoseableMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->RegisterComponent(); // not attached: the ghost stays where it was left
		Mesh->SetSkinnedAssetAndUpdate(Source->GetSkinnedAsset(), true);
		Ghost.Mesh = Mesh;
		Ghost.Materials.Reset();
		const int32 SlotCount = Mesh->GetNumMaterials();
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			Ghost.Materials.Add(Mesh->CreateAndSetMaterialInstanceDynamicFromMaterial(Slot, GhostMaterial));
		}
	}

	FTransform Transform = Source->GetComponentTransform();
	if (LocationOverride)
	{
		Transform.SetLocation(*LocationOverride + (Source->GetComponentLocation() - GetOwner()->GetActorLocation()));
	}
	Ghost.Mesh->SetWorldTransform(Transform);
	Ghost.Mesh->CopyPoseFromSkeletalComponent(Source);
	Ghost.Mesh->SetVisibility(true);

	for (UMaterialInstanceDynamic* Material : Ghost.Materials)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(OpacityParameter, StartOpacity);
			Material->SetVectorParameterValue(TintParameter, Tint);
		}
	}
	Ghost.SpawnTime = Time;
	Ghost.bActive = true;
	LastSpawnTime = Time;
	SetComponentTickEnabled(true);
	return true;
}

bool UMDAfterImageComponent::RequestAfterImage()
{
	return SpawnGhost(nullptr);
}

bool UMDAfterImageComponent::RequestAfterImageAt(FVector Location)
{
	return SpawnGhost(&Location);
}

void UMDAfterImageComponent::StartTrail(float Duration, float Interval)
{
	if (!IsEnabledHere() || Duration <= 0.0f)
	{
		return;
	}
	TrailInterval = FMath::Max(Interval > 0.0f ? Interval : MinInterval, MinInterval);
	TrailEndTime = FMath::Max(TrailEndTime, Now() + Duration);
	NextTrailTime = Now();
	SetComponentTickEnabled(true);
}

void UMDAfterImageComponent::StopTrail()
{
	TrailEndTime = 0.0;
}

void UMDAfterImageComponent::ClearAll()
{
	StopTrail();
	for (FMDAfterImageGhost& Ghost : Pool)
	{
		Ghost.bActive = false;
		if (Ghost.Mesh)
		{
			Ghost.Mesh->SetVisibility(false);
		}
	}
}

void UMDAfterImageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double Time = Now();
	if (Time < TrailEndTime && Time >= NextTrailTime)
	{
		SpawnGhost(nullptr);
		NextTrailTime = Time + TrailInterval;
	}

	bool bAnyActive = false;
	const float SafeLifetime = FMath::Max(Lifetime, 0.05f);
	for (FMDAfterImageGhost& Ghost : Pool)
	{
		if (!Ghost.bActive)
		{
			continue;
		}
		const float Age = static_cast<float>(Time - Ghost.SpawnTime);
		if (Age >= SafeLifetime)
		{
			Ghost.bActive = false;
			if (Ghost.Mesh)
			{
				Ghost.Mesh->SetVisibility(false);
			}
			continue;
		}
		bAnyActive = true;
		const float Opacity = StartOpacity * (1.0f - Age / SafeLifetime);
		for (UMaterialInstanceDynamic* Material : Ghost.Materials)
		{
			if (Material)
			{
				Material->SetScalarParameterValue(OpacityParameter, Opacity);
			}
		}
	}

	if (!bAnyActive && Time >= TrailEndTime)
	{
		SetComponentTickEnabled(false);
	}
}
