#include "MDGameState.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"

AMDGameState::AMDGameState()
{
}

void AMDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDGameState, bFriendlyFireEnabled);
	DOREPLIFETIME(AMDGameState, bHasCheckpoint);
	DOREPLIFETIME(AMDGameState, CheckpointTransform);
	DOREPLIFETIME(AMDGameState, CheckpointOrder);
	DOREPLIFETIME(AMDGameState, ZonesCompleted);
	DOREPLIFETIME(AMDGameState, SoulsSaved);
}

void AMDGameState::SetZonesCompleted(int32 Zones)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 NewZones = FMath::Clamp(Zones, 0, 4);
	if (NewZones != ZonesCompleted)
	{
		ZonesCompleted = NewZones;
		OnRunProgressChanged.Broadcast();
	}
}

void AMDGameState::AddSoulsSaved(int32 Delta)
{
	SetSoulsSaved(SoulsSaved + Delta);
}

void AMDGameState::SetSoulsSaved(int32 Souls)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 NewSouls = FMath::Max(Souls, 0);
	if (NewSouls != SoulsSaved)
	{
		SoulsSaved = NewSouls;
		OnRunProgressChanged.Broadcast();
	}
}

void AMDGameState::OnRep_RunProgress()
{
	OnRunProgressChanged.Broadcast();
}

void AMDGameState::SetCheckpoint(const FTransform& Transform, int32 Order)
{
	if (!HasAuthority() || (bHasCheckpoint && Order < CheckpointOrder))
	{
		return;
	}
	bHasCheckpoint = true;
	CheckpointTransform = Transform;
	CheckpointOrder = Order;
}

bool AMDGameState::GetCheckpoint(FTransform& OutTransform) const
{
	OutTransform = CheckpointTransform;
	return bHasCheckpoint;
}

void AMDGameState::SetFriendlyFireEnabled(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}
	if (bFriendlyFireEnabled != bEnabled)
	{
		bFriendlyFireEnabled = bEnabled;
		OnFriendlyFireChanged.Broadcast(bFriendlyFireEnabled);
	}
}

bool AMDGameState::IsFriendlyFireEnabled(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AMDGameState* State = World ? World->GetGameState<AMDGameState>() : nullptr;
	return State ? State->bFriendlyFireEnabled : true;
}

void AMDGameState::OnRep_FriendlyFire()
{
	OnFriendlyFireChanged.Broadcast(bFriendlyFireEnabled);
}

static FAutoConsoleCommandWithWorldAndArgs GMDFriendlyFireCommand(
	TEXT("md.FriendlyFire"),
	TEXT("md.FriendlyFire <0|1> - enable/disable player friendly fire (server/standalone only)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		AMDGameState* State = World->GetGameState<AMDGameState>();
		if (!State)
		{
			UE_LOG(LogTemp, Warning, TEXT("md.FriendlyFire: no AMDGameState in this world (set GameStateClass)."));
			return;
		}
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Log, TEXT("md.FriendlyFire = %d"), State->bFriendlyFireEnabled ? 1 : 0);
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("md.FriendlyFire: can only be changed on the server."));
			return;
		}
		State->SetFriendlyFireEnabled(FCString::Atoi(*Args[0]) != 0);
		UE_LOG(LogTemp, Log, TEXT("md.FriendlyFire set to %d"), State->bFriendlyFireEnabled ? 1 : 0);
	}));
