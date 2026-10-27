#include "MDDamageNumbers.h"

#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"

FGameplayTag MDDamageNumbers::ResolveType(const FGameplayTag& Type)
{
	if (Type.IsValid())
	{
		const FGameplayTag Known[] = {
			TAG_Damage_Type_Physical.GetTag(),
			TAG_Damage_Type_Frost.GetTag(),
			TAG_Damage_Type_Fire.GetTag(),
			TAG_Damage_Type_Poison.GetTag(),
			TAG_Damage_Type_Bleed.GetTag(),
		};
		for (const FGameplayTag& Candidate : Known)
		{
			if (Type.MatchesTag(Candidate))
			{
				return Candidate;
			}
		}
	}
	return TAG_Damage_Type_Physical.GetTag();
}

FLinearColor MDDamageNumbers::DefaultTypeColor(const FGameplayTag& Type)
{
	const FGameplayTag Resolved = ResolveType(Type);
	if (Resolved == TAG_Damage_Type_Frost.GetTag())
	{
		return FLinearColor(0.45f, 0.80f, 1.00f, 1.0f);
	}
	if (Resolved == TAG_Damage_Type_Fire.GetTag())
	{
		return FLinearColor(1.00f, 0.50f, 0.10f, 1.0f);
	}
	if (Resolved == TAG_Damage_Type_Poison.GetTag())
	{
		return FLinearColor(0.40f, 0.90f, 0.25f, 1.0f);
	}
	if (Resolved == TAG_Damage_Type_Bleed.GetTag())
	{
		return FLinearColor(0.90f, 0.10f, 0.40f, 1.0f); // pinker than the crit red, so a bleeding crit-less tick never reads as a crit
	}
	return FLinearColor::White;
}

FLinearColor MDDamageNumbers::DefaultCritColor()
{
	return FLinearColor(1.00f, 0.08f, 0.05f, 1.0f);
}

FLinearColor MDDamageNumbers::ResolveColor(const FGameplayTag& Type, bool bCrit, const TMap<FGameplayTag, FLinearColor>* TypeOverrides, const FLinearColor* CritOverride)
{
	if (bCrit)
	{
		return CritOverride ? *CritOverride : DefaultCritColor();
	}
	const FGameplayTag Resolved = ResolveType(Type);
	if (TypeOverrides)
	{
		if (const FLinearColor* Override = TypeOverrides->Find(Resolved))
		{
			return *Override;
		}
	}
	return DefaultTypeColor(Resolved);
}

bool MDDamageNumbers::IsEligibleVictim(const AActor* Victim)
{
	return Victim && UMDGameplayStatics::GetMDAbilitySystemComponent(Victim) != nullptr;
}
