#include "MDPoiseProfile.h"

#include "Animation/AnimMontage.h"

UAnimMontage* UMDPoiseProfile::FindMontage(EMDHitReaction Reaction, EMDHitDirection Direction) const
{
	for (int32 Rank = static_cast<int32>(Reaction); Rank > static_cast<int32>(EMDHitReaction::None); --Rank)
	{
		const EMDHitReaction Wanted = static_cast<EMDHitReaction>(Rank);
		UAnimMontage* AnyDirection = nullptr;
		for (const FMDReactionMontage& Entry : ReactionMontages)
		{
			if (Entry.Reaction != Wanted || !Entry.Montage)
			{
				continue;
			}
			if (Entry.Direction == Direction)
			{
				return Entry.Montage;
			}
			if (!AnyDirection)
			{
				AnyDirection = Entry.Montage;
			}
		}
		if (AnyDirection)
		{
			return AnyDirection;
		}
	}
	return nullptr;
}
