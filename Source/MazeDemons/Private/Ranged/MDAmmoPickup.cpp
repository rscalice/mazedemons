#include "Ranged/MDAmmoPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMDAmmoPickup::AMDAmmoPickup()
{
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(90.0f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	SetRootComponent(Trigger);

	Pouch = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pouch"));
	Pouch->SetupAttachment(Trigger);
	Pouch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pouch->SetRelativeScale3D(FVector(0.22f, 0.14f, 0.10f));
	Pouch->SetRelativeLocation(FVector(0.0f, 0.0f, -60.0f));
	Pouch->SetRelativeRotation(FRotator(0.0f, 25.0f, 0.0f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Pouch->SetStaticMesh(CubeMesh.Object);
	}
}

void AMDAmmoPickup::BeginPlay()
{
	Super::BeginPlay();

	// Amber for Light rounds, red-brown for Heavy (cosmetic; the engine basic-shape material takes a Color parameter).
	if (Pouch && Pouch->GetStaticMesh())
	{
		const bool bHeavy = ItemRowName == FName(TEXT("AmmoHeavy"));
		if (UMaterialInstanceDynamic* Mid = Pouch->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), bHeavy ? FLinearColor(0.35f, 0.07f, 0.04f) : FLinearColor(0.85f, 0.55f, 0.08f));
		}
	}

	if (!HasAuthority())
	{
		return;
	}
	if (LifetimeSeconds > 0.0f)
	{
		SetLifeSpan(LifetimeSeconds);
	}
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AMDAmmoPickup::OnTriggerOverlap);
		// Dropped on top of a player: nothing will "begin" overlapping, so look now.
		TArray<AActor*> Already;
		Trigger->GetOverlappingActors(Already, APawn::StaticClass());
		for (AActor* Actor : Already)
		{
			if (Touch(Actor))
			{
				break;
			}
		}
	}
}

void AMDAmmoPickup::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	Touch(OtherActor);
}

bool AMDAmmoPickup::Touch(AActor* Toucher)
{
	if (!HasAuthority() || !Cast<APawn>(Toucher))
	{
		return false;
	}
	return TryPickup(Toucher); // bPickedUp makes the second toucher lose
}
