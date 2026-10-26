#include "AI/MDTargetableComponent.h"

#include "GameFramework/Actor.h"

UMDTargetableComponent::UMDTargetableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMDTargetableComponent* UMDTargetableComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDTargetableComponent>() : nullptr;
}

bool UMDTargetableComponent::IsActorTargetable(const AActor* Actor)
{
	const UMDTargetableComponent* Targetable = FindFor(Actor);
	return Targetable && Targetable->bTargetable;
}
