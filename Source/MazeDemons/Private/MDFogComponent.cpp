#include "MDFogComponent.h"

#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDMapZone.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	FMDFogGrid& EnsureFogGrid(TMap<int32, FMDFogGrid>& Grids, const AMDMapZone& Zone)
	{
		FMDFogGrid* Existing = Grids.Find(Zone.ZoneIndex);
		if (!Existing)
		{
			FMDFogGrid& Created = Grids.Add(Zone.ZoneIndex);
			Created.Init(Zone.GetMapBounds(), Zone.GetFogCellSize());
			return Created;
		}
		return *Existing;
	}
}

UMDFogComponent::UMDFogComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UMDFogComponent* UMDFogComponent::FindFor(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AMDGameState* State = World ? World->GetGameState<AMDGameState>() : nullptr;
	return State ? State->FogComponent.Get() : nullptr;
}

void UMDFogComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDFogComponent, Fog);
}

bool UMDFogComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UMDFogComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsServer() && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(UpdateTimer, this, &UMDFogComponent::UpdateFog, FMath::Max(UpdateInterval, 0.05f), true);
	}
}

void UMDFogComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(UpdateTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool UMDFogComponent::GetZoneBits(int32 Zone, TArray<uint8>& OutBits) const
{
	for (const FMDFogZoneBits& Entry : Fog)
	{
		if (Entry.ZoneIndex == Zone)
		{
			OutBits = Entry.Bits;
			return true;
		}
	}
	return false;
}

void UMDFogComponent::UpdateFog()
{
	const AGameStateBase* State = Cast<AGameStateBase>(GetOwner());
	UWorld* World = GetWorld();
	if (!State || !World)
	{
		return;
	}

	TArray<FVector2D> Positions;
	for (const APlayerState* PlayerState : State->PlayerArray)
	{
		const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Pawn);
		if (ASC && ASC->IsDead())
		{
			continue;
		}
		const FVector Location = Pawn->GetActorLocation();
		Positions.Add(FVector2D(Location.X, Location.Y));
	}
	if (Positions.IsEmpty())
	{
		return;
	}

	bool bAnyChanged = false;
	for (TActorIterator<AMDMapZone> It(World); It; ++It)
	{
		const AMDMapZone* Zone = *It;
		FMDFogGrid& Grid = EnsureFogGrid(Grids, *Zone);
		bool bChanged = false;
		for (const FVector2D& Position : Positions)
		{
			bChanged |= Grid.RevealRadius(Position, RevealRadius);
		}
		if (bChanged)
		{
			PublishZone(Zone->ZoneIndex, Grid);
			bAnyChanged = true;
		}
	}
	if (bAnyChanged)
	{
		OnFogChanged.Broadcast();
	}
}

void UMDFogComponent::PublishZone(int32 Zone, const FMDFogGrid& Grid)
{
	FMDFogZoneBits* Entry = Fog.FindByPredicate([Zone](const FMDFogZoneBits& Bits) { return Bits.ZoneIndex == Zone; });
	if (!Entry)
	{
		Entry = &Fog.AddDefaulted_GetRef();
		Entry->ZoneIndex = Zone;
	}
	Entry->Bits = Grid.Bits;
}

void UMDFogComponent::RevealAllZones()
{
	UWorld* World = GetWorld();
	if (!IsServer() || !World)
	{
		return;
	}
	for (TActorIterator<AMDMapZone> It(World); It; ++It)
	{
		FMDFogGrid& Grid = EnsureFogGrid(Grids, **It);
		Grid.RevealAll();
		PublishZone(It->ZoneIndex, Grid);
	}
	OnFogChanged.Broadcast();
}

void UMDFogComponent::ResetAllZones()
{
	if (!IsServer())
	{
		return;
	}
	Grids.Empty();
	Fog.Reset();
	OnFogChanged.Broadcast();
}

void UMDFogComponent::OnRep_Fog()
{
	OnFogChanged.Broadcast();
}
