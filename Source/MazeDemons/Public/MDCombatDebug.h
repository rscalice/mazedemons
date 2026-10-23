#pragma once

#include "CoreMinimal.h"

// Combat debug console variables (md.Combat.*). Read-only accessors; the commands MDSetPoise and MDStun live in MDCombatDebug.cpp.
namespace MDCombatDebug
{
	// md.Combat.Debug: on-screen combo step, windows, buffer, target poise and last reaction.
	MAZEDEMONS_API bool IsDebugEnabled();

	// md.Combat.DrawTraces: draw weapon traces and impact points.
	MAZEDEMONS_API bool ShouldDrawTraces();

	// md.Combat.HitStopScale: multiplier on hit-stop length (0 disables it).
	MAZEDEMONS_API float HitStopScale();

	// md.Combat.ShakeScale: multiplier on camera shake.
	MAZEDEMONS_API float ShakeScale();

	// md.Combat.ForceReaction: 0 = off, else every landed hit resolves to this EMDHitReaction value (1 Flinch .. 4 Knockdown).
	MAZEDEMONS_API int32 ForceReaction();
}
