#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

// SetByCaller tag carrying raw damage into UMDDamageEffect.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);

// Stamina state, kept as loose tags on the server by UMDStaminaComponent. Attacks never check these.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Exhausted);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Sprinting);
