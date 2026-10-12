#include "MDPlayerState.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "GameFramework/Pawn.h"

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
}
