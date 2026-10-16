#include "MDStaminaComponent.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UMDStaminaComponent::UMDStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UMDStaminaComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server simulates stamina; everyone else receives attribute and flag replication.
	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	SetComponentTickEnabled(bAuthority);
	PrimaryComponentTick.TickInterval = SimulationInterval;
}

void UMDStaminaComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDStaminaComponent, bExhausted);
}

UMDAbilitySystemComponent* UMDStaminaComponent::GetASC() const
{
	if (const AMDPlayerState* PS = Cast<AMDPlayerState>(GetOwner()))
	{
		return PS->GetMDAbilitySystemComponent();
	}
	return nullptr;
}

bool UMDStaminaComponent::HasStaminaFor(float Cost) const
{
	const UMDAbilitySystemComponent* ASC = GetASC();
	return ASC && ASC->GetStamina() >= Cost;
}

bool UMDStaminaComponent::IsSprintAllowedFor(bool bPawnLocallyControlled) const
{
	if (!bExhausted)
	{
		return true;
	}
	if (bPawnLocallyControlled || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	return (Now - ExhaustedStartTime) < SprintGateGrace;
}

FMDStaminaStepOutput UMDStaminaComponent::Step(const FMDStaminaStepInput& In)
{
	FMDStaminaStepOutput Out;
	Out.Stamina = FMath::Clamp(In.Stamina, 0.0f, In.MaxStamina);
	Out.bExhausted = In.bExhausted;

	if (In.bSprinting && !In.bExhausted)
	{
		Out.Stamina = FMath::Max(Out.Stamina - In.SprintDrainPerSec * In.DeltaTime, 0.0f);
		Out.bSpent = true;
	}
	else if (In.TimeSinceSpend >= In.RegenDelay)
	{
		Out.Stamina = FMath::Min(Out.Stamina + In.RegenPerSec * In.DeltaTime, In.MaxStamina);
	}

	// Hysteresis: exhausted at 0, recovered only after regaining a fraction of the pool (no flicker).
	if (Out.Stamina <= 0.0f)
	{
		Out.bExhausted = true;
	}
	else if (Out.bExhausted && Out.Stamina >= In.ExhaustedRecoverFraction * In.MaxStamina)
	{
		Out.bExhausted = false;
	}
	return Out;
}

bool UMDStaminaComponent::IsPawnSprinting() const
{
	const AMDPlayerState* PS = Cast<AMDPlayerState>(GetOwner());
	const ACharacter* Character = PS ? Cast<ACharacter>(PS->GetPawn()) : nullptr;
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}
	return Movement->Velocity.Size2D() >= SprintSpeedThreshold;
}

void UMDStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UMDAbilitySystemComponent* ASC = GetASC();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ASC || ASC->IsDead() || !ASC->GetMazeDemonsAttributes())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	FMDStaminaStepInput In;
	In.Stamina = ASC->GetStamina();
	In.MaxStamina = ASC->GetMaxStamina();
	In.RegenPerSec = ASC->GetStaminaRegenRate();
	In.RegenDelay = RegenDelay;
	In.SprintDrainPerSec = SprintDrainPerSec;
	In.ExhaustedRecoverFraction = ExhaustedRecoverFraction;
	In.DeltaTime = DeltaTime;
	In.TimeSinceSpend = static_cast<float>(Now - LastSpendTime);
	In.bSprinting = IsPawnSprinting();
	In.bExhausted = bExhausted;

	const FMDStaminaStepOutput Out = Step(In);
	if (!FMath::IsNearlyEqual(Out.Stamina, In.Stamina))
	{
		ASC->SetStaminaClamped(Out.Stamina);
	}
	if (Out.bSpent)
	{
		LastSpendTime = Now;
	}
	SetExhausted(Out.bExhausted);

	// Loose tags are reference counted: only touch the tag when the state actually changes.
	const bool bShouldTagSprinting = In.bSprinting && !Out.bExhausted;
	if (bShouldTagSprinting != bSprintTagged)
	{
		bSprintTagged = bShouldTagSprinting;
		if (bSprintTagged)
		{
			ASC->AddLooseGameplayTag(TAG_State_Sprinting.GetTag());
		}
		else
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Sprinting.GetTag());
		}
	}
}

void UMDStaminaComponent::Consume(float Cost)
{
	UMDAbilitySystemComponent* ASC = GetASC();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ASC || Cost <= 0.0f)
	{
		return;
	}
	ASC->SetStaminaClamped(ASC->GetStamina() - Cost);
	if (const UWorld* World = GetWorld())
	{
		LastSpendTime = World->GetTimeSeconds();
	}
	if (ASC->GetStamina() <= 0.0f)
	{
		SetExhausted(true);
	}
}

void UMDStaminaComponent::NotifyJumped()
{
	Consume(JumpCost);
}

void UMDStaminaComponent::NotifyTraversalStarted()
{
	Consume(VaultCost);
}

void UMDStaminaComponent::ResetState()
{
	LastSpendTime = -1000.0;
	SetExhausted(false);
}

void UMDStaminaComponent::SetExhausted(bool bNewExhausted)
{
	if (bExhausted == bNewExhausted)
	{
		return;
	}
	bExhausted = bNewExhausted;
	if (bExhausted && GetOwner() && GetOwner()->HasAuthority() && GetWorld())
	{
		ExhaustedStartTime = GetWorld()->GetTimeSeconds();
	}

	if (UMDAbilitySystemComponent* ASC = GetASC())
	{
		if (bExhausted)
		{
			ASC->AddLooseGameplayTag(TAG_State_Exhausted.GetTag());
		}
		else
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Exhausted.GetTag());
		}
	}
	OnExhaustedChanged.Broadcast(bExhausted);
}

void UMDStaminaComponent::OnRep_Exhausted()
{
	OnExhaustedChanged.Broadcast(bExhausted);
}
