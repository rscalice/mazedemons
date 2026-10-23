#include "MDImpactSet.h"

FMDImpactEntry UMDImpactSet::FindEntry(EPhysicalSurface Surface) const
{
	for (const FMDImpactEntry& Entry : Entries)
	{
		if (Entry.Surface == Surface)
		{
			return Entry;
		}
	}
	return Default;
}
