#include "MDBlinkComponent.h"

#include "MDAfterImageComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

UMDBlinkComponent::UMDBlinkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UMDBlinkComponent* UMDBlinkComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDBlinkComponent>() : nullptr;
}

bool UMDBlinkComponent::CanBlink() const
{
	const UWorld* World = GetWorld();
	return GetOwner() && World && (World->GetTimeSeconds() - LastBlinkTime) >= Cooldown;
}

bool UMDBlinkComponent::TeleportTo(FVector Location)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !CanBlink())
	{
		return false;
	}
	const FVector Origin = Owner->GetActorLocation();
	if (MaxRange > 0.0f && FVector::Dist(Origin, Location) > MaxRange)
	{
		return false;
	}

	Multicast_Departure(Origin); // runs inline on the server, before the move, so the ghost shows the old spot
	if (!Owner->TeleportTo(Location, Owner->GetActorRotation()))
	{
		return false;
	}
	LastBlinkTime = GetWorld()->GetTimeSeconds();
	Multicast_Arrival(Owner->GetActorLocation());
	return true;
}

void UMDBlinkComponent::Multicast_Departure_Implementation(FVector Origin)
{
	if (UMDAfterImageComponent* AfterImage = UMDAfterImageComponent::FindFor(GetOwner()))
	{
		AfterImage->RequestAfterImageAt(Origin);
	}
	OnBlinkDeparture(Origin);
	OnDeparted.Broadcast(Origin);
}

void UMDBlinkComponent::Multicast_Arrival_Implementation(FVector Destination)
{
	// The departure ghost just used the rate limit window: leave the arrival ghost a moment later.
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Destination]()
		{
			if (UMDAfterImageComponent* AfterImage = UMDAfterImageComponent::FindFor(GetOwner()))
			{
				AfterImage->RequestAfterImageAt(Destination);
			}
		}), 0.06f, false);
	}
	OnBlinkArrival(Destination);
	OnArrived.Broadcast(Destination);
}
