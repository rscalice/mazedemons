#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class AActor;

// The one place that decides what a floating damage number looks like and who gets one.
//
// Damage type: a gameplay tag under Damage.Type (TAG_Damage_Type_*). An empty or unknown tag is Physical, so every existing hit
// stays white. Anything that deals damage passes its type through FMDHitContext::DamageType (melee steps through
// FMDAttackStep::DamageType, status effects through UMDCombatStatics::ApplyTypedDamage) and it reaches the number widget
// unchanged. Child tags map to their parent (Damage.Type.Fire.Burn shows as Fire).
//
// Colors: Physical white, Frost icy blue, Fire / Burn orange, Poison green, Bleed crimson pink. A critical hit is red and
// overrides the type color. These are the defaults; UMDDamageNumberWidget::TypeColors / CritColor override them per widget class
// (a data override on a Blueprint child of the widget, selected by UMDCombatFeedbackComponent::DamageNumberClass).
namespace MDDamageNumbers
{
	// Physical for an empty / unrecognised tag, else the matching Damage.Type.* tag (children collapse to their parent).
	MAZEDEMONS_API FGameplayTag ResolveType(const FGameplayTag& Type);

	MAZEDEMONS_API FLinearColor DefaultTypeColor(const FGameplayTag& Type);
	MAZEDEMONS_API FLinearColor DefaultCritColor();

	// Crit color wins over the type color. Overrides are optional: a type missing from the map, or a null map, uses the default.
	MAZEDEMONS_API FLinearColor ResolveColor(const FGameplayTag& Type, bool bCrit, const TMap<FGameplayTag, FLinearColor>* TypeOverrides = nullptr, const FLinearColor* CritOverride = nullptr);

	// Every creature with an ability system gets numbers (enemies, the test dummy, other players where friendly fire allows the
	// hit): the rule never looks at the victim's class. Null and plain actors do not.
	MAZEDEMONS_API bool IsEligibleVictim(const AActor* Victim);
}
