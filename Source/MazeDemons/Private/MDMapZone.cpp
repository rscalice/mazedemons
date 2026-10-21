#include "MDMapZone.h"

#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "MDQuestTypes.h"
#include "MDUISettings.h"

AMDMapZone::AMDMapZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // every machine captures and queries its own copy

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	ZoneBox->SetBoxExtent(FVector(2000.0f, 2000.0f, 500.0f));
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetGenerateOverlapEvents(false);
	SetRootComponent(ZoneBox);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(ZoneBox);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
}

void AMDMapZone::BeginPlay()
{
	Super::BeginPlay();
	if (bCaptureOnBeginPlay && GetNetMode() != NM_DedicatedServer)
	{
		CaptureMap();
	}
}

void AMDMapZone::CaptureMap()
{
	if (!bCaptured)
	{
		DoCapture();
	}
}

void AMDMapZone::ForceRecapture()
{
	DoCapture();
}

void AMDMapZone::DoCapture()
{
	if (!Capture || !ZoneBox || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const int32 Resolution = FMath::Clamp(CaptureResolution, 64, 4096);
	if (!RuntimeMap || RuntimeMap->SizeX != Resolution)
	{
		RuntimeMap = NewObject<UTextureRenderTarget2D>(this);
		RuntimeMap->InitAutoFormat(Resolution, Resolution);
		RuntimeMap->UpdateResourceImmediate(true);
	}

	const FBox2D MapBounds = GetMapBounds();
	const FVector2D Centre = (MapBounds.Min + MapBounds.Max) * 0.5;
	const float TopZ = ZoneBox->GetComponentLocation().Z + ZoneBox->GetScaledBoxExtent().Z + CaptureHeightAboveBox;

	// Pitch -90 looks straight down; yaw -90 puts +X on the right and +Y down in the image (U follows X, V follows Y).
	Capture->SetWorldLocationAndRotation(FVector(Centre.X, Centre.Y, TopZ), FRotator(-90.0f, -90.0f, 0.0f));
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = static_cast<float>(MapBounds.Max.X - MapBounds.Min.X);
	Capture->CaptureSource = CaptureSource;
	Capture->TextureTarget = RuntimeMap;
	Capture->ShowFlags.SetPostProcessing(false);
	Capture->ShowFlags.SetDynamicShadows(false);
	Capture->CaptureScene();
	bCaptured = true;
}

UTexture* AMDMapZone::GetMapTexture() const
{
	if (RuntimeMap)
	{
		return RuntimeMap;
	}
	return BakedMap;
}

FBox2D AMDMapZone::GetMapBounds() const
{
	const FVector Centre = ZoneBox ? ZoneBox->GetComponentLocation() : GetActorLocation();
	const FVector Extent = ZoneBox ? ZoneBox->GetScaledBoxExtent() : FVector(2000.0, 2000.0, 500.0);
	const double Half = FMath::Max(FMath::Max(Extent.X, Extent.Y), 1.0);
	return FBox2D(FVector2D(Centre.X - Half, Centre.Y - Half), FVector2D(Centre.X + Half, Centre.Y + Half));
}

float AMDMapZone::GetFogCellSize() const
{
	if (FogCellSize > 0.0f)
	{
		return FogCellSize;
	}
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	return Settings ? Settings->FogCellSize : 400.0f;
}

FVector2D AMDMapZone::WorldToMapUV(FVector World) const
{
	return UMDQuestRules::WorldToMapUV(FVector2D(World.X, World.Y), GetMapBounds());
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
		const FBox2D Bounds = Zone->GetMapBounds();
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
