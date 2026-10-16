#include "MDUIStatics.h"

#include "MDUISettings.h"
#include "MDUIStyle.h"

const UMDUIStyle* UMDUIStatics::GetStyle()
{
	if (const UMDUISettings* Settings = GetDefault<UMDUISettings>())
	{
		if (const UMDUIStyle* Asset = Settings->Style.LoadSynchronous())
		{
			return Asset;
		}
	}
	return GetDefault<UMDUIStyle>();
}

FLinearColor UMDUIStatics::GetColor(EMDColor Token)
{
	return GetStyle()->GetColor(Token);
}

FLinearColor UMDUIStatics::RarityColor(EMDRarity Rarity)
{
	return GetStyle()->RarityColor(Rarity);
}

FMDMotion UMDUIStatics::GetMotion()
{
	return GetStyle()->Motion;
}

float UMDUIStatics::EaseOutCubic(float T)
{
	const float Inv = 1.0f - FMath::Clamp(T, 0.0f, 1.0f);
	return 1.0f - Inv * Inv * Inv;
}
