#include "Ranged/MDHUDOptions.h"

#include "HAL/IConsoleManager.h"
#include "MDUISettings.h"
#include "Ranged/MDRangedUIRules.h"

namespace
{
	void SaveSettings()
	{
		GetMutableDefault<UMDLocalSettings>()->SaveConfig();
	}
}

void UMDHUDOptions::SetAimAssistStrength(float Strength01)
{
	GetMutableDefault<UMDLocalSettings>()->AimAssistStrength = FMath::Clamp(Strength01, 0.0f, 1.0f);
	SaveSettings();
}

void UMDHUDOptions::SetAimToggle(bool bToggle)
{
	GetMutableDefault<UMDLocalSettings>()->bAimToggle = bToggle;
	SaveSettings();
}

void UMDHUDOptions::SetReticleScale(float Scale)
{
	GetMutableDefault<UMDLocalSettings>()->ReticleScale = FMDRangedUIRules::ClampReticleScale(Scale);
	SaveSettings();
}

void UMDHUDOptions::SetAutoReload(bool bAuto)
{
	GetMutableDefault<UMDLocalSettings>()->bAutoReload = bAuto;
	SaveSettings();
}

void UMDHUDOptions::SetOffscreenThreat(bool bEnabled)
{
	GetMutableDefault<UMDLocalSettings>()->bOffscreenThreat = bEnabled;
	SaveSettings();
	ApplySavedOptions();
}

void UMDHUDOptions::SetLockOnAssistStrength(float Strength01)
{
	GetMutableDefault<UMDLocalSettings>()->LockOnAssistStrength = FMath::Clamp(Strength01, 0.0f, 1.0f);
	SaveSettings();
}

void UMDHUDOptions::SetLockOnHold(bool bHold)
{
	GetMutableDefault<UMDLocalSettings>()->bLockOnHold = bHold;
	SaveSettings();
}

void UMDHUDOptions::SetLockOnReticleScale(float Scale)
{
	GetMutableDefault<UMDLocalSettings>()->LockOnReticleScale = FMath::Clamp(Scale, 0.5f, 2.0f);
	SaveSettings();
}

void UMDHUDOptions::SetLockOnAutoAcquire(bool bAuto)
{
	GetMutableDefault<UMDLocalSettings>()->bLockOnAutoAcquire = bAuto;
	SaveSettings();
}

void UMDHUDOptions::ApplySavedOptions()
{
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("md.UI.OffscreenThreat")))
	{
		Var->Set(GetDefault<UMDLocalSettings>()->bOffscreenThreat ? 1 : 0, ECVF_SetByGameSetting);
	}
}

float UMDHUDOptions::GetReticleScale()
{
	return FMDRangedUIRules::ClampReticleScale(GetDefault<UMDLocalSettings>()->ReticleScale);
}

bool UMDHUDOptions::GetAutoReload()
{
	return GetDefault<UMDLocalSettings>()->bAutoReload;
}
