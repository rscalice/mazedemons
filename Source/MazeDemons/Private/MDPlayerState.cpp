#include "MDPlayerState.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

AMDPlayerState::AMDPlayerState()
{
	AbilitySystem = CreateDefaultSubobject<UMDAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	Attributes = CreateDefaultSubobject<UMDAttributeSet>(TEXT("MazeDemonsAttributes"));

	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* AMDPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

UMDAbilitySystemComponent* AMDPlayerState::GetMDAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AMDPlayerState::HandleDeath()
{
	RagdollPawn(GetPawn());
}

void AMDPlayerState::RagdollPawn(APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh || Mesh->IsSimulatingPhysics())
	{
		return;
	}

	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetSimulatePhysics(true);
}

void AMDPlayerState::BeginPlay()
{
	Super::BeginPlay();

	OnPawnSet.AddDynamic(this, &AMDPlayerState::HandlePawnSet);
	if (APawn* ExistingPawn = GetPawn())
	{
		HandlePawnSet(this, ExistingPawn, nullptr);
	}
}

void AMDPlayerState::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (!NewPawn || !AbilitySystem)
	{
		return;
	}

	AbilitySystem->InitAbilityActorInfo(this, NewPawn);
	AbilitySystem->RegisterAttributeSet(Attributes);

	// Single OnDeath binding for the PlayerState's lifetime; the handler always targets the current pawn,
	// so respawns (OldPawn -> NewPawn) cannot stack bindings.
	if (!bDeathBound)
	{
		bDeathBound = true;
		AbilitySystem->OnDeath.AddDynamic(this, &AMDPlayerState::HandleDeath);
	}

	if (HasAuthority())
	{
		if (!bStatsInitialized)
		{
			bStatsInitialized = true;
			AbilitySystem->InitializeStats(StartingMaxHealth, StartingAttackPower, StartingBaseDefense);
		}
		else if (AbilitySystem->IsDead())
		{
			// Respawn: stats persist, but a new pawn must not start dead.
			AbilitySystem->ResetHealthToMax();
		}
	}

	// Pawn arrived after death was already replicated (late join / pawn set later). Checked after the
	// server-side respawn reset so a fresh respawn pawn is not ragdolled.
	if (AbilitySystem->IsDead())
	{
		RagdollPawn(NewPawn);
	}
}
