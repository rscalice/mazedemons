#include "MDPlayerController.h"

#include "MDAbilitySystemComponent.h"
#include "MDPlayerState.h"
#include "MDInventoryComponent.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

#if !UE_BUILD_SHIPPING
namespace
{
	// Inventory may live on the pawn, the controller or the PlayerState.
	UMDInventoryComponent* FindInventoryFor(const APawn* Pawn, const AController* Controller)
	{
		UMDInventoryComponent* Inv = Pawn ? Pawn->FindComponentByClass<UMDInventoryComponent>() : nullptr;
		if (!Inv && Controller)
		{
			Inv = Controller->FindComponentByClass<UMDInventoryComponent>();
		}
		if (!Inv)
		{
			const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : (Controller ? Controller->PlayerState.Get() : nullptr);
			Inv = PS ? PS->FindComponentByClass<UMDInventoryComponent>() : nullptr;
		}
		return Inv;
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
