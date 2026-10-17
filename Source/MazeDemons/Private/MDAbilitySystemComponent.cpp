#include "MDAbilitySystemComponent.h"

#include "MDAttributeSet.h"
#include "MDDamageEffect.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDAbilitySystem, Log, All);

UMDAbilitySystemComponent::UMDAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

UMDAttributeSet* UMDAbilitySystemComponent::GetMazeDemonsAttributes() const
{
	if (MazeDemonsAttributes)
	{
		return MazeDemonsAttributes;
	}
	return const_cast<UMDAttributeSet*>(GetSet<UMDAttributeSet>());
}

void UMDAbilitySystemComponent::RegisterAttributeSet(UMDAttributeSet* InSet)
{
	MazeDemonsAttributes = InSet;
	if (InSet && !GetSet<UMDAttributeSet>())
	{
		AddAttributeSetSubobject(InSet);
	}
}

void UMDAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	BindAttributeDelegates();
}

void UMDAbilitySystemComponent::ResetHealthToMax()
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetHealth(Attrs->GetMaxHealth());
	}
}

float UMDAbilitySystemComponent::GetStamina() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetStamina() : 0.0f;
}

float UMDAbilitySystemComponent::GetMaxStamina() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetMaxStamina() : 0.0f;
}

float UMDAbilitySystemComponent::GetStaminaRegenRate() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetStaminaRegenRate() : 0.0f;
}

void UMDAbilitySystemComponent::InitializeStamina(float NewMaxStamina, float NewRegenRate)
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetMaxStamina(FMath::Max(NewMaxStamina, 1.0f));
		Attrs->SetStaminaRegenRate(FMath::Max(NewRegenRate, 0.0f));
		Attrs->SetStamina(Attrs->GetMaxStamina());
	}
}

void UMDAbilitySystemComponent::ResetStaminaToMax()
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetStamina(Attrs->GetMaxStamina());
	}
}

void UMDAbilitySystemComponent::SetStaminaClamped(float NewStamina)
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetStamina(FMath::Clamp(NewStamina, 0.0f, Attrs->GetMaxStamina()));
	}
}

float UMDAbilitySystemComponent::GetHealth() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetMaxHealth() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetMaxHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetAttackPower() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetAttackPower() : 0.0f;
}

float UMDAbilitySystemComponent::GetDefense() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetDefense() : 0.0f;
}

void UMDAbilitySystemComponent::SetArmorPieceDamageReduction(FName ArmorSlot, float DamageReductionPercent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Add(ArmorSlot, FMath::Clamp(DamageReductionPercent, 0.0f, 90.0f));
	RecalculateDefense();
}

void UMDAbilitySystemComponent::RemoveArmorPieceDamageReduction(FName ArmorSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Remove(ArmorSlot);
	RecalculateDefense();
}

float UMDAbilitySystemComponent::ComputeAttackPower(float InBaseAttackPower, const TMap<FName, float>& WeaponBonuses)
{
	// A two-handed weapon occupies both hands, so its bonus replaces the one-handed slots.
	static const FName TwoHanded(TEXT("TwoHanded"));
	static const FName MainHand(TEXT("MainHand"));
	static const FName OffHand(TEXT("OffHand"));
	const bool bTwoHanded = WeaponBonuses.Contains(TwoHanded);

	float Total = InBaseAttackPower;
	for (const TPair<FName, float>& Bonus : WeaponBonuses)
	{
		if (bTwoHanded && (Bonus.Key == MainHand || Bonus.Key == OffHand))
		{
			continue;
		}
		Total += Bonus.Value;
	}
	return FMath::Max(Total, 0.0f);
}

void UMDAbilitySystemComponent::SetWeaponAttackBonus(FName WeaponSlot, float AttackPowerBonus)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || WeaponSlot.IsNone())
	{
		return;
	}

	EquippedWeaponAttackBonuses.Add(WeaponSlot, AttackPowerBonus);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::RemoveWeaponAttackBonus(FName WeaponSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || WeaponSlot.IsNone())
	{
		return;
	}

	EquippedWeaponAttackBonuses.Remove(WeaponSlot);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::RecalculateAttackPower()
{
	if (UMDAttributeSet* Attrs = GetMazeDemonsAttributes())
	{
		Attrs->SetAttackPower(ComputeAttackPower(BaseAttackPower, EquippedWeaponAttackBonuses));
	}
}

void UMDAbilitySystemComponent::OnRep_BaseStats()
{
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::ApplyDamage(float Damage, AActor* DamageInstigator, AActor* DamageCauser)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogMDAbilitySystem, Verbose, TEXT("ApplyDamage ignored on non-authority."));
		return;
	}
	if (Damage <= 0.0f || IsDead())
	{
		return;
	}

	AActor* Target = GetAvatarActor();
	if (!Target)
	{
		Target = GetOwner();
	}
	if (!UMDGameplayStatics::CanDamage(DamageInstigator, Target))
	{
		UE_LOG(LogMDAbilitySystem, Verbose, TEXT("ApplyDamage blocked by team rules: %s -> %s"),
			*GetNameSafe(DamageInstigator), *GetNameSafe(Target));
		return;
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddInstigator(DamageInstigator, DamageCauser ? DamageCauser : DamageInstigator);

	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UMDDamageEffect::StaticClass(), 1.0f, Context);
	if (Spec.IsValid() && Spec.Data.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Damage.GetTag(), Damage);
		ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

void UMDAbilitySystemComponent::ApplyHealing(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Amount > 0.0f && Attrs && !IsDead())
	{
		Attrs->SetHealth(FMath::Clamp(Attrs->GetHealth() + Amount, 0.0f, Attrs->GetMaxHealth()));
	}
}

void UMDAbilitySystemComponent::InitializeStats(float NewMaxHealth, float NewAttackPower, float NewBaseDefense)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (!Attrs)
	{
		return;
	}

	BaseDefense = FMath::Clamp(NewBaseDefense, 0.0f, 90.0f);
	Attrs->SetMaxHealth(FMath::Max(NewMaxHealth, 1.0f));
	Attrs->SetHealth(Attrs->GetMaxHealth());
	BaseAttackPower = FMath::Max(NewAttackPower, 0.0f);
	RecalculateAttackPower();
	RecalculateDefense();
	OnStatsChanged.Broadcast();
}

FGameplayAbilitySpecHandle UMDAbilitySystemComponent::GrantAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	if (!AbilityClass || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return FGameplayAbilitySpecHandle();
	}
	return GiveAbility(FGameplayAbilitySpec(AbilityClass, Level, INDEX_NONE, GetOwner()));
}

bool UMDAbilitySystemComponent::IsDead() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs && Attrs->GetHealth() <= 0.0f;
}

void UMDAbilitySystemComponent::RecalculateDefense()
{
	float RemainingDamageFraction = 1.0f - BaseDefense / 100.0f;
	for (const TPair<FName, float>& ArmorPiece : EquippedArmorDamageReductions)
	{
		RemainingDamageFraction *= 1.0f - ArmorPiece.Value / 100.0f;
	}

	if (UMDAttributeSet* Attrs = GetMazeDemonsAttributes())
	{
		Attrs->SetDefense(FMath::Clamp((1.0f - RemainingDamageFraction) * 100.0f, 0.0f, 90.0f));
	}
}

void UMDAbilitySystemComponent::BindAttributeDelegates()
{
	if (bDelegatesBound)
	{
		return;
	}
	bDelegatesBound = true;

	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleHealthChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleMaxHealthChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetAttackPowerAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleAttackPowerChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetDefenseAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleDefenseChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetStaminaAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleStaminaChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetMaxStaminaAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleMaxStaminaChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetStaminaRegenRateAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleStaminaRegenRateChanged);
}

void UMDAbilitySystemComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();

	// Death state is server-authoritative; clients receive it through bIsDeadState (RepNotify).
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (ChangeData.NewValue <= 0.0f && !bIsDeadState)
		{
			bIsDeadState = true;
			OnDeath.Broadcast();
		}
		else if (ChangeData.NewValue > 0.0f && bIsDeadState)
		{
			bIsDeadState = false; // respawn / revive
		}
	}
}

void UMDAbilitySystemComponent::OnRep_IsDeadState(bool bOldValue)
{
	if (bIsDeadState && !bOldValue)
	{
		OnDeath.Broadcast();
	}
}

void UMDAbilitySystemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDAbilitySystemComponent, bIsDeadState);
	DOREPLIFETIME_CONDITION(UMDAbilitySystemComponent, BaseDefense, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDAbilitySystemComponent, BaseAttackPower, COND_OwnerOnly);
}

void UMDAbilitySystemComponent::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnMaxHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData)
{
	OnAttackPowerChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleDefenseChanged(const FOnAttributeChangeData& ChangeData)
{
	OnDefenseChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	OnStaminaChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleStaminaRegenRateChanged(const FOnAttributeChangeData& ChangeData)
{
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleMaxStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	OnMaxStaminaChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();

	// A lowered maximum pulls current stamina down with it (authority only; clients receive the result).
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SetStaminaClamped(GetStamina());
	}
}
