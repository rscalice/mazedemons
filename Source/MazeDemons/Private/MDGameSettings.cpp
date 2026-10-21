#include "MDGameSettings.h"

#include "MDBalanceData.h"
#include "MDPerkTypes.h"
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

const FMDPerkTuning& UMDGameSettings::GetPerkTuning()
{
	static const FMDPerkTuning Defaults;
	const UMDBalanceData* Data = GetBalanceData();
	return Data ? Data->Perks : Defaults;
}

const FMDBalanceReference& UMDGameSettings::GetBalanceReference()
{
	static const FMDBalanceReference Defaults;
	const UMDBalanceData* Data = GetBalanceData();
	return Data ? Data->Reference : Defaults;
}

const UMDPerkTree* UMDGameSettings::GetPerkTree()
{
	const UMDGameSettings* Settings = GetDefault<UMDGameSettings>();
	if (Settings && !Settings->PerkTree.IsNull())
	{
		if (Settings->PerkTree.IsValid())
		{
			return Settings->PerkTree.Get();
		}

		// Same throttle as the balance asset: DA_PerkTree is created in the editor after this code ships.
		static double NextCheckTime = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextCheckTime)
		{
			if (FPackageName::DoesPackageExist(Settings->PerkTree.ToSoftObjectPath().GetLongPackageName()))
			{
				if (const UMDPerkTree* Loaded = Settings->PerkTree.LoadSynchronous())
				{
					return Loaded;
				}
			}
			NextCheckTime = Now + 5.0;
		}
	}
	return GetDefault<UMDPerkTree>();
}

FMDEconomyTuning UMDGameSettings::GetEconomyTuning()
{
	return GetEconomy();
}
