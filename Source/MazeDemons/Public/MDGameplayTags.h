#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

// SetByCaller tag carrying raw damage into UMDDamageEffect.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);

// Stamina state, kept as loose tags on the server by UMDStaminaComponent. Attacks never check these.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Exhausted);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Sprinting);

// C1 combat state, kept as loose tags: Staggered while the poise component is stunned (blocks attack, heavy and dodge),
// Invulnerable during dodge i-frames (ApplyDamage skips the hit).
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Staggered);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Invulnerable);

// SetByCaller tags feeding UMDPerkEffect (the single infinite perk effect). Magnitudes are deltas from the attribute base.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_MaxHealth);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_MaxStamina);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_StaminaRegen);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_CritChance);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_CritDamage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_HealthRegen);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_StaminaCostScale);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Perk_SprintSpeed);

// U4 quests and minimap. Objective tags match markers and events; Enemy.Ghoul is on the Zone 2 kill targets.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Objective_Zone1_Shrine);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Objective_Zone3_Key);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Objective_Zone4_Altar);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_Ghoul);

// V1 enemy archetype tags (one per UMDEnemyArchetype::ArchetypeTag; kill credit and spawning match on them).
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_BoneArcher);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_GraveBrute);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_Cultist);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_Revenant);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Enemy_Werewolf);

// Minimap marker kinds (mirrors EMDMapMarkerType; usable as widget lookup keys).
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Objective);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Gate);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Station);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Checkpoint);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Exit);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Map_Marker_Player);
