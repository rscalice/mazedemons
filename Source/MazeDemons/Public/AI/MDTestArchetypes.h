#pragma once

#include "CoreMinimal.h"

class UMDEnemyArchetype;
class UObject;

// V1 WP5 test archetypes. There is no Bone Archer, Brute, Werewolf or Cultist asset yet (content is V2 / WP10), so the
// behaviours are exercised with archetypes built in code: the ghoul's attack set plus the behaviour switches. They exist only at
// runtime (not saved) and only resolve on the machine that built them: use them in tests and in PIE on a listen server host.
namespace MDTestArchetypes
{
	enum class EKind : uint8
	{
		Archer,   // kiter: PreferredRange 9 m, RangedData = DA_Ranged_HollowPistol, no melee
		Brute,    // bCanCharge, PunishGreedHits 3, heavy swing; allowed Guard / Striker
		Werewolf, // bCanBlink
		Caster    // bIsCaster: screened by Guards
	};

	// Builds an archetype of Kind (owned by Outer). The melee attack set of a plain enemy is a single 200 cm swipe.
	MAZEDEMONS_API UMDEnemyArchetype* Make(UObject* Outer, EKind Kind);

	// Parses "archer" / "brute" / "werewolf" / "caster" (case-insensitive).
	MAZEDEMONS_API bool ParseKind(const FString& Text, EKind& OutKind);
}
