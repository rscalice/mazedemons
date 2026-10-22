#include "MDMapZone.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

AMDMapZone::AMDMapZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // every machine queries its own copy

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	ZoneBox->SetBoxExtent(FVector(2000.0f, 2000.0f, 500.0f));
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetGenerateOverlapEvents(false);
	SetRootComponent(ZoneBox);
}

FBox2D AMDMapZone::GetZoneBounds() const
{
	const FVector Centre = ZoneBox ? ZoneBox->GetComponentLocation() : GetActorLocation();
	const FVector Extent = ZoneBox ? ZoneBox->GetScaledBoxExtent() : FVector(2000.0, 2000.0, 500.0);
	return FBox2D(FVector2D(Centre.X - Extent.X, Centre.Y - Extent.Y), FVector2D(Centre.X + Extent.X, Centre.Y + Extent.Y));
}

bool AMDMapZone::ContainsXY(FVector Location) const
{
	if (!ZoneBox)
	{
		return false;
	}
	const FVector Local = ZoneBox->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = ZoneBox->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y;
}

AMDMapZone* AMDMapZone::FindZoneAt(const UObject* WorldContextObject, FVector Location)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	AMDMapZone* Best = nullptr;
	double BestArea = 0.0;
	for (TActorIterator<AMDMapZone> It(World); It; ++It)
	{
		AMDMapZone* Zone = *It;
		if (!Zone->ContainsXY(Location))
		{
			continue;
		}
		const FBox2D Bounds = Zone->GetZoneBounds();
		const double Area = (Bounds.Max.X - Bounds.Min.X) * (Bounds.Max.Y - Bounds.Min.Y);
		if (!Best || Area < BestArea)
		{
			Best = Zone;
			BestArea = Area;
		}
	}
	return Best;
}

AMDMapZone* AMDMapZone::FindZoneByIndex(const UObject* WorldContextObject, int32 Index)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMDMapZone> It(World); It; ++It)
	{
		if (It->ZoneIndex == Index)
		{
			return *It;
		}
	}
	return nullptr;
}
