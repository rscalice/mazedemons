#include "Ranged/MDBeamRules.h"

void FMDBeamNumbers::Add(TArray<FMDBeamNumberBucket>& Buckets, const FMDHitConfirm& Hit, double Now)
{
	for (FMDBeamNumberBucket& Bucket : Buckets)
	{
		if (Bucket.Confirm.Victim == Hit.Victim && Bucket.Confirm.DamageType == Hit.DamageType)
		{
			Bucket.Confirm.Damage += Hit.Damage;
			Bucket.Confirm.bCrit |= Hit.bCrit;
			Bucket.Confirm.bKill |= Hit.bKill;
			Bucket.Confirm.bHeadshot |= Hit.bHeadshot;
			Bucket.Confirm.HitStopFrames = FMath::Max(Bucket.Confirm.HitStopFrames, Hit.HitStopFrames);
			Bucket.Confirm.Location = Hit.Location;
			return;
		}
	}
	FMDBeamNumberBucket& NewBucket = Buckets.AddDefaulted_GetRef();
	NewBucket.Confirm = Hit;
	NewBucket.Start = Now;
}

void FMDBeamNumbers::Collect(TArray<FMDBeamNumberBucket>& Buckets, double Now, bool bFlushAll, TArray<FMDHitConfirm>& Out)
{
	for (int32 i = 0; i < Buckets.Num();)
	{
		const FMDBeamNumberBucket& Bucket = Buckets[i];
		const bool bDue = bFlushAll || Bucket.Confirm.bKill || (Now - Bucket.Start) >= FMDBeamRules::NumberWindowSeconds - KINDA_SMALL_NUMBER;
		if (bDue)
		{
			Out.Add(Bucket.Confirm);
			Buckets.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}
}
