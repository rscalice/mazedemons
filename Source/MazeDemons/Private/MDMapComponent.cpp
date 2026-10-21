#include "MDMapComponent.h"

#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MDFogComponent.h"
#include "MDMapZone.h"
#include "MDObjectiveMarker.h"
#include "MDQuestComponent.h"
#include "MDUISettings.h"
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
	Super::EndPlay(EndPlayReason);
}

void UMDMapComponent::BindSources()
{
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

void UMDMapComponent::Refresh()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	BindSources();

	// Keep the last zone while there is no pawn (dead / respawning).
	if (const APawn* Pawn = PC->GetPawn())
	{
		AMDMapZone* Zone = AMDMapZone::FindZoneAt(this, Pawn->GetActorLocation());
		if (Zone != CurrentZone.Get())
		{
			CurrentZone = Zone;
			bFogDirty = true;
			RefreshMarkers();
			RebuildGrid();
			OnZoneChanged.Broadcast(Zone);
			bFogDirty = false;
			return;
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
	const AMDMapZone* Zone = CurrentZone.Get();
	if (!Zone)
	{
		FogGrid = FMDFogGrid();
		return;
	}
	// Rebuilt from the replicated bits every time (cheap, at most 512 bytes), so a server-side fog reset also clears it here.
	FogGrid.Init(Zone->GetMapBounds(), Zone->GetFogCellSize());
	TArray<uint8> Bits;
	const UMDFogComponent* Fog = BoundFog.Get();
	if (Fog && Fog->GetZoneBits(Zone->ZoneIndex, Bits))
	{
		FogGrid.MergeBits(Bits);
	}
	RebuildFogTexture();
	OnFogUpdated.Broadcast();
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

UTexture* UMDMapComponent::GetMapTexture() const
{
	const AMDMapZone* Zone = CurrentZone.Get();
	return Zone ? Zone->GetMapTexture() : nullptr;
}

FVector2D UMDMapComponent::WorldToMapUV(FVector World) const
{
	const AMDMapZone* Zone = CurrentZone.Get();
	return Zone ? Zone->WorldToMapUV(World) : FVector2D::ZeroVector;
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

void UMDMapComponent::ForceRecaptureCurrentZone()
{
	if (AMDMapZone* Zone = CurrentZone.Get())
	{
		Zone->ForceRecapture();
	}
}

FString UMDMapComponent::BuildDump() const
{
	const AMDMapZone* Zone = CurrentZone.Get();
	const UTexture* Map = GetMapTexture();
	return FString::Printf(TEXT("Map: zone %s (index %d), map texture %s (%s), fog grid %dx%d cell %.0f, revealed %.0f%%, shared fog bits %s, markers %d, minimap %s"),
		*GetNameSafe(Zone), Zone ? Zone->ZoneIndex : 0, *GetNameSafe(Map),
		Zone && Zone->HasRuntimeMap() ? TEXT("runtime capture") : TEXT("baked or none"),
		FogGrid.Width, FogGrid.Height, FogGrid.CellSize, FogGrid.GetRevealedFraction() * 100.0f,
		BoundFog.IsValid() ? TEXT("bound") : TEXT("not bound"), CachedMarkers.Num(),
		IsMinimapNorthUp() ? TEXT("north-up") : TEXT("rotates with camera"));
}
