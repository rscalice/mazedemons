#include "AI/MDEnemyArchetype.h"

#include "MDPoiseProfile.h"

FGameplayTagContainer UMDEnemyArchetype::GetVictimTags() const
{
	FGameplayTagContainer Tags;
	if (ArchetypeTag.IsValid())
	{
		Tags.AddTag(ArchetypeTag);
	}
	return Tags;
}

float UMDEnemyArchetype::GetAttackRange() const
{
	float Range = 0.0f;
	for (const FMDEnemyAttack& Attack : Attacks)
	{
		Range = FMath::Max(Range, Attack.Range);
	}
	return Range;
}

int32 UMDEnemyArchetype::PickAttack(float Distance) const
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Attacks.Num(); ++i)
	{
		if (Distance >= Attacks[i].MinRange && Distance <= Attacks[i].Range && (Best == INDEX_NONE || Attacks[i].Range < Attacks[Best].Range))
		{
			Best = i;
		}
	}
	return Best;
}

int32 UMDEnemyArchetype::PickAttackWeighted(float Distance, float Roll) const
{
	float Total = 0.0f;
	for (const FMDEnemyAttack& Attack : Attacks)
	{
		if (Distance >= Attack.MinRange && Distance <= Attack.Range)
		{
			Total += FMath::Max(Attack.SelectionWeight, 0.0f);
		}
	}
	if (Total <= 0.0f)
	{
		return PickAttack(Distance);
	}
	float Remaining = FMath::Clamp(Roll, 0.0f, 0.9999f) * Total;
	int32 Last = INDEX_NONE;
	for (int32 i = 0; i < Attacks.Num(); ++i)
	{
		const FMDEnemyAttack& Attack = Attacks[i];
		if (Distance < Attack.MinRange || Distance > Attack.Range || Attack.SelectionWeight <= 0.0f)
		{
			continue;
		}
		Last = i;
		Remaining -= Attack.SelectionWeight;
		if (Remaining < 0.0f)
		{
			return i;
		}
	}
	return Last;
}

bool UMDEnemyArchetype::ShouldFaceTarget(bool bHasTarget, float Distance, float EngageFaceRange, bool bAttacking)
{
	return bHasTarget && !bAttacking && EngageFaceRange > 0.0f && Distance <= EngageFaceRange;
}

bool UMDEnemyArchetype::AllowsTier(EMDIntelligenceTier Tier) const
{
	const uint8 Low = FMath::Min(static_cast<uint8>(MinTier), static_cast<uint8>(MaxTier));
	const uint8 High = FMath::Max(static_cast<uint8>(MinTier), static_cast<uint8>(MaxTier));
	const uint8 Value = static_cast<uint8>(Tier);
	return Value >= Low && Value <= High;
}

UMDPoiseProfile* UMDEnemyArchetype::ResolvePoiseProfile(UObject* Outer) const
{
	UObject* Owner = Outer ? Outer : GetTransientPackage();
	if (!PoiseProfile)
	{
		UMDPoiseProfile* Fresh = NewObject<UMDPoiseProfile>(Owner);
		Fresh->MaxPoise = FMath::Max(MaxPoise, 1.0f);
		return Fresh;
	}
	if (FMath::IsNearlyEqual(PoiseProfile->MaxPoise, MaxPoise))
	{
		return PoiseProfile;
	}
	UMDPoiseProfile* Copy = DuplicateObject<UMDPoiseProfile>(PoiseProfile, Owner);
	Copy->MaxPoise = FMath::Max(MaxPoise, 1.0f);
	return Copy;
}

bool UMDEnemyArchetype::IsEnemyTag(const FGameplayTag& Tag)
{
	static const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Enemy"), false);
	return Tag.IsValid() && Root.IsValid() && Tag.MatchesTag(Root);
}
