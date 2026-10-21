#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MDPerkEffect.generated.h"

// The single infinite GE carrying every perk bonus (BALANCE section 6: deterministic, one handle). Additive modifiers on
// MaxHealth, MaxStamina, StaminaRegenRate, CritChance, CritDamage, HealthRegen, StaminaCostScale and SprintSpeedScale, each driven
// by a SetByCaller "Data.Perk.*" tag. UMDPerkComponent removes the previous handle and applies one new spec on every change; it
// always sets all eight magnitudes (0 when unused) so no SetByCaller lookup ever misses.
UCLASS()
class MAZEDEMONS_API UMDPerkEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMDPerkEffect();
};
