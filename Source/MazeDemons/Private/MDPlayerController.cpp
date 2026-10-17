#include "MDPlayerController.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDGameState.h"
#include "MDPlayerState.h"
#include "MDInventoryComponent.h"
#include "MDPickupBase.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Blueprint/UserWidget.h"
#include "MDUISettings.h"

void AMDPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || HUDWidget)
	{
		return;
	}
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	if (const TSubclassOf<UUserWidget> HUDClass = Settings ? Settings->HUDWidgetClass.LoadSynchronous() : nullptr)
	{
		HUDWidget = CreateWidget<UUserWidget>(this, HUDClass);
		if (HUDWidget)
		{
			HUDWidget->AddToViewport(0);
		}
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	// Inventory may live on the pawn, the controller or the PlayerState; GetInventory checks all of them.
	UMDInventoryComponent* FindInventoryFor(const APawn* Pawn, const AController* Controller)
	{
		UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(Pawn);
		return Inv ? Inv : UMDGameplayStatics::GetInventory(Controller);
	}

	const TCHAR* NetModeName(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	void LogInventoryBlock(const UMDInventoryComponent* Inv, const AActor* Subject, const UWorld* World)
	{
		const ENetMode Mode = World ? World->GetNetMode() : NM_Standalone;
		const bool bServer = Mode != NM_Client;
		const APawn* Pawn = Cast<APawn>(Subject);
		const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
		const UEnum* RoleEnum = StaticEnum<ENetRole>();
		const FString Role = RoleEnum ? RoleEnum->GetNameStringByValue(static_cast<int64>(Subject->GetLocalRole())) : FString();
		const FString RemoteRole = RoleEnum ? RoleEnum->GetNameStringByValue(static_cast<int64>(Subject->GetRemoteRole())) : FString();

		UE_LOG(LogMDInventory, Warning, TEXT("==== Inventory [%s] NetMode=%s LocalRole=%s RemoteRole=%s Player=%s Actor=%s ===="),
			bServer ? TEXT("Server") : TEXT("Client"), NetModeName(Mode), *Role, *RemoteRole,
			PS ? *PS->GetPlayerName() : TEXT("<none>"), *GetNameSafe(Subject));
		UE_LOG(LogMDInventory, Warning, TEXT("  Gold: %d"), Inv->GetGold());
		const TArray<FMDItemStack>& Items = Inv->GetItems();
		UE_LOG(LogMDInventory, Warning, TEXT("  Items (%d):"), Items.Num());
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			UE_LOG(LogMDInventory, Warning, TEXT("    %d %s x%d"), i, *Items[i].ItemRowName.ToString(), Items[i].Quantity);
		}
		const TArray<FMDEquippedSlot>& Equipped = Inv->GetEquipped();
		UE_LOG(LogMDInventory, Warning, TEXT("  Equipped (%d):"), Equipped.Num());
		for (const FMDEquippedSlot& E : Equipped)
		{
			UE_LOG(LogMDInventory, Warning, TEXT("    slot %d %s"), E.Slot, *E.ItemRowName.ToString());
		}
	}
}
#endif

// ---- Respawn ----

void AMDPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Server only (OnPossess is authority-side). One binding for the PlayerState's lifetime.
	if (const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>())
	{
		if (UMDAbilitySystemComponent* ASC = PS->GetMDAbilitySystemComponent())
		{
			ASC->OnDeath.AddUniqueDynamic(this, &AMDPlayerController::HandleOwnerDeath);
		}
	}
}

void AMDPlayerController::HandleOwnerDeath()
{
	if (!HasAuthority() || GetWorldTimerManager().IsTimerActive(RespawnTimer))
	{
		return;
	}
	const float Delay = FMath::Max(RespawnDelay, 0.01f);
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &AMDPlayerController::Respawn, Delay, false);

	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		RespawnAtServerTime = GS->GetServerWorldTimeSeconds() + Delay;
	}
}

void AMDPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMDPlayerController, RespawnAtServerTime, COND_OwnerOnly);
}

void AMDPlayerController::NotifyPickup(FName ItemRowName, int32 Quantity)
{
	if (!HasAuthority() || ItemRowName.IsNone() || Quantity <= 0)
	{
		return;
	}
	if (IsLocalController())
	{
		OnPickupNotified.Broadcast(ItemRowName, Quantity); // listen-server host: no RPC hop
	}
	else
	{
		Client_NotifyPickup(ItemRowName, Quantity);
	}
}

void AMDPlayerController::Client_NotifyPickup_Implementation(FName ItemRowName, int32 Quantity)
{
	OnPickupNotified.Broadcast(ItemRowName, Quantity);
}

bool AMDPlayerController::FindRespawnTransform(FTransform& OutTransform) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. Party: near the first living teammate that has a pawn.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Other = It->Get();
		if (!Other || Other == this)
		{
			continue;
		}
		const APawn* Mate = Other->GetPawn();
		const AMDPlayerState* MatePS = Other->GetPlayerState<AMDPlayerState>();
		const UMDAbilitySystemComponent* MateASC = MatePS ? MatePS->GetMDAbilitySystemComponent() : nullptr;
		if (!Mate || !MateASC || MateASC->IsDead())
		{
			continue;
		}

		const float HalfHeight = 90.0f;
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(34.0f, HalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRespawnSpot), false, Mate);
		const float StartAngle = FMath::FRandRange(0.0f, 360.0f);
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector Dir = FRotator(0.0f, StartAngle + i * 45.0f, 0.0f).Vector();
			const FVector Base = Mate->GetActorLocation() + Dir * PartySpawnRadius;

			// Find the floor under the candidate.
			FHitResult Floor;
			const FVector TraceStart = Base + FVector(0.0f, 0.0f, 200.0f);
			const FVector TraceEnd = Base - FVector(0.0f, 0.0f, 600.0f);
			if (!World->LineTraceSingleByChannel(Floor, TraceStart, TraceEnd, ECC_WorldStatic, Params))
			{
				continue;
			}
			const FVector Spot = Floor.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 5.0f);
			if (World->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				continue; // inside a wall or another actor
			}
			OutTransform = FTransform(FRotator(0.0f, (-Dir).Rotation().Yaw, 0.0f), Spot);
			return true;
		}
		// No free spot around this teammate: fall back to the checkpoint rather than a wall.
		break;
	}

	// 2. Solo (or no free spot near the party): the last zone checkpoint.
	if (const AMDGameState* State = World->GetGameState<AMDGameState>())
	{
		if (State->GetCheckpoint(OutTransform))
		{
			return true;
		}
	}

	// 3. Caller falls back to a level PlayerStart.
	return false;
}

void AMDPlayerController::Respawn()
{
	AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	if (!HasAuthority() || !GameMode)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	RespawnAtServerTime = 0.0;

	// Only dead players respawn (the timer can fire after a revive or disconnect-reconnect).
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (!ASC || !ASC->IsDead())
	{
		return;
	}

	FTransform Spot;
	const bool bHaveSpot = FindRespawnTransform(Spot);

	if (APawn* CorpsePawn = GetPawn())
	{
		UnPossess();
		CorpsePawn->Destroy(); // no persistent corpse in the slice
	}

	// Possessing the new pawn makes AMDPlayerState reset health, clear DeadPawn and re-apply equipment.
	if (bHaveSpot)
	{
		GameMode->RestartPlayerAtTransform(this, Spot);
	}
	else
	{
		GameMode->RestartPlayer(this);
	}
	Client_OnRespawned();
}

void AMDPlayerController::Client_OnRespawned_Implementation()
{
	ResetIgnoreInputFlags();
	EnableInput(this);
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
}

void AMDPlayerController::RequestPickup(AActor* Pickup)
{
	if (Pickup)
	{
		Server_RequestPickup(Pickup);
	}
}

void AMDPlayerController::Server_RequestPickup_Implementation(AActor* Pickup)
{
	AMDPickupBase* PickupActor = Cast<AMDPickupBase>(Pickup);
	APawn* PlayerPawn = GetPawn();
	if (!PickupActor || !PlayerPawn)
	{
		return;
	}
	// The client only asks; the server re-validates range so a client cannot grab pickups across the map.
	if (FVector::DistSquared(PlayerPawn->GetActorLocation(), PickupActor->GetActorLocation()) > FMath::Square(MaxPickupDistance))
	{
		return;
	}
	PickupActor->TryPickup(PlayerPawn);
}
void AMDPlayerController::KillPlayer()
{
#if !UE_BUILD_SHIPPING
	Server_KillPlayer();
#endif
}

void AMDPlayerController::Server_KillPlayer_Implementation()
{
#if !UE_BUILD_SHIPPING
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (ASC && !ASC->IsDead())
	{
		// Instigator == avatar pawn, so the team/friendly-fire gate in CanDamage lets self-damage through.
		ASC->ApplyDamage(1.0e9f, GetPawn(), GetPawn());
	}
#endif
}

void AMDPlayerController::GiveItem(FName RowName, int32 Qty)
{
#if !UE_BUILD_SHIPPING
	Server_GiveItem(RowName, Qty);
#endif
}

void AMDPlayerController::Server_GiveItem_Implementation(FName RowName, int32 Qty)
{
#if !UE_BUILD_SHIPPING
	UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this);
	if (!Inv)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("GiveItem: no UMDInventoryComponent found for %s"), *GetNameSafe(this));
		return;
	}
	const bool bOk = Inv->AddItem(RowName, Qty);
	UE_LOG(LogMDInventory, Warning, TEXT("GiveItem %s x%d -> %s"), *RowName.ToString(), Qty, bOk ? TEXT("added") : TEXT("rejected (invalid row, qty or inventory full)"));
#endif
}

void AMDPlayerController::DumpInventory()
{
#if !UE_BUILD_SHIPPING
	const APawn* P = GetPawn();
	const UMDInventoryComponent* Inv = FindInventoryFor(P, this);
	if (!Inv)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("DumpInventory: no UMDInventoryComponent for local player %s"), *GetNameSafe(this));
		return;
	}
	LogInventoryBlock(Inv, P ? static_cast<const AActor*>(P) : static_cast<const AActor*>(this), GetWorld());
#endif
}

void AMDPlayerController::DumpAllInventories()
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	int32 Count = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const UMDInventoryComponent* Inv = FindInventoryFor(*It, It->GetController());
		if (Inv)
		{
			LogInventoryBlock(Inv, *It, World);
			++Count;
		}
	}
	UE_LOG(LogMDInventory, Warning, TEXT("DumpAllInventories: %d pawn(s) with an inventory visible on this machine"), Count);
#endif
}

void AMDPlayerController::ListItemRows()
{
#if !UE_BUILD_SHIPPING
	const UMDInventoryComponent* Existing = FindInventoryFor(GetPawn(), this);
	UMDInventoryComponent* Temp = nullptr;
	const UMDInventoryComponent* Inv = Existing;
	if (!Inv || !Inv->ItemTable)
	{
		Temp = NewObject<UMDInventoryComponent>(GetTransientPackage());
		Temp->ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Items/DT_Items"));
		Inv = Temp;
	}
	if (!Inv->ItemTable)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("ListItemRows: could not load /Game/Blueprints/Items/DT_Items"));
		return;
	}
	const TArray<FName> Rows = Inv->ItemTable->GetRowNames();
	UE_LOG(LogMDInventory, Warning, TEXT("ListItemRows: %d rows (slot 0 MainHand,1 OffHand,2 Shield,3 TwoHanded,4 Helmet,5 Chest,6 Gloves,7 Legs,8 Boots,9 Gold,255 none)"), Rows.Num());
	for (const FName Row : Rows)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("  %s  slot=%d  maxStack=%d"), *Row.ToString(), Inv->GetSlotForItem(Row), Inv->GetMaxStackForItem(Row));
	}
#endif
}
