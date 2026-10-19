#include "MDAttributeSet.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameSettings.h"
#include "MDPerkCatalog.h"
#include "MDPerkTypes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// BALANCE section 7 caps (the built-in defaults; they are hard limits for the attributes whatever the tree asset says).
	const FMDPerkCaps& MDAttributeCaps()
	{
		static const FMDPerkCaps Caps;
		return Caps;
	}

	// StaminaRegenRate is 20 /s at the start; Quick Breath may add at most StaminaRegenBonusCap of that.
	constexpr float MDBaseStaminaRegen = 20.0f;
}

UMDAttributeSet::UMDAttributeSet()
	: Health(100.0f)
	, MaxHealth(100.0f)
	, AttackPower(10.0f)
	, Defense(0.0f)
	, IncomingDamage(0.0f)
	, Stamina(100.0f)
	, MaxStamina(100.0f)
	, StaminaRegenRate(20.0f)
	, CritChance(0.0f)
	, CritDamage(1.5f)
	, HealthRegen(0.0f)
	, StaminaCostScale(1.0f)
	, SprintSpeedScale(1.0f)
{
}

void UMDAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, IncomingDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, StaminaRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, CritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, CritDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, HealthRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, StaminaCostScale, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDAttributeSet, SprintSpeedScale, COND_None, REPNOTIFY_Always);
}

bool UMDAttributeSet::ClampCappedAttribute(const FGameplayAttribute& Attribute, float& Value) const
{
	const FMDPerkCaps& Caps = MDAttributeCaps();
	if (Attribute == GetCritChanceAttribute())
	{
		Value = FMath::Clamp(Value, 0.0f, Caps.CritChanceCap);
	}
	else if (Attribute == GetCritDamageAttribute())
	{
		Value = FMath::Clamp(Value, 1.0f, Caps.CritDamageCap);
	}
	else if (Attribute == GetHealthRegenAttribute())
	{
		Value = FMath::Clamp(Value, 0.0f, Caps.HealthRegenCap);
	}
	else if (Attribute == GetStaminaCostScaleAttribute())
	{
		Value = FMath::Clamp(Value, Caps.StaminaCostScaleMin, 1.0f);
	}
	else if (Attribute == GetSprintSpeedScaleAttribute())
	{
		Value = FMath::Clamp(Value, 1.0f, 1.0f + Caps.SprintSpeedBonusCap);
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		Value = FMath::Clamp(Value, 0.0f, Caps.MaxStaminaCap);
	}
	else if (Attribute == GetStaminaRegenRateAttribute())
	{
		Value = FMath::Clamp(Value, 0.0f, MDBaseStaminaRegen * (1.0f + Caps.StaminaRegenBonusCap));
	}
	else
	{
		return false;
	}
	return true;
}

void UMDAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetAttackPowerAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, 90.0f);
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxStamina());
	}
	else
	{
		// Perk attributes, MaxStamina <= 150, StaminaRegenRate <= 20 * 1.45 (all also clamp below at 0 / their minimum).
		ClampCappedAttribute(Attribute, NewValue);
	}
}

void UMDAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float Damage = GetIncomingDamage();
		SetIncomingDamage(0.0f);

		if (Damage > 0.0f)
		{
			const float DamageReduction = FMath::Clamp(GetDefense(), 0.0f, 100.0f) / 100.0f;
			const float DamageAfterDR = Damage * (1.0f - DamageReduction);

			// Perk rules (Thick Hide, Bulwark, Second Wind). The state is zero / false on every ASC but a perked player's, so
			// for everyone else this is exactly Health - DamageAfterDR clamped to [0, MaxHealth].
			UMDAbilitySystemComponent* MDASC = Cast<UMDAbilitySystemComponent>(GetOwningAbilitySystemComponent());
			const FMDPerkCombatState State = MDASC ? MDASC->GetPerkCombatState() : FMDPerkCombatState();
			const FMDIncomingDamageResult Result = UMDPerkCatalog::ResolveIncomingDamage(
				DamageAfterDR, GetHealth(), GetMaxHealth(), State, UMDGameSettings::GetPerkTuning());
			SetHealth(Result.NewHealth);
			if (Result.bSecondWindTriggered && MDASC)
			{
				MDASC->ConsumeSecondWind(); // after the health write, so listeners already see 1 HP
			}
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetAttackPowerAttribute())
	{
		SetAttackPower(FMath::Max(GetAttackPower(), 0.0f));
	}
	else if (Data.EvaluatedData.Attribute == GetDefenseAttribute())
	{
		SetDefense(FMath::Clamp(GetDefense(), 0.0f, 90.0f));
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.0f, GetMaxStamina()));
	}
	else if (Data.EvaluatedData.Attribute == GetMaxStaminaAttribute())
	{
		float NewMax = GetMaxStamina();
		ClampCappedAttribute(GetMaxStaminaAttribute(), NewMax);
		SetMaxStamina(NewMax);
		SetStamina(FMath::Clamp(GetStamina(), 0.0f, GetMaxStamina()));
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaRegenRateAttribute())
	{
		float NewRegen = GetStaminaRegenRate();
		ClampCappedAttribute(GetStaminaRegenRateAttribute(), NewRegen);
		SetStaminaRegenRate(NewRegen);
	}
	else if (Data.EvaluatedData.Attribute == GetCritChanceAttribute())
	{
		float Value = GetCritChance();
		ClampCappedAttribute(GetCritChanceAttribute(), Value);
		SetCritChance(Value);
	}
	else if (Data.EvaluatedData.Attribute == GetCritDamageAttribute())
	{
		float Value = GetCritDamage();
		ClampCappedAttribute(GetCritDamageAttribute(), Value);
		SetCritDamage(Value);
	}
	else if (Data.EvaluatedData.Attribute == GetHealthRegenAttribute())
	{
		float Value = GetHealthRegen();
		ClampCappedAttribute(GetHealthRegenAttribute(), Value);
		SetHealthRegen(Value);
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaCostScaleAttribute())
	{
		float Value = GetStaminaCostScale();
		ClampCappedAttribute(GetStaminaCostScaleAttribute(), Value);
		SetStaminaCostScale(Value);
	}
	else if (Data.EvaluatedData.Attribute == GetSprintSpeedScaleAttribute())
	{
		float Value = GetSprintSpeedScale();
		ClampCappedAttribute(GetSprintSpeedScaleAttribute(), Value);
		SetSprintSpeedScale(Value);
	}
}

void UMDAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, Health, OldValue);
}

void UMDAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, MaxHealth, OldValue);
}

void UMDAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, AttackPower, OldValue);
}

void UMDAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, Defense, OldValue);
}

void UMDAttributeSet::OnRep_IncomingDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, IncomingDamage, OldValue);
}

void UMDAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, Stamina, OldValue);
}

void UMDAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, MaxStamina, OldValue);
}

void UMDAttributeSet::OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, StaminaRegenRate, OldValue);
}

void UMDAttributeSet::OnRep_CritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, CritChance, OldValue);
}

void UMDAttributeSet::OnRep_CritDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, CritDamage, OldValue);
}

void UMDAttributeSet::OnRep_HealthRegen(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, HealthRegen, OldValue);
}

void UMDAttributeSet::OnRep_StaminaCostScale(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, StaminaCostScale, OldValue);
}

void UMDAttributeSet::OnRep_SprintSpeedScale(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDAttributeSet, SprintSpeedScale, OldValue);
}
