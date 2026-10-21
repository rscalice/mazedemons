#include "MDPerkEffect.h"

#include "MDAttributeSet.h"
#include "MDGameplayTags.h"

UMDPerkEffect::UMDPerkEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	auto AddModifier = [this](const FGameplayAttribute& Attribute, const FGameplayTag& Tag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = Tag;

		FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	};

	AddModifier(UMDAttributeSet::GetMaxHealthAttribute(), TAG_Data_Perk_MaxHealth.GetTag());
	AddModifier(UMDAttributeSet::GetMaxStaminaAttribute(), TAG_Data_Perk_MaxStamina.GetTag());
	AddModifier(UMDAttributeSet::GetStaminaRegenRateAttribute(), TAG_Data_Perk_StaminaRegen.GetTag());
	AddModifier(UMDAttributeSet::GetCritChanceAttribute(), TAG_Data_Perk_CritChance.GetTag());
	AddModifier(UMDAttributeSet::GetCritDamageAttribute(), TAG_Data_Perk_CritDamage.GetTag());
	AddModifier(UMDAttributeSet::GetHealthRegenAttribute(), TAG_Data_Perk_HealthRegen.GetTag());
	AddModifier(UMDAttributeSet::GetStaminaCostScaleAttribute(), TAG_Data_Perk_StaminaCostScale.GetTag());
	AddModifier(UMDAttributeSet::GetSprintSpeedScaleAttribute(), TAG_Data_Perk_SprintSpeed.GetTag());
}
