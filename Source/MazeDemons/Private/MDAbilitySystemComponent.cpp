#include "MDAbilitySystemComponent.h"

#include "MDAttributeSet.h"
#include "GameplayEffect.h"

UMDAbilitySystemComponent::UMDAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

UMDAttributeSet* UMDAbilitySystemComponent::GetMazeDemonsAttributes() const
{
	return MazeDemonsAttributes;
}

float UMDAbilitySystemComponent::GetHealth() const
{
	return MazeDemonsAttributes ? MazeDemonsAttributes->GetHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetMaxHealth() const
{
	return MazeDemonsAttributes ? MazeDemonsAttributes->GetMaxHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetAttackPower() const
{
	return MazeDemonsAttributes ? MazeDemonsAttributes->GetAttackPower() : 0.0f;
}

float UMDAbilitySystemComponent::GetDefense() const
{
	return MazeDemonsAttributes ? MazeDemonsAttributes->GetDefense() : 0.0f;
}

void UMDAbilitySystemComponent::SetArmorPieceDamageReduction(FName ArmorSlot, float DamageReductionPercent)
{
	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Add(ArmorSlot, FMath::Clamp(DamageReductionPercent, 0.0f, 90.0f));
	RecalculateDefense();
}

void UMDAbilitySystemComponent::RemoveArmorPieceDamageReduction(FName ArmorSlot)
{
	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Remove(ArmorSlot);
	RecalculateDefense();
}

void UMDAbilitySystemComponent::ApplyDamage(float Damage)
{
	if (Damage <= 0.0f || !MazeDemonsAttributes || IsDead())
	{
		return;
	}

	UGameplayEffect* DamageEffect = NewObject<UGameplayEffect>(GetTransientPackage(), TEXT("MDDamageEffect"));
	DamageEffect->DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo& Modifier = DamageEffect->Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UMDAttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Damage));

	ApplyGameplayEffectToSelf(DamageEffect, 1.0f, MakeEffectContext());
}

void UMDAbilitySystemComponent::ApplyHealing(float Amount)
{
	if (Amount > 0.0f && MazeDemonsAttributes && !IsDead())
	{
		MazeDemonsAttributes->SetHealth(FMath::Clamp(MazeDemonsAttributes->GetHealth() + Amount, 0.0f, MazeDemonsAttributes->GetMaxHealth()));
	}
}

void UMDAbilitySystemComponent::InitializeStats(float NewMaxHealth, float NewAttackPower, float NewBaseDefense)
{
	if (!MazeDemonsAttributes)
	{
		return;
	}

	BaseDefense = FMath::Clamp(NewBaseDefense, 0.0f, 90.0f);
	MazeDemonsAttributes->SetMaxHealth(FMath::Max(NewMaxHealth, 1.0f));
	MazeDemonsAttributes->SetHealth(MazeDemonsAttributes->GetMaxHealth());
	MazeDemonsAttributes->SetAttackPower(FMath::Max(NewAttackPower, 0.0f));
	RecalculateDefense();
}

FGameplayAbilitySpecHandle UMDAbilitySystemComponent::GrantAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	if (!AbilityClass)
	{
		return FGameplayAbilitySpecHandle();
	}
	return GiveAbility(FGameplayAbilitySpec(AbilityClass, Level, INDEX_NONE, GetOwner()));
}

bool UMDAbilitySystemComponent::IsDead() const
{
	return MazeDemonsAttributes && MazeDemonsAttributes->GetHealth() <= 0.0f;
}

void UMDAbilitySystemComponent::RecalculateDefense()
{
	float RemainingDamageFraction = 1.0f - BaseDefense / 100.0f;
	for (const TPair<FName, float>& ArmorPiece : EquippedArmorDamageReductions)
	{
		RemainingDamageFraction *= 1.0f - ArmorPiece.Value / 100.0f;
	}

	if (MazeDemonsAttributes)
	{
		MazeDemonsAttributes->SetDefense(FMath::Clamp((1.0f - RemainingDamageFraction) * 100.0f, 0.0f, 90.0f));
	}
}

void UMDAbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		MazeDemonsAttributes = NewObject<UMDAttributeSet>(Owner, TEXT("MazeDemonsAttributes"));
		AddAttributeSetSubobject(MazeDemonsAttributes.Get());
		InitAbilityActorInfo(Owner, Owner);
		GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UMDAbilitySystemComponent::HandleHealthChanged);
		GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetMaxHealthAttribute())
			.AddUObject(this, &UMDAbilitySystemComponent::HandleMaxHealthChanged);
		GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetAttackPowerAttribute())
			.AddUObject(this, &UMDAbilitySystemComponent::HandleAttackPowerChanged);
		GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetDefenseAttribute())
			.AddUObject(this, &UMDAbilitySystemComponent::HandleDefenseChanged);
	}
}

void UMDAbilitySystemComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	if (ChangeData.NewValue <= 0.0f && ChangeData.OldValue > 0.0f)
	{
		OnDeath.Broadcast();
	}
}

void UMDAbilitySystemComponent::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnMaxHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
}

void UMDAbilitySystemComponent::HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData)
{
	OnAttackPowerChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
}

void UMDAbilitySystemComponent::HandleDefenseChanged(const FOnAttributeChangeData& ChangeData)
{
	OnDefenseChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
}
