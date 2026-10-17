#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDInventoryTypes.h"
#include "MDInventoryComponent.generated.h"

class APawn;
class UDataTable;

MAZEDEMONS_API DECLARE_LOG_CATEGORY_EXTERN(LogMDInventory, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDEquipmentChanged);

// Server-authoritative inventory. Items/GoldAmount replicate to the owner only; Equipped replicates to everyone.
// Item data (slot type, max stack) is read from ItemTable (DT_Items) by row name.
UCLASS(Blueprintable, BlueprintType, ClassGroup = (MazeDemons), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory", meta = (ClampMin = "1"))
	int32 MaxSlots = 20;

	// DT_Items. Used server-side to validate rows, find an item's equip slot (column itemType) and max stack (column maxStack).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TObjectPtr<UDataTable> ItemTable;

	// Max distance in front of the owner at which dropped items spawn.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	float DropDistance = 120.0f;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDEquipmentChanged OnEquipmentChanged;

	// The pawn this inventory acts through. The owner may be the pawn itself, its controller or its PlayerState.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	APawn* GetOwnerPawn() const;

	// Authority only: re-fires K2_ApplyEquipment for every equipped slot and the armor reductions, then broadcasts
	// OnEquipmentChanged. Call after the avatar pawn changes (respawn) so the new pawn shows/uses its equipment.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	void ReapplyAllEquipment();

	// ---- Authority only ----
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	bool AddItem(FName ItemRowName, int32 Quantity);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	bool RemoveItem(int32 Index, int32 Quantity);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	void AddGold(int32 Amount);

	// ---- Client-callable requests (RPC, or direct call when already authoritative) ----
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestEquip(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestUnequip(uint8 Slot);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestDrop(int32 Index, int32 Quantity);

	// Server-authoritative delete (no world pickup spawned).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestDestroy(int32 Index, int32 Quantity);

	// Uses one unit of the consumable at Index (heals the owner). Server validates; no-op if it fails.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestUseItem(int32 Index);

	// DT_Consumables (FMDConsumableRow by item row name). Loaded on demand.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TSoftObjectPtr<UDataTable> ConsumableTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Items/DT_Consumables.DT_Consumables")));

	// True if the item row has a DT_Consumables row.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	bool IsConsumable(FName ItemRowName) const;

	// Pure rule: a heal is accepted only for a living owner below max health once the cooldown has elapsed.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	static bool CanUseHeal(float Health, float MaxHealth, bool bDead, double Now, double ReadyAt);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	const TArray<FMDItemStack>& GetItems() const { return Items; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	const TArray<FMDEquippedSlot>& GetEquipped() const { return Equipped; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetGold() const { return GoldAmount; }

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_EquipItem(int32 Index);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_UnequipSlot(uint8 Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_DropItem(int32 Index, int32 Quantity);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_DestroyItem(int32 Index, int32 Quantity);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_UseItem(int32 Index);

	// Called on the server after an item is equipped. BP applies mesh/stats/abilities (and handles two-handed rules).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Inventory", meta = (DisplayName = "Apply Equipment"))
	void K2_ApplyEquipment(uint8 Slot, FName ItemRowName);

	// Called on the server after a slot is cleared.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Inventory", meta = (DisplayName = "Clear Equipment"))
	void K2_ClearEquipment(uint8 Slot);

	// Called on the server to spawn a replicated pickup in the world.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Inventory", meta = (DisplayName = "Spawn Drop"))
	void K2_SpawnDrop(FName ItemRowName, int32 Quantity, FVector Location);

	// Equip slot (E_ItemTypes value) for a row, or 255 if the row is missing / not equippable. Default reads ItemTable.
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Maze Demons|Inventory")
	uint8 GetSlotForItem(FName ItemRowName) const;

	// Max stack for a row (>=1). Default reads ItemTable column maxStack.
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetMaxStackForItem(FName ItemRowName) const;

	UFUNCTION()
	void OnRep_Items();

	UFUNCTION()
	void OnRep_Gold();

	UFUNCTION()
	void OnRep_Equipped();

	UPROPERTY(ReplicatedUsing = OnRep_Items, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDItemStack> Items;

	UPROPERTY(ReplicatedUsing = OnRep_Gold, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	int32 GoldAmount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Equipped, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDEquippedSlot> Equipped;

private:
	// Client-side snapshot used by OnRep_Equipped to diff what changed.
	TArray<FMDEquippedSlot> PrevEquipped;

	// Lets the automation tests (Private/Tests/MDInventoryTests.cpp) reach protected/private members.
	friend struct FMDInventoryTestAccess;
	// Debug exec commands (ListItemRows) read GetSlotForItem.
	friend class AMDPlayerController;

	bool IsValidRow(FName Row) const;
	void ApplyArmorReduction(uint8 Slot, FName Row);
	void UseItem_Impl(int32 Index);
	class UMDAbilitySystemComponent* ResolveASC() const;
	const FMDConsumableRow* FindConsumable(FName Row) const;

	// Server world time (seconds) at which the next consumable may be used.
	double ConsumableReadyAt = 0.0;
	void EquipItem_Impl(int32 Index);
	void UnequipSlot_Impl(uint8 Slot);
	void DropItem_Impl(int32 Index, int32 Quantity);
	bool HasRoomForItem(FName Row, int32 Qty) const;
};
