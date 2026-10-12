#include "MDTestDummy.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDGameplayStatics.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMDTestDummy::AMDTestDummy()
{
	PrimaryActorTick.bCanEverTick = false;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.0f, 90.0f);
	Capsule->SetCollisionProfileName(TEXT("BlockAll"));
	SetRootComponent(Capsule);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	Body->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyMesh(TEXT("/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny.SKM_Manny"));
	if (BodyMesh.Succeeded())
	{
		Body->SetSkeletalMesh(BodyMesh.Object);
	}

	auto MakeArmorComponent = [this](const TCHAR* Name) -> USkeletalMeshComponent*
	{
		USkeletalMeshComponent* Comp = CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		Comp->SetupAttachment(Body);
		Comp->SetLeaderPoseComponent(Body);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Comp;
	};
	HelmetMesh = MakeArmorComponent(TEXT("HelmetMesh"));
	ChestMesh = MakeArmorComponent(TEXT("ChestMesh"));
	LegsMesh = MakeArmorComponent(TEXT("LegsMesh"));
	BootsMesh = MakeArmorComponent(TEXT("BootsMesh"));

	bReplicates = true;

	AbilitySystem = CreateDefaultSubobject<UMDAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	Attributes = CreateDefaultSubobject<UMDAttributeSet>(TEXT("MazeDemonsAttributes"));
}

UAbilitySystemComponent* AMDTestDummy::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

FGenericTeamId AMDTestDummy::GetGenericTeamId() const
{
	return FGenericTeamId(MDTeams::Demons);
}

void AMDTestDummy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDTestDummy, EquippedArmor);
}

USkeletalMeshComponent* AMDTestDummy::GetArmorComponent(FName Slot) const
{
	if (Slot == TEXT("Helmet")) { return HelmetMesh; }
	if (Slot == TEXT("Chest")) { return ChestMesh; }
	if (Slot == TEXT("Legs")) { return LegsMesh; }
	if (Slot == TEXT("Boots")) { return BootsMesh; }
	return nullptr;
}

void AMDTestDummy::ApplyArmorVisuals()
{
	for (USkeletalMeshComponent* Comp : { HelmetMesh.Get(), ChestMesh.Get(), LegsMesh.Get(), BootsMesh.Get() })
	{
		if (Comp)
		{
			Comp->SetSkeletalMesh(nullptr);
		}
	}
	for (const FMDDummyArmorSlot& Piece : EquippedArmor)
	{
		if (USkeletalMeshComponent* Comp = GetArmorComponent(Piece.Slot))
		{
			Comp->SetSkeletalMesh(Piece.Mesh);
			Comp->SetLeaderPoseComponent(Body);
		}
	}
}

void AMDTestDummy::OnRep_Armor()
{
	ApplyArmorVisuals();
}

void AMDTestDummy::EquipArmorPiece(FName Slot, USkeletalMesh* Mesh, float DamageReductionPercent)
{
	if (!HasAuthority() || !GetArmorComponent(Slot))
	{
		return;
	}

	FMDDummyArmorSlot* Existing = EquippedArmor.FindByPredicate([Slot](const FMDDummyArmorSlot& P) { return P.Slot == Slot; });
	if (!Existing)
	{
		Existing = &EquippedArmor.AddDefaulted_GetRef();
	}
	Existing->Slot = Slot;
	Existing->Mesh = Mesh;
	Existing->DamageReductionPercent = DamageReductionPercent;

	ApplyArmorVisuals();
	if (AbilitySystem)
	{
		AbilitySystem->SetArmorPieceDamageReduction(Slot, DamageReductionPercent);
	}
}

void AMDTestDummy::UnequipArmorPiece(FName Slot)
{
	if (!HasAuthority())
	{
		return;
	}

	EquippedArmor.RemoveAll([Slot](const FMDDummyArmorSlot& P) { return P.Slot == Slot; });
	ApplyArmorVisuals();
	if (AbilitySystem)
	{
		AbilitySystem->RemoveArmorPieceDamageReduction(Slot);
	}
}

void AMDTestDummy::UnequipAllArmor()
{
	if (!HasAuthority())
	{
		return;
	}

	const TArray<FMDDummyArmorSlot> Previous = EquippedArmor;
	for (const FMDDummyArmorSlot& Piece : Previous)
	{
		UnequipArmorPiece(Piece.Slot);
	}
}

void AMDTestDummy::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
		AbilitySystem->RegisterAttributeSet(Attributes);
		AbilitySystem->OnHealthChanged.AddDynamic(this, &AMDTestDummy::HandleHealthChanged);
		if (HasAuthority())
		{
			ResetDummy();
			for (const FMDDummyArmorSlot& Piece : StartingArmor)
			{
				EquipArmorPiece(Piece.Slot, Piece.Mesh, Piece.DamageReductionPercent);
			}
		}
		ApplyArmorVisuals();
	}
}

void AMDTestDummy::ResetDummy()
{
	if (!HasAuthority())
	{
		return;
	}

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
		AActor* DamageInstigator = nullptr;
		if (EventInstigator)
		{
			DamageInstigator = EventInstigator->GetPawn() ? static_cast<AActor*>(EventInstigator->GetPawn()) : static_cast<AActor*>(EventInstigator);
		}
		else if (DamageCauser)
		{
			DamageInstigator = DamageCauser->GetInstigator() ? static_cast<AActor*>(DamageCauser->GetInstigator()) : DamageCauser;
		}
		AbilitySystem->ApplyDamage(Damage, DamageInstigator, DamageCauser);
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

	if (NewValue <= 0.0f && OldValue > 0.0f && RespawnDelay >= 0.0f && HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ResetTimer, this, &AMDTestDummy::ResetDummy, FMath::Max(RespawnDelay, 0.01f), false);
	}
}
