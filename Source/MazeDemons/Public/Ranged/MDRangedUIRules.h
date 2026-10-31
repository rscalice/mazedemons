// R7: the pure rules behind the ranged HUD (ammo counter, heat / charge meter, reticle). Widgets only draw what these return, so every
// threshold is covered by automation tests without a viewport.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MDUITypes.h"

// What the heat / charge meter shows. Hidden = not a beam weapon (the meter does not exist for guns).
enum class EMDHeatMeterState : uint8
{
	Hidden,
	Idle,       // beam weapon, nothing built up
	Heating,    // heat building (below the warning line)
	Warning,    // at or above the warning line: about to overheat
	Overheated, // locked out, venting
	Charging    // charge-style beam (staff): the bar is the charge left
};

// What the ammo counter prompts under the numbers.
enum class EMDAmmoPrompt : uint8
{
	None,
	Reload,  // magazine empty and the bag has rounds
	NoAmmo   // magazine and bag both empty
};

struct MAZEDEMONS_API FMDRangedUIRules
{
	// Heat fraction at which the meter turns red.
	static constexpr float HeatWarningFraction = 0.8f;
	// Seconds the ammo counter takes to fade in on a swap to ranged mode / out on the swap back.
	static constexpr float CounterFadeInSeconds = 0.12f;
	static constexpr float CounterFadeOutSeconds = 0.08f;
	// Smallest gap (pixels) between the reticle centre and the inner end of a tick.
	static constexpr float ReticleMinGap = 6.0f;
	// Reticle size option range (UMDLocalSettings::ReticleScale).
	static constexpr float ReticleScaleMin = 0.5f;
	static constexpr float ReticleScaleMax = 2.0f;

	// Pixels from the centre to the inner end of a tick for a spread half-angle (degrees) at the vertical FOV and viewport height.
	// Monotonic in the spread; never below MinGap.
	static float ReticleGapPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap = ReticleMinGap);

	// Displayed gap chasing the target gap: opens fast (a shot's bloom shows at once), closes slower (it settles). Never overshoots.
	static float AnimateGap(float Current, float Target, float DeltaSeconds);

	// Hit marker colour: red on a kill, the element colour on a typed (non-physical) hit, white otherwise.
	static FLinearColor HitMarkerColour(bool bKill, const FGameplayTag& DamageType);

	// True when the hit shows the extra headshot tick.
	static bool ShowsHeadshotTick(bool bHeadshot) { return bHeadshot; }

	static float ClampReticleScale(float Scale) { return FMath::Clamp(Scale, ReticleScaleMin, ReticleScaleMax); }

	// Meter state. Value / Max = heat (or the charge left when bChargeStyle). bOverheated wins over everything else.
	static EMDHeatMeterState HeatMeterState(bool bBeamWeapon, bool bChargeStyle, float Value, float Max, bool bOverheated);

	// Fill token of the meter bar for a state (heat orange -> red, charge steel blue, overheat danger).
	static EMDColor HeatMeterToken(EMDHeatMeterState State);

	// Bar fill 0..1 (0 for a zero or negative max).
	static float MeterFraction(float Value, float Max);

	// R5: how much of the reticle ring is drawn (0..1 of the circle, clockwise from the top). Heat / charge fraction while the weapon
	// works; while overheated the ring is the lock still to run (LockFraction 1 -> 0), so it unwinds as the weapon cools. Hidden = 0.
	static float HeatRingSweep(EMDHeatMeterState State, float MeterFractionValue, float LockFraction);

	static EMDAmmoPrompt AmmoPrompt(int32 Mag, int32 Reserve, bool bReloading);

	// 1 while the player is in ranged mode with a gun, else 0.
	static float CounterTargetOpacity(bool bRangedMode, bool bHasGun) { return (bRangedMode && bHasGun) ? 1.0f : 0.0f; }

	// Linear step toward Target; the speed is 1 / the fade time for the direction.
	static float StepOpacity(float Current, float Target, float DeltaSeconds);
};
