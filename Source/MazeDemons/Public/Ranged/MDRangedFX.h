
// R1 fix: one-shot Niagara spawns for muzzle flashes and projectile impacts. A Niagara component auto-destroys only when its system
// COMPLETES, so a system with a looping emitter (the FreeStylizedLaserBeamVFX hit / muzzle systems) lives forever and the pink clouds
// pile up. SpawnOneShot also deactivates the component after MaxSeconds and destroys it a moment later, whatever the system does.
#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UWorld;

namespace MDRangedFX
{
	// Component tag on every one-shot spawned here (the leak test counts them).
	MAZEDEMONS_API extern const FName OneShotTag;

	// Hard caps (seconds): a muzzle flash and an impact are short by nature.
	constexpr float MuzzleMaxSeconds = 0.6f;
	constexpr float ImpactMaxSeconds = 1.2f;
	// After the deactivate, how long the particles get to fade before the component is destroyed.
	constexpr float DestroyGraceSeconds = 0.5f;

	// Tags Comp as a one-shot and arms the hard cap: Deactivate after MaxSeconds, DestroyComponent DestroyGraceSeconds later.
	// SpawnOneShot calls it; it is public so a test can arm a hand-made component (no renderer needed).
	MAZEDEMONS_API void CapLifetime(UWorld* World, UNiagaraComponent* Comp, float MaxSeconds);

	// Spawns System at a world location with auto-destroy, no pooling, and the hard lifetime cap. Null when it could not spawn.
	MAZEDEMONS_API UNiagaraComponent* SpawnOneShot(UWorld* World, UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation,
		const FVector& Scale, float MaxSeconds);

	// One-shot components of this world that are still alive (not destroyed or pending destroy).
	MAZEDEMONS_API int32 CountActive(const UWorld* World);
}
