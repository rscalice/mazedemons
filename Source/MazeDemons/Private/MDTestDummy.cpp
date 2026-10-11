#include "MDTestDummy.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMDTestDummy::AMDTestDummy()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionProfileName(TEXT("BlockAll"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Body->SetStaticMesh(CylinderMesh.Object);
		Body->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.8f));
	}

	AbilitySystem = CreateDefaultSubobject<UMDAbilitySystemComponent>(TEXT("AbilitySystem"));
}

void AMDTestDummy::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystem)
	{
		AbilitySystem->OnHealthChanged.AddDynamic(this, &AMDTestDummy::HandleHealthChanged);
		ResetDummy();
	}
}

void AMDTestDummy::ResetDummy()
{
	GetWorldTimerManager().ClearTimer(ResetTimer);

	if (AbilitySystem)
	{
		AbilitySystem->InitializeStats(StartingMaxHealth, StartingAttackPower, StartingDefense);
	}
}

float AMDTestDummy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AbilitySystem && Damage > 0.0f)
	{
		AbilitySystem->ApplyDamage(Damage);
	}
	return Damage;
}

void AMDTestDummy::HandleHealthChanged(float OldValue, float NewValue)
{
	if (bShowDamageNumbers && NewValue < OldValue)
	{
		const FString Text = FString::Printf(TEXT("-%.1f  (HP %.1f/%.1f, Def %.1f%%)"),
			OldValue - NewValue, NewValue, AbilitySystem->GetMaxHealth(), AbilitySystem->GetDefense());
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0f, 0.0f, 150.0f), Text, nullptr, FColor::Yellow, 2.0f, true);
		UE_LOG(LogTemp, Log, TEXT("TestDummy %s: %s"), *GetName(), *Text);
	}

	if (NewValue <= 0.0f && OldValue > 0.0f && RespawnDelay >= 0.0f)
	{
		GetWorldTimerManager().SetTimer(ResetTimer, this, &AMDTestDummy::ResetDummy, FMath::Max(RespawnDelay, 0.01f), false);
	}
}
