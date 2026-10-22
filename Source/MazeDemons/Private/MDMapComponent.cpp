#include "MDMapComponent.h"

#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MDFogComponent.h"
#include "MDLevelMap.h"
#include "MDMapPlan.h"
#include "MDMapZone.h"
#include "MDObjectiveMarker.h"
#include "MDQuestComponent.h"
#include "MDUIStatics.h"
#include "MDUISettings.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "TimerManager.h"

UMDMapComponent::UMDMapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMDMapComponent* UMDMapComponent::FindFor(const AActor* ControllerOrPawn)
{
	if (const APawn* Pawn = Cast<APawn>(ControllerOrPawn))
	{
		ControllerOrPawn = Pawn->GetController();
	}
	return ControllerOrPawn ? ControllerOrPawn->FindComponentByClass<UMDMapComponent>() : nullptr;
}

void UMDMapComponent::BeginPlay()
{
	Super::BeginPlay();
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC && PC->IsLocalController() && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UMDMapComponent::Refresh, FMath::Max(RefreshInterval, 0.05f), true);
	}
}

void UMDMapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (UMDFogComponent* Fog = BoundFog.Get())
	{
		Fog->OnFogChanged.RemoveDynamic(this, &UMDMapComponent::HandleFogChanged);
	}
	if (UMDQuestComponent* Quests = BoundQuests.Get())
	{
		Quests->OnQuestsChanged.RemoveDynamic(this, &UMDMapComponent::HandleQuestsChanged);
	}
	if (AMDLevelMap* Map = LevelMap.Get())
	{
		Map->OnMapCaptured.RemoveDynamic(this, &UMDMapComponent::HandleLevelMapCaptured);
	}
	Super::EndPlay(EndPlayReason);
}

void UMDMapComponent::BindSources()
{
	if (!LevelMap.IsValid())
	{
		if (AMDLevelMap* Map = AMDLevelMap::FindLevelMap(this))
		{
			LevelMap = Map;
			Map->OnMapCaptured.AddUniqueDynamic(this, &UMDMapComponent::HandleLevelMapCaptured);
			MapTextureCapture = -1; // picked up below once the map has a plan
		}
	}
	if (!BoundFog.IsValid())
	{
		if (UMDFogComponent* Fog = UMDFogComponent::FindFor(this))
		{
			BoundFog = Fog;
			Fog->OnFogChanged.AddUniqueDynamic(this, &UMDMapComponent::HandleFogChanged);
			bFogDirty = true;
		}
	}
	if (!BoundQuests.IsValid())
	{
		if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
		{
			BoundQuests = Quests;
			Quests->OnQuestsChanged.AddUniqueDynamic(this, &UMDMapComponent::HandleQuestsChanged);
			RefreshMarkers();
		}
	}
}

void UMDMapComponent::HandleFogChanged()
{
	bFogDirty = true; // applied by the next Refresh (at most RefreshInterval later)
}

void UMDMapComponent::HandleQuestsChanged()
{
	RefreshMarkers();
}

void UMDMapComponent::HandleLevelMapCaptured(AMDLevelMap* Map)
{
	if (Map && Map == LevelMap.Get())
	{
		MapTextureCapture = -1; // rebuilt by the next Refresh with the new plan
	}
}

void UMDMapComponent::Refresh()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	BindSources();

	// The level map's plan is new (first capture or recapture): rebuild the texture and tell the HUD to feed it again.
	const AMDLevelMap* Map = LevelMap.Get();
	if (Map && Map->HasPlan() && Map->GetCaptureCount() != MapTextureCapture)
	{
		MapTextureCapture = Map->GetCaptureCount();
		RebuildGrid();
		bFogDirty = false;
		OnLevelMapChanged.Broadcast(LevelMap.Get());
	}

	// Keep the last zone while there is no pawn (dead / respawning). Zone changes never touch the map.
	if (const APawn* Pawn = PC->GetPawn())
	{
		AMDMapZone* Zone = AMDMapZone::FindZoneAt(this, Pawn->GetActorLocation());
		if (Zone != CurrentZone.Get())
		{
			CurrentZone = Zone;
			RefreshMarkers();
			OnZoneChanged.Broadcast(Zone);
		}
	}
	if (bFogDirty)
	{
		RebuildGrid();
		bFogDirty = false;
	}
	if (++RefreshCount % 8 == 0)
	{
		RefreshMarkers();
	}
}

void UMDMapComponent::RebuildGrid()
{
	// Rebuilt from the replicated layout and bits every time (cheap, at most 2 KB), so a server-side reset also clears it here.
	const UMDFogComponent* Fog = BoundFog.Get();
	if (!Fog || !Fog->BuildGrid(FogGrid))
	{
		FogGrid = FMDFogGrid();
	}
	RebuildFogTexture();
	RebuildMapTexture();
	OnFogUpdated.Broadcast();
}

void UMDMapComponent::RebuildMapTexture()
{
	const AMDLevelMap* Map = LevelMap.Get();
	if (!Map || !Map->HasPlan())
	{
		return;
	}
	const int32 Size = Map->GetPlanSize();
	TArray<FColor> Pixels;
	const FColor FogColor = UMDUIStatics::GetColor(EMDColor::Bg0).ToFColor(true);
	if (!FMDMapPlanRules::ComposeFog(Map->GetPlanPixels(), Size, FogGrid, FogColor, Pixels))
	{
		return;
	}

	if (!MapTexture || MapTexture->GetSizeX() != Size || MapTexture->GetSizeY() != Size)
	{
		// First upload (or a new resolution): fill the mip and create the resource.
		MapTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("MDLevelMinimap"));
		if (!MapTexture)
		{
			return;
		}
		MapTexture->SRGB = true;
		MapTexture->NeverStream = true;
		MapTexture->Filter = TF_Bilinear;
		MapTexture->AddressX = TA_Clamp;
		MapTexture->AddressY = TA_Clamp;
		MapTexture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		MapTexture->UpdateResource();
		return;
	}

	// Same object, new pixels: async region update, no render-thread flush, the ProHUD brush keeps pointing at it.
	const uint32 Pitch = static_cast<uint32>(Size) * sizeof(FColor);
	uint8* Buffer = static_cast<uint8*>(FMemory::Malloc(Pitch * Size));
	FMemory::Memcpy(Buffer, Pixels.GetData(), Pitch * Size);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size, Size);
	MapTexture->UpdateTextureRegions(0, 1, Region, Pitch, sizeof(FColor), Buffer,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
}

void UMDMapComponent::RebuildFogTexture()
{
	if (!FogGrid.IsValid())
	{
		return;
	}
	const int32 Width = FogGrid.Width;
	const int32 Height = FogGrid.Height;
	if (!FogTexture || FogTexture->GetSizeX() != Width || FogTexture->GetSizeY() != Height)
	{
		FogTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!FogTexture)
		{
			return;
		}
		FogTexture->SRGB = false;
		FogTexture->NeverStream = true;
		FogTexture->Filter = TF_Bilinear;
		FogTexture->AddressX = TA_Clamp;
		FogTexture->AddressY = TA_Clamp;
	}

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			Pixels[Y * Width + X] = FogGrid.IsRevealedAt(X, Y) ? FColor(255, 0, 0, 255) : FColor(0, 0, 0, 255);
		}
	}

	FTexture2DMipMap& Mip = FogTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	FogTexture->UpdateResource();
}

void UMDMapComponent::RefreshMarkers()
{
	CachedMarkers.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMDObjectiveMarker> It(World); It; ++It)
		{
			if (It->MapMarker != EMDMapMarkerType::None)
			{
				CachedMarkers.Add(*It);
			}
		}
	}
}

TArray<AMDObjectiveMarker*> UMDMapComponent::GetObjectiveMarkers() const
{
	TArray<AMDObjectiveMarker*> Result;
	for (AMDObjectiveMarker* Marker : CachedMarkers)
	{
		if (IsValid(Marker) && !Marker->bConsumed && Marker->MapMarker != EMDMapMarkerType::None)
		{
			Result.Add(Marker);
		}
	}
	return Result;
}

FVector2D UMDMapComponent::WorldToMapUV(FVector World) const
{
	const AMDLevelMap* Map = LevelMap.Get();
	return Map ? Map->WorldToMapUV(World) : FVector2D::ZeroVector;
}

bool UMDMapComponent::IsMinimapNorthUp() const
{
	const UMDLocalSettings* Settings = GetDefault<UMDLocalSettings>();
	return Settings && Settings->bMinimapNorthUp;
}

void UMDMapComponent::SetMinimapNorthUp(bool bNorthUp)
{
	UMDLocalSettings* Settings = GetMutableDefault<UMDLocalSettings>();
	if (!Settings || Settings->bMinimapNorthUp == bNorthUp)
	{
		return;
	}
	Settings->bMinimapNorthUp = bNorthUp;
	Settings->SaveConfig();
	OnMinimapOptionsChanged.Broadcast();
}

void UMDMapComponent::ToggleMinimapRotation()
{
	SetMinimapNorthUp(!IsMinimapNorthUp());
}

float UMDMapComponent::GetMinimapZoom() const
{
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	return Settings ? FMath::Max(Settings->MinimapZoom, 0.1f) : 1.0f;
}

float UMDMapComponent::GetMinimapRadiusUU() const
{
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	return Settings ? FMath::Max(Settings->MinimapRadiusUU, 100.0f) : 3000.0f;
}

void UMDMapComponent::ForceRecaptureLevelMap()
{
	if (AMDLevelMap* Map = LevelMap.IsValid() ? LevelMap.Get() : AMDLevelMap::FindLevelMap(this))
	{
		Map->Recapture();
	}
}

FString UMDMapComponent::BuildDump() const
{
	const AMDMapZone* Zone = CurrentZone.Get();
	const AMDLevelMap* Map = LevelMap.Get();
	return FString::Printf(TEXT("Map: %s | minimap texture %s %dx%d (plan capture %d) | zone %s (index %d) | fog grid %dx%d cell %.0f, revealed %.0f%%, shared fog %s | markers %d | minimap %s"),
		Map ? *Map->BuildDump() : TEXT("NO level map in this world"),
		*GetNameSafe(MapTexture), MapTexture ? MapTexture->GetSizeX() : 0, MapTexture ? MapTexture->GetSizeY() : 0, MapTextureCapture,
		*GetNameSafe(Zone), Zone ? Zone->ZoneIndex : 0,
		FogGrid.Width, FogGrid.Height, FogGrid.CellSize, FogGrid.GetRevealedFraction() * 100.0f,
		BoundFog.IsValid() ? TEXT("bound") : TEXT("not bound"), CachedMarkers.Num(),
		IsMinimapNorthUp() ? TEXT("north-up") : TEXT("rotates with camera"));
}
