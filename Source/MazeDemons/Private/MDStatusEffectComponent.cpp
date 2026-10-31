#include "MDStatusEffectComponent.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatStatics.h"
#include "MDGameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace
{
	TSubclassOf<UGameplayEffect> TagEffectClass(EMDStatusType Type)
	{
		switch (Type)
		{
		case EMDStatusType::Bleed:
			return UMDStatusEffect_Bleed::StaticClass();
		case EMDStatusType::Frost:
			return UMDStatusEffect_Frost::StaticClass();
		case EMDStatusType::Poison:
			return UMDStatusEffect_Poison::StaticClass();
		case EMDStatusType::Burn:
			return UMDStatusEffect_Burn::StaticClass();
		default:
			return nullptr;
		}
	}
}

UMDStatusEffectComponent::UMDStatusEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // runs only while something is active
	SetIsReplicatedByDefault(true);
}

UMDStatusEffectComponent* UMDStatusEffectComponent::FindFor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDStatusEffectComponent* Own = Actor->FindComponentByClass<UMDStatusEffectComponent>())
	{
		return Own;
	}
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		PS = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		PS = Controller->PlayerState;
	}
	return PS ? PS->FindComponentByClass<UMDStatusEffectComponent>() : nullptr;
}

bool UMDStatusEffectComponent::ApplyStatusToActor(AActor* Target, EMDStatusType Type, AActor* Instigator)
{
	UMDStatusEffectComponent* Comp = FindFor(Target);
	return Comp && Comp->ApplyStatus(Type, Instigator);
}

void UMDStatusEffectComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority() && !bDeathBound)
	{
		if (UMDAbilitySystemComponent* ASC = GetASC())
		{
			ASC->OnDeath.AddUniqueDynamic(this, &UMDStatusEffectComponent::HandleOwnerDeath);
			bDeathBound = true;
		}
	}
	UpdateTickEnabled();
}

void UMDStatusEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMDAbilitySystemComponent* ASC = GetASC())
	{
		ASC->OnDeath.RemoveDynamic(this, &UMDStatusEffectComponent::HandleOwnerDeath);
	}
	bDeathBound = false;
	// Hand the pawn its normal speed back.
	Statuses.Reset();
	ApplyMovementScale();
	Super::EndPlay(EndPlayReason);
}

void UMDStatusEffectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDStatusEffectComponent, Statuses);
}

UMDAbilitySystemComponent* UMDStatusEffectComponent::GetASC() const
{
	return UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner());
}

AActor* UMDStatusEffectComponent::GetAvatar() const
{
	if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
	{
		if (APawn* Pawn = PS->GetPawn())
		{
			return Pawn;
		}
		// Between possessions the ASC still names the avatar it was last initialised with.
		AActor* Avatar = GetASC() ? GetASC()->GetAvatarActor() : nullptr;
		return Avatar != GetOwner() ? Avatar : nullptr;
	}
	return GetOwner();
}

double UMDStatusEffectComponent::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

const FMDStatusRep* UMDStatusEffectComponent::FindRep(EMDStatusType Type) const
{
	return Statuses.FindByPredicate([Type](const FMDStatusRep& E) { return E.Type == Type; });
}

int32 UMDStatusEffectComponent::GetStacks(EMDStatusType Type) const
{
	const FMDStatusRep* Rep = FindRep(Type);
	return Rep ? Rep->Stacks : 0;
}

float UMDStatusEffectComponent::GetRemainingSeconds(EMDStatusType Type) const
{
	const FMDStatusRep* Rep = FindRep(Type);
	return Rep ? FMath::Max(0.0f, static_cast<float>(Rep->EndServerTime - GetServerNow())) : 0.0f;
}

bool UMDStatusEffectComponent::ApplyStatus(EMDStatusType Type, AActor* Instigator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Type == EMDStatusType::None || Type == EMDStatusType::Count)
	{
		return false;
	}
	UMDAbilitySystemComponent* ASC = GetASC();
	if (ASC && ASC->IsDead())
	{
		return false;
	}
	if (ASC && !bDeathBound)
	{
		ASC->OnDeath.AddUniqueDynamic(this, &UMDStatusEffectComponent::HandleOwnerDeath);
		bDeathBound = true;
	}

	if (FMDStatusRules::Apply(Active, Type, Instigator))
	{
		AddTagEffect(Type);
	}
	SyncReplicated();
	UpdateTickEnabled();
	return true;
}

void UMDStatusEffectComponent::ClearStatus(EMDStatusType Type)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (Active.RemoveAll([Type](const FMDStatusRuntime& E) { return E.Type == Type; }) > 0)
	{
		RemoveTagEffect(Type);
		SyncReplicated();
		UpdateTickEnabled();
	}
}

void UMDStatusEffectComponent::ClearAll()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	TArray<EMDStatusType> Types;
	for (const FMDStatusRuntime& Entry : Active)
	{
		Types.Add(Entry.Type);
	}
	Active.Reset();
	for (const EMDStatusType Type : Types)
	{
		RemoveTagEffect(Type);
	}
	SyncReplicated();
	UpdateTickEnabled();
}

void UMDStatusEffectComponent::HandleOwnerDeath()
{
	ClearAll();
}

void UMDStatusEffectComponent::AdvanceStatuses(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Active.IsEmpty())
	{
		return;
	}
	TArray<FMDStatusTick> Ticks;
	TArray<EMDStatusType> Expired;
	FMDStatusRules::Advance(Active, DeltaSeconds, Ticks, &Expired);

	for (const EMDStatusType Type : Expired)
	{
		RemoveTagEffect(Type);
	}
	if (Expired.Num() > 0)
	{
		SyncReplicated();
	}

	// Ticks last: a lethal one fires OnDeath, which clears whatever is left (the arrays above are already settled).
	if (Ticks.Num() > 0)
	{
		const UMDAbilitySystemComponent* ASC = GetASC();
		AActor* Avatar = GetAvatar();
		if (ASC && Avatar)
		{
			for (const FMDStatusTick& Tick : Ticks)
			{
				const float Damage = ASC->GetMaxHealth() * Tick.Fraction;
				UMDCombatStatics::ApplyTypedDamage(Tick.Instigator.Get(), Avatar, Damage, FMDStatusRules::GetDamageType(Tick.Type));
			}
		}
	}
	UpdateTickEnabled();
}

void UMDStatusEffectComponent::SyncReplicated()
{
	const double Now = GetServerNow();
	TArray<FMDStatusRep> Next;
	Next.Reserve(Active.Num());
	for (const FMDStatusRuntime& Entry : Active)
	{
		FMDStatusRep& Rep = Next.AddDefaulted_GetRef();
		Rep.Type = Entry.Type;
		Rep.Stacks = static_cast<uint8>(FMath::Clamp(Entry.Stacks, 0, 255));
		Rep.Duration = Entry.Duration;
		Rep.EndServerTime = Now + Entry.Remaining;
	}
	bool bSame = Next.Num() == Statuses.Num();
	for (int32 Index = 0; bSame && Index < Next.Num(); ++Index)
	{
		bSame = Next[Index].IsSameAs(Statuses[Index]);
	}
	if (bSame)
	{
		return;
	}
	Statuses = MoveTemp(Next);
	OnStatusesChanged.Broadcast(); // the server's own machine has no OnRep
}

void UMDStatusEffectComponent::OnRep_Statuses()
{
	UpdateTickEnabled();
	OnStatusesChanged.Broadcast();
}

void UMDStatusEffectComponent::UpdateTickEnabled()
{
	const bool bNeedTick = !Statuses.IsEmpty() || !Active.IsEmpty();
	if (!bNeedTick)
	{
		ApplyMovementScale(); // one last pass puts the pawn's speed back before the tick goes to sleep
	}
	if (IsRegistered())
	{
		SetComponentTickEnabled(bNeedTick);
	}
}

void UMDStatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		AdvanceStatuses(FMath::Min(DeltaTime, 1.0f));
	}
	ApplyMovementScale();
}

void UMDStatusEffectComponent::ApplyMovementScale()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatar());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (SlowedMovement.Get() != Move)
	{
		// New pawn (respawn): whatever speed we scaled on the old one is gone with it.
		SlowedMovement = Move;
		bSpeedScaled = false;
		if (Move)
		{
			// Run after the pawn's own tick (the movement Blueprint sets MaxWalkSpeed there) and before the movement component uses it.
			AddTickPrerequisiteActor(Character);
			Move->AddTickPrerequisiteComponent(this);
		}
	}
	if (!Move)
	{
		return;
	}

	// Frost slow. Gameplay code owns MaxWalkSpeed (the character Blueprint rewrites it as gait changes), so we scale on top of it each frame:
	// if the value is still the one we wrote, the base is the one we remembered; otherwise the game has set a new base.
	const float Scale = GetMoveSpeedScale();
	const bool bUntouched = bSpeedScaled && FMath::IsNearlyEqual(Move->MaxWalkSpeed, LastWrittenSpeed, 0.01f);
	if (Scale < 1.0f - KINDA_SMALL_NUMBER)
	{
		const float Base = bUntouched ? LastBaseSpeed : Move->MaxWalkSpeed;
		Move->MaxWalkSpeed = Base * Scale;
		LastBaseSpeed = Base;
		LastWrittenSpeed = Move->MaxWalkSpeed;
		bSpeedScaled = true;
	}
	else if (bSpeedScaled)
	{
		if (bUntouched)
		{
			Move->MaxWalkSpeed = LastBaseSpeed;
		}
		bSpeedScaled = false;
	}
}

void UMDStatusEffectComponent::AddTagEffect(EMDStatusType Type)
{
	UMDAbilitySystemComponent* ASC = GetASC();
	const TSubclassOf<UGameplayEffect> EffectClass = TagEffectClass(Type);
	if (!ASC || !EffectClass || TagHandles.Contains(Type))
	{
		return;
	}
	const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.0f, ASC->MakeEffectContext());
	if (Handle.IsValid())
	{
		TagHandles.Add(Type, Handle);
	}
}

void UMDStatusEffectComponent::RemoveTagEffect(EMDStatusType Type)
{
	FActiveGameplayEffectHandle Handle;
	if (TagHandles.RemoveAndCopyValue(Type, Handle))
	{
		if (UMDAbilitySystemComponent* ASC = GetASC())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
}
