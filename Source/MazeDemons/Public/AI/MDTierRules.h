#pragma once

#include "CoreMinimal.h"
#include "AI/MDAITypes.h"

// V1 WP4 intelligence tier rules (v1-design 3.1). Plain functions, no world: the pack director gathers the inputs at a pack's
// spawn (UMDPackDirectorSubsystem::ComputeTier) and these turn them into a tier.
//
//   DepthScore = fixed layout: zone 1..4 -> 0.1 / 0.4 / 0.7 / 1.0 (the PDG RoomDepth / MaxRoomDepth replaces it later)
//   PartyScore = 0.7 * clamp(AvgPerksSpent / 8) + 0.3 * clamp(AvgUpgradeLevel / 3)
//                (the design's AvgPartyLevel and AvgGearScore: there is no level system yet, so the level term is the perk points
//                 each player has spent and the gear term is the average upgrade level of the equipped items, max +3)
//   SizeScore  = (Players - 1) / 3
//   ITValue    = 0.55 * Depth + 0.30 * Party + 0.15 * Size + Modifier + seeded jitter (+/- 0.05)
//   Tier       = T0 < 0.15 <= T1 < 0.35 <= T2 < 0.60 <= T3 < 0.85 <= T4, then the archetype's MinTier / MaxTier clamp
namespace MDTier
{
	constexpr float DepthWeight = 0.55f;
	constexpr float PartyWeight = 0.30f;
	constexpr float SizeWeight = 0.15f;

	// PartyScore split between the "level" term and the "gear" term (3.1: 0.7 / 0.3).
	constexpr float PartyLevelWeight = 0.7f;
	constexpr float PartyGearWeight = 0.3f;

	// Perk points spent that count as a maxed "level" term, and the upgrade level that counts as maxed gear (MaxUpgradeLevel = 3).
	constexpr float PerksForMaxParty = 8.0f;
	constexpr float UpgradeForMaxParty = 3.0f;

	// The run seed adds this much +/- to ITValue so two packs in one room can differ by a tier.
	constexpr float JitterAmplitude = 0.05f;

	// Tier boundaries: ITValue below [0] is T0, below [1] T1, below [2] T2, below [3] T3, else T4.
	constexpr float Thresholds[4] = { 0.15f, 0.35f, 0.60f, 0.85f };

	// Fixed-layout depth: zone index 1..4 -> 0.1 / 0.4 / 0.7 / 1.0; 0 (no zone) or anything else -> 0.
	MAZEDEMONS_API float DepthScore(int32 ZoneIndex);

	MAZEDEMONS_API float PartyScore(float AvgPerksSpent, float AvgUpgradeLevel);

	// 0 for a solo player (and for no players), 1 for four or more.
	MAZEDEMONS_API float SizeScore(int32 NumPlayers);

	MAZEDEMONS_API float ITValue(float Depth, float Party, float Size, float Modifier);

	MAZEDEMONS_API EMDIntelligenceTier TierFromValue(float Value);

	// Min <= result <= Max (a swapped Min / Max is treated as the range between them, like UMDEnemyArchetype::AllowsTier).
	MAZEDEMONS_API EMDIntelligenceTier ClampTier(EMDIntelligenceTier Tier, EMDIntelligenceTier Min, EMDIntelligenceTier Max);

	// Deterministic jitter in [-JitterAmplitude, +JitterAmplitude] for a (run seed, pack id).
	MAZEDEMONS_API float Jitter(int32 RunSeed, int32 PackId);

	struct FInputs
	{
		// AMDMapZone::ZoneIndex of the zone the pack spawns in; 0 = not in a zone.
		int32 ZoneIndex = 0;
		int32 NumPlayers = 0;
		float AvgPerksSpent = 0.0f;
		float AvgUpgradeLevel = 0.0f;
		// Zone / boss / room tag modifier (3.1); 0 for now.
		float Modifier = 0.0f;
		int32 RunSeed = 0;
		int32 PackId = 0;
		EMDIntelligenceTier MinTier = EMDIntelligenceTier::T0;
		EMDIntelligenceTier MaxTier = EMDIntelligenceTier::T4;
	};

	struct FResult
	{
		float Depth = 0.0f;
		float Party = 0.0f;
		float Size = 0.0f;
		float Jitter = 0.0f;
		// ITValue including the jitter: the number the debug overlay prints.
		float Value = 0.0f;
		// Tier after the archetype clamp.
		EMDIntelligenceTier Tier = EMDIntelligenceTier::T0;
	};

	MAZEDEMONS_API FResult Compute(const FInputs& In);
}
