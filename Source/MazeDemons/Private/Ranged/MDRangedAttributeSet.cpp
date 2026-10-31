#include "Ranged/MDRangedAttributeSet.h"

#include "Net/UnrealNetwork.h"

UMDRangedAttributeSet::UMDRangedAttributeSet()
	: Heat(0.0f)
	, MaxHeat(100.0f)
	, Charge(100.0f)
	, MaxCharge(100.0f)
{
}

void UMDRangedAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDRangedAttributeSet, Heat, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDRangedAttributeSet, MaxHeat, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDRangedAttributeSet, Charge, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDRangedAttributeSet, MaxCharge, COND_OwnerOnly, REPNOTIFY_Always);
}

void UMDRangedAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetHeatAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(GetMaxHeat(), 1.0f));
	}
	else if (Attribute == GetChargeAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(GetMaxCharge(), 1.0f));
	}
	else if (Attribute == GetMaxHeatAttribute() || Attribute == GetMaxChargeAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UMDRangedAttributeSet::OnRep_Heat(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDRangedAttributeSet, Heat, OldValue);
}

void UMDRangedAttributeSet::OnRep_MaxHeat(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDRangedAttributeSet, MaxHeat, OldValue);
}

void UMDRangedAttributeSet::OnRep_Charge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDRangedAttributeSet, Charge, OldValue);
}

void UMDRangedAttributeSet::OnRep_MaxCharge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDRangedAttributeSet, MaxCharge, OldValue);
}
