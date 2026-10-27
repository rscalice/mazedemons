#include "AI/MDRallyPoint.h"

#include "Components/SceneComponent.h"

const FName AMDRallyPoint::RallyTag(TEXT("MDRallyPoint"));

AMDRallyPoint::AMDRallyPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Tags.Add(RallyTag);
}
