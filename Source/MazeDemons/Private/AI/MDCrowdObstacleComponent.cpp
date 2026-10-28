#include "AI/MDCrowdObstacleComponent.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Navigation/CrowdManager.h"

UMDCrowdObstacleComponent::UMDCrowdObstacleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

FVector UMDCrowdObstacleComponent::GetCrowdAgentLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FAISystem::InvalidLocation;
}

FVector UMDCrowdObstacleComponent::GetCrowdAgentVelocity() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetVelocity() : FVector::ZeroVector;
}

void UMDCrowdObstacleComponent::GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const
{
	CylinderRadius = 34.0f + Margin;
	CylinderHalfHeight = 90.0f;
	if (const AActor* Owner = GetOwner())
	{
		if (const UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			CylinderRadius = Capsule->GetScaledCapsuleRadius() + Margin;
			CylinderHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
}

float UMDCrowdObstacleComponent::GetCrowdAgentMaxSpeed() const
{
	return 700.0f; // a sprinting player; the crowd anticipates the obstacle's motion up to this
}

UMDCrowdObstacleComponent* UMDCrowdObstacleComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn || !Pawn->HasAuthority())
	{
		return nullptr;
	}
	if (UMDCrowdObstacleComponent* Existing = Pawn->FindComponentByClass<UMDCrowdObstacleComponent>())
	{
		return Existing;
	}
	if (!UCrowdManager::GetCurrent(Pawn->GetWorld()))
	{
		return nullptr;
	}
	UMDCrowdObstacleComponent* Component = NewObject<UMDCrowdObstacleComponent>(Pawn, TEXT("MDCrowdObstacle"));
	Component->RegisterComponent();
	return Component;
}

void UMDCrowdObstacleComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithCrowd();
}

void UMDCrowdObstacleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromCrowd();
	Super::EndPlay(EndPlayReason);
}

void UMDCrowdObstacleComponent::RegisterWithCrowd()
{
	if (bRegistered)
	{
		return;
	}
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	if (UCrowdManager* Crowd = UCrowdManager::GetCurrent(GetWorld()))
	{
		Crowd->RegisterAgent(this);
		bRegistered = true;
	}
}

void UMDCrowdObstacleComponent::UnregisterFromCrowd()
{
	if (!bRegistered)
	{
		return;
	}
	if (UCrowdManager* Crowd = UCrowdManager::GetCurrent(GetWorld()))
	{
		Crowd->UnregisterAgent(this);
	}
	bRegistered = false;
}

void UMDCrowdObstacleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bRegistered)
	{
		RegisterWithCrowd(); // the crowd manager may appear after this component (navigation system init)
		return;
	}
	// Keep the obstacle's position and velocity fresh for the crowd step (the manager refreshes non-simulated agents in
	// its own tick too; this covers a manager that ticks before the pawn moved).
	if (UCrowdManager* Crowd = UCrowdManager::GetCurrent(GetWorld()))
	{
		Crowd->UpdateAgentState(this);
	}
}
