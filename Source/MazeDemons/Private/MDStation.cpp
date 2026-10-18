#include "MDStation.h"

#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "MDGameSettings.h"
#include "MDInventoryTypes.h"
#include "MDPlayerController.h"
#include "Net/UnrealNetwork.h"

AMDStation::AMDStation()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// Must equal GA_PlayerInteract's RequiredActorTag (set in the Blueprint; verified in WP2).
	Tags.Add(FName(TEXT("Interactable")));

	InteractBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractBox"));
	InteractBox->InitBoxExtent(FVector(80.0f, 80.0f, 60.0f));
	InteractBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetRootComponent(InteractBox);
}

void AMDStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDStation, Services);
}

bool AMDStation::Supports(EMDStationService Service) const
{
	return (Services & static_cast<int32>(Service)) != 0;
}

bool AMDStation::IsWithinRange(const FVector& A, const FVector& B, float Range)
{
	return FVector::DistSquared(A, B) <= FMath::Square(FMath::Max(Range, 0.0f));
}

void AMDStation::Interact(APawn* User)
{
	if (!HasAuthority() || !User)
	{
		return;
	}
	if (!IsWithinRange(User->GetActorLocation(), GetActorLocation(), UMDGameSettings::GetEconomy().StationRange))
	{
		return;
	}
	if (AMDPlayerController* PC = Cast<AMDPlayerController>(User->GetController()))
	{
		PC->NotifyStationOpened(this);
	}
}
