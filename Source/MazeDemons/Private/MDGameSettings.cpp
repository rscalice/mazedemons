#include "MDGameSettings.h"

#include "MDBalanceData.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

const UMDBalanceData* UMDGameSettings::GetBalanceData()
{
	const UMDGameSettings* Settings = GetDefault<UMDGameSettings>();
	if (!Settings || Settings->BalanceData.IsNull())
	{
		return nullptr;
	}
	if (Settings->BalanceData.IsValid())
	{
		return Settings->BalanceData.Get();
	}

	// The asset is created in the editor after this code ships: do not try (and log) a failing load on every call.
	static double NextCheckTime = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextCheckTime)
	{
		return nullptr;
	}
	if (!FPackageName::DoesPackageExist(Settings->BalanceData.ToSoftObjectPath().GetLongPackageName()))
	{
		NextCheckTime = Now + 5.0;
		return nullptr;
	}
	return Settings->BalanceData.LoadSynchronous();
}

const FMDEconomyTuning& UMDGameSettings::GetEconomy()
{
	static const FMDEconomyTuning Defaults;
	const UMDBalanceData* Data = GetBalanceData();
	return Data ? Data->Economy : Defaults;
}
