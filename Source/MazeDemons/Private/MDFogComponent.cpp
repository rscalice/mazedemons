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
#include "MDLevelMap.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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
	DOREPLIFETIME(UMDFogComponent, LevelFog);
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

bool UMDFogComponent::BuildGrid(FMDFogGrid& OutGrid) const
{
	if (!LevelFog.HasLayout())
	{
		OutGrid = FMDFogGrid();
		return false;
	}
	OutGrid.Init(FBox2D(LevelFog.BoundsMin, LevelFog.BoundsMax), LevelFog.CellSize);
	OutGrid.MergeBits(LevelFog.Bits);
	return true;
}

bool UMDFogComponent::EnsureGrid()
{
	const AMDLevelMap* Map = AMDLevelMap::FindLevelMap(this);
	if (!Map)
	{
		return false;
	}
	FMDFogGrid Wanted;
	Wanted.Init(Map->GetMapBounds(), Map->GetFogCellSize());
	if (!Grid.HasSameLayout(Wanted))
	{
		// New or moved level map (e.g. recaptured after generation): the old bits no longer line up, start hidden.
		Grid = Wanted;
		Publish();
		OnFogChanged.Broadcast();
	}
	return true;
}

void UMDFogComponent::Publish()
{
	LevelFog.BoundsMin = Grid.Bounds.Min;
	LevelFog.BoundsMax = Grid.Bounds.Max;
	LevelFog.CellSize = Grid.CellSize;
	LevelFog.Bits = Grid.Bits;
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

	if (!EnsureGrid())
	{
		return;
	}
	bool bChanged = false;
	for (const FVector2D& Position : Positions)
	{
		bChanged |= Grid.RevealRadius(Position, RevealRadius);
	}
	if (bChanged)
	{
		Publish();
		OnFogChanged.Broadcast();
	}
}

void UMDFogComponent::RevealAll()
{
	if (!IsServer() || !EnsureGrid())
	{
		return;
	}
	Grid.RevealAll();
	Publish();
	OnFogChanged.Broadcast();
}

void UMDFogComponent::ResetFog()
{
	if (!IsServer())
	{
		return;
	}
	if (EnsureGrid())
	{
		Grid.Clear();
		Publish();
	}
	OnFogChanged.Broadcast();
}

void UMDFogComponent::OnRep_Fog()
{
	OnFogChanged.Broadcast();
}
