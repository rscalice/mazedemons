#include "MDLevelMap.h"

#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MDMapZone.h"
#include "MDObjectiveMarker.h"
#include "MDQuestGate.h"
#include "MDQuestTypes.h"
#include "MDUISettings.h"
#include "TextureResource.h"
#include "TimerManager.h"

AMDLevelMap::AMDLevelMap()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // every machine captures its own copy

	MapBox = CreateDefaultSubobject<UBoxComponent>(TEXT("MapBox"));
	MapBox->SetBoxExtent(FVector(4000.0f, 4000.0f, 600.0f));
	MapBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapBox->SetGenerateOverlapEvents(false);
	MapBox->ShapeColor = FColor(80, 200, 255);
	SetRootComponent(MapBox);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(MapBox);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = SCS_SceneDepth;
}

AMDLevelMap* AMDLevelMap::FindLevelMap(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMDLevelMap> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

void AMDLevelMap::BeginPlay()
{
	Super::BeginPlay();
	if (bCaptureOnBeginPlay && GetNetMode() != NM_DedicatedServer && GetWorld())
	{
		// Next tick: every level actor has registered its render state by then, and the capture never depends on pawns.
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &AMDLevelMap::CaptureMap);
	}
}

void AMDLevelMap::CaptureMap()
{
	if (!bCaptured)
	{
		if (bAutoFitToZones)
		{
			FitToZones();
		}
		DoCapture();
	}
}

void AMDLevelMap::Recapture()
{
	if (bAutoFitToZones)
	{
		FitToZones();
	}
	DoCapture();
}

bool AMDLevelMap::FitToZones()
{
	UWorld* World = GetWorld();
	if (!World || !MapBox)
	{
		return false;
	}
	TArray<FBox2D> Boxes;
	for (TActorIterator<AMDMapZone> It(World); It; ++It)
	{
		Boxes.Add(It->GetZoneBounds());
	}
	const FBox2D Fit = FMDMapPlanRules::FitBounds(Boxes, AutoFitPadding);
	if (!Fit.bIsValid)
	{
		return false;
	}
	const FVector2D Centre = (Fit.Min + Fit.Max) * 0.5;
	const FVector2D Half = (Fit.Max - Fit.Min) * 0.5;
	const FVector Location = MapBox->GetComponentLocation();
	const FVector Scale = MapBox->GetComponentScale();
	SetActorLocation(FVector(Centre.X, Centre.Y, Location.Z));
	MapBox->SetBoxExtent(FVector(Half.X / FMath::Max(Scale.X, 0.01), Half.Y / FMath::Max(Scale.Y, 0.01), MapBox->GetUnscaledBoxExtent().Z));
	return true;
}

void AMDLevelMap::CollectHidden(TArray<AActor*>& OutActors) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == this)
		{
			continue;
		}
		const bool bHide = Actor->IsA<APawn>()
			|| Actor->IsA<AMDObjectiveMarker>()
			|| Actor->IsA<AMDQuestGate>()
			|| (!HiddenActorTag.IsNone() && Actor->ActorHasTag(HiddenActorTag))
			|| (bHideReplicatedActors && Actor->GetIsReplicated());
		if (bHide)
		{
			OutActors.Add(Actor);
		}
	}
}

void AMDLevelMap::DoCapture()
{
	if (!Capture || !MapBox || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const int32 Resolution = FMath::Clamp(CaptureResolution, 256, 4096);
	if (!DepthTarget || DepthTarget->SizeX != Resolution)
	{
		DepthTarget = NewObject<UTextureRenderTarget2D>(this);
		DepthTarget->RenderTargetFormat = RTF_R32f;
		DepthTarget->ClearColor = FLinearColor::Black;
		DepthTarget->bAutoGenerateMips = false;
		DepthTarget->InitAutoFormat(Resolution, Resolution);
		DepthTarget->UpdateResourceImmediate(true);
	}

	const FBox2D MapBounds = GetMapBounds();
	const FVector2D Centre = (MapBounds.Min + MapBounds.Max) * 0.5;
	const float BoxZ = MapBox->GetComponentLocation().Z;
	const float BoxHalfZ = MapBox->GetScaledBoxExtent().Z;
	const float EyeZ = BoxZ + BoxHalfZ;
	const float MinValidHeight = BoxZ - BoxHalfZ;

	TArray<AActor*> Hidden;
	CollectHidden(Hidden);
	HiddenActorCount = Hidden.Num();
	Capture->HiddenActors.Reset();
	for (AActor* Actor : Hidden)
	{
		Capture->HiddenActors.Add(Actor);
	}

	// Pitch -90 looks straight down; yaw -90 puts +X on the right and +Y down in the image (U follows X, V follows Y).
	Capture->SetWorldLocationAndRotation(FVector(Centre.X, Centre.Y, EyeZ), FRotator(-90.0f, -90.0f, 0.0f));
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = static_cast<float>(MapBounds.Max.X - MapBounds.Min.X);
	Capture->bAutoCalculateOrthoPlanes = false;
	Capture->bOverride_CustomNearClippingPlane = true;
	Capture->CustomNearClippingPlane = 1.0f;
	Capture->CaptureSource = SCS_SceneDepth;
	Capture->TextureTarget = DepthTarget;
	Capture->ShowFlags.SetPostProcessing(false);
	Capture->ShowFlags.SetDynamicShadows(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->CaptureScene();

	// The read-back flushes the render thread, so it sees this capture. Happens once per capture, never per frame.
	FTextureRenderTargetResource* Resource = DepthTarget->GameThread_GetRenderTargetResource();
	TArray<FLinearColor> Depth;
	if (!Resource || !Resource->ReadLinearColorPixels(Depth) || Depth.Num() != Resolution * Resolution)
	{
		UE_LOG(LogTemp, Warning, TEXT("AMDLevelMap: depth read-back failed (%d pixels)"), Depth.Num());
		return;
	}

	TArray<float> Heights;
	Heights.SetNumUninitialized(Depth.Num());
	for (int32 Index = 0; Index < Depth.Num(); ++Index)
	{
		const float D = Depth[Index].R;
		// No hit reads as the far plane (or 0 / garbage): treat it as below the box, i.e. Void.
		Heights[Index] = (FMath::IsFinite(D) && D > 0.0f) ? EyeZ - D : MinValidHeight - 1000.0f;
	}
	FloorHeight = FMDMapPlanRules::EstimateFloorHeight(Heights, MinValidHeight, 10.0f);
	if (!FMDMapPlanRules::BuildPlan(Heights, Resolution, FloorHeight, MinValidHeight, Style, PlanPixels, &PlanCounts))
	{
		return;
	}
	PlanSize = Resolution;
	bCaptured = true;
	++CaptureCount;
	UE_LOG(LogTemp, Log, TEXT("AMDLevelMap: %s"), *BuildDump());
	OnMapCaptured.Broadcast(this);
}

FBox2D AMDLevelMap::GetLevelBounds() const
{
	const FVector Centre = MapBox ? MapBox->GetComponentLocation() : GetActorLocation();
	const FVector Extent = MapBox ? MapBox->GetScaledBoxExtent() : FVector(4000.0, 4000.0, 600.0);
	return FBox2D(FVector2D(Centre.X - Extent.X, Centre.Y - Extent.Y), FVector2D(Centre.X + Extent.X, Centre.Y + Extent.Y));
}

FBox2D AMDLevelMap::GetMapBounds() const
{
	return FMDMapPlanRules::MakeSquare(GetLevelBounds());
}

FVector AMDLevelMap::GetMinimapCornerBL() const
{
	const FBox2D Bounds = GetMapBounds();
	const float Z = MapBox ? MapBox->GetComponentLocation().Z : GetActorLocation().Z;
	return FVector(Bounds.Min.X, Bounds.Min.Y, Z);
}

FVector AMDLevelMap::GetMinimapCornerTR() const
{
	const FBox2D Bounds = GetMapBounds();
	const float Z = MapBox ? MapBox->GetComponentLocation().Z : GetActorLocation().Z;
	return FVector(Bounds.Max.X, Bounds.Max.Y, Z);
}

float AMDLevelMap::GetMinimapCanvasSize() const
{
	const FBox2D Bounds = GetMapBounds();
	return FMath::Max(static_cast<float>(Bounds.Max.X - Bounds.Min.X) * FMath::Max(MinimapCanvasPixelsPerUU, 0.01f), 64.0f);
}

float AMDLevelMap::GetFogCellSize() const
{
	if (FogCellSize > 0.0f)
	{
		return FogCellSize;
	}
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	return Settings ? Settings->FogCellSize : 400.0f;
}

FVector2D AMDLevelMap::WorldToMapUV(FVector World) const
{
	return UMDQuestRules::WorldToMapUV(FVector2D(World.X, World.Y), GetMapBounds());
}

FString AMDLevelMap::BuildDump() const
{
	const FBox2D Bounds = GetMapBounds();
	const int32 Total = FMath::Max(PlanSize * PlanSize, 1);
	auto Percent = [this, Total](int32 Index) { return PlanCounts.IsValidIndex(Index) ? 100.0f * PlanCounts[Index] / Total : 0.0f; };
	return FString::Printf(TEXT("level map %s: %s, map square (%.0f,%.0f)-(%.0f,%.0f), %dpx (%.1f uu/px), floor z %.0f, captures %d, hidden actors %d, ")
		TEXT("floor %.1f%% edge %.1f%% low %.1f%% obstacle %.1f%% solid %.1f%% void %.1f%%"),
		*GetName(), HasPlan() ? TEXT("captured") : TEXT("not captured"), Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y,
		PlanSize, PlanSize > 0 ? (Bounds.Max.X - Bounds.Min.X) / PlanSize : 0.0, FloorHeight, CaptureCount, HiddenActorCount,
		Percent(static_cast<int32>(EMDMapCell::Floor)), Percent(5), Percent(static_cast<int32>(EMDMapCell::Low)),
		Percent(static_cast<int32>(EMDMapCell::Obstacle)), Percent(static_cast<int32>(EMDMapCell::Solid)),
		Percent(static_cast<int32>(EMDMapCell::Void)));
}
