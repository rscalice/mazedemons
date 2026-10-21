#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

// SetByCaller tag carrying raw damage into UMDDamageEffect.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);

// Stamina state, kept as loose tags on the server by UMDStaminaComponent. Attacks never check these.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Exhausted);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Sprinting);

// SetByCaller tags feeding UMDPerkEffect (the single infinite perk effect). Magnitudes are deltas from the attribute base.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_MaxHealth);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_MaxStamina);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_StaminaRegen);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_CritChance);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_CritDamage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_HealthRegen);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_StaminaCostScale);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_SprintSpeed);
