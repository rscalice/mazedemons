#include "Ranged/MDRangedUIRules.h"

#include "MDDamageNumbers.h"
#include "MDGameplayTags.h"

float FMDRangedUIRules::ReticleGapPixels(float SpreadDeg, float VerticalFovDeg, float ViewportHeight, float MinGap)
{
	const float Fov = FMath::Clamp(VerticalFovDeg, 10.0f, 170.0f);
	const float PixelsPerDegree = FMath::Max(ViewportHeight, 1.0f) / Fov;
	return FMath::Max(MinGap, MinGap + FMath::Max(SpreadDeg, 0.0f) * PixelsPerDegree);
}

float FMDRangedUIRules::AnimateGap(float Current, float Target, float DeltaSeconds)
{
	const float Speed = Target > Current ? 40.0f : 14.0f;
	return FMath::FInterpTo(Current, Target, FMath::Max(DeltaSeconds, 0.0f), Speed);
}

FLinearColor FMDRangedUIRules::HitMarkerColour(bool bKill, const FGameplayTag& DamageType)
{
	if (bKill)
	{
		return MDDamageNumbers::DefaultCritColor();
	}
	const FGameplayTag Resolved = MDDamageNumbers::ResolveType(DamageType);
	if (Resolved != TAG_Damage_Type_Physical.GetTag())
	{
		return MDDamageNumbers::DefaultTypeColor(Resolved);
	}
	return FLinearColor::White;
}

EMDHeatMeterState FMDRangedUIRules::HeatMeterState(bool bBeamWeapon, bool bChargeStyle, float Value, float Max, bool bOverheated)
{
	if (!bBeamWeapon)
	{
		return EMDHeatMeterState::Hidden;
	}
	if (bOverheated)
	{
		return EMDHeatMeterState::Overheated;
	}
	if (bChargeStyle)
	{
		return EMDHeatMeterState::Charging;
	}
	const float Fraction = MeterFraction(Value, Max);
	if (Fraction >= HeatWarningFraction)
	{
		return EMDHeatMeterState::Warning;
	}
	return Fraction > 0.0f ? EMDHeatMeterState::Heating : EMDHeatMeterState::Idle;
}

float FMDRangedUIRules::HeatRingSweep(EMDHeatMeterState State, float MeterFractionValue, float LockFraction)
{
	switch (State)
	{
	case EMDHeatMeterState::Hidden:
		return 0.0f;
	case EMDHeatMeterState::Overheated:
		return FMath::Clamp(LockFraction, 0.0f, 1.0f);
	default:
		return FMath::Clamp(MeterFractionValue, 0.0f, 1.0f);
	}
}

EMDColor FMDRangedUIRules::HeatMeterToken(EMDHeatMeterState State)
{
	switch (State)
	{
	case EMDHeatMeterState::Warning:
	case EMDHeatMeterState::Overheated:
		return EMDColor::Danger;
	case EMDHeatMeterState::Charging:
		return EMDColor::Upgrade;
	default:
		return EMDColor::Warning;
	}
}

float FMDRangedUIRules::MeterFraction(float Value, float Max)
{
	return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(Value / Max, 0.0f, 1.0f) : 0.0f;
}

EMDAmmoPrompt FMDRangedUIRules::AmmoPrompt(int32 Mag, int32 Reserve, bool bReloading)
{
	if (Mag > 0 || bReloading)
	{
		return EMDAmmoPrompt::None;
	}
	return Reserve > 0 ? EMDAmmoPrompt::Reload : EMDAmmoPrompt::NoAmmo;
}

float FMDRangedUIRules::StepOpacity(float Current, float Target, float DeltaSeconds)
{
	const float Seconds = Target > Current ? CounterFadeInSeconds : CounterFadeOutSeconds;
	const float Step = FMath::Max(DeltaSeconds, 0.0f) / FMath::Max(Seconds, KINDA_SMALL_NUMBER);
	return Target > Current ? FMath::Min(Current + Step, Target) : FMath::Max(Current - Step, Target);
}
