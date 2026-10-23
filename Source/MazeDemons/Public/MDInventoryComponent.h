#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDInventoryTypes.h"
#include "MDUITypes.h"
#include "MDInventoryComponent.generated.h"

class APawn;
class UDataTable;
class UMDAbilitySystemComponent;
class UMDComboSet;

MAZEDEMONS_API DECLARE_LOG_CATEGORY_EXTERN(LogMDInventory, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDEquipmentChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDQuickSlotsChanged);

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

	// Deducts gold. All-or-nothing: false (nothing changes) if Amount is negative or exceeds the balance.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	bool SpendGold(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	bool CanAfford(int32 Amount) const { return Amount >= 0 && GoldAmount >= Amount; }

	// Removes Qty units of Row across stacks. All-or-nothing: false (nothing changes) if the inventory holds fewer.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Inventory")
	bool RemoveItemByRow(FName Row, int32 Qty);

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

	// DT_ItemStats (FMDItemStatsRow by item row name). Optional: a missing table means no bonuses.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TSoftObjectPtr<UDataTable> ItemStatsTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Items/DT_ItemStats.DT_ItemStats")));

	// True if the item row has a DT_Consumables row.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	bool IsConsumable(FName ItemRowName) const;

	// Health restored by a consumable row (0 if it is not one).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetConsumableHeal(FName ItemRowName) const;

	// Equip slot (E_ItemTypes value) for a row, or 255 if not equippable. Public wrapper for UI / stat comparison.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	uint8 GetItemEquipSlot(FName ItemRowName) const { return GetSlotForItem(ItemRowName); }

	// ---- Item reads (DT_Items column Rarity_*, ArmorDamageReductionPercent_*; DT_ItemStats) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	EMDRarity GetItemRarity(FName ItemRowName) const;

	// Damage reduction percent of an armor row (0 if none).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetItemArmorReduction(FName ItemRowName) const;

	// Attack power bonus of a weapon row from DT_ItemStats (0 if the table or row is missing).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetItemAttackBonus(FName ItemRowName) const;

	// C1: the combo data of a weapon row from DT_ItemStats.ComboSet (loads it; null if the table, row or field is empty, which
	// means the weapon keeps the old DoLightAttack path).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	UMDComboSet* GetItemComboSet(FName ItemRowName) const;

	// C1 dual wield: DT_ItemStats.DualComboSet of a weapon row (null when empty).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	UMDComboSet* GetItemDualComboSet(FName ItemRowName) const;

	// C1: the combo set for what is equipped right now (reads the Equipped array, so call it after the array changed).
	// Two-hander: its ComboSet. Main hand + off hand whose rows share the same non-empty DualComboSet: that dual set.
	// Otherwise the main hand's ComboSet. Null = old DoLightAttack path. Call on every machine that plays montages.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	UMDComboSet* ResolveEquippedComboSet() const;

	// ---- Durability / upgrades (DT_ItemStats columns; condition and upgrade level are per equipped slot) ----
	// Max durability of a row (0 = none). Rows with durability never stack.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetItemMaxDurability(FName ItemRowName) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetItemValue(FName ItemRowName) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	bool IsItemUpgradeable(FName ItemRowName) const;

	// Upgrade level of an equipment slot (0 if never upgraded). Per slot: a replacement item inherits it.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetSlotUpgradeLevel(uint8 Slot) const;

	// Durability of the item equipped in Slot. Max is 0 when the slot is empty or the item has no durability.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	void GetSlotDurability(uint8 Slot, int32& Cur, int32& Max) const;

	// Durability of the bag stack at BagIndex (Max 0 when it has none or the index is invalid).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	void GetItemDurability(int32 BagIndex, int32& Cur, int32& Max) const;

	// Effectiveness multiplier (0.5..1) from the equipped item's durability; 1 for empty / non-durable slots.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetSlotConditionMultiplier(uint8 Slot) const;

	// Effective stat of the item equipped in Slot, for current -> next displays without maths in Blueprint: damage reduction
	// percent for armor slots (4-8), AttackPower (base + weapon) for weapon slots (0, 1, 3), 0 for an empty slot or Shield.
	// LevelOverride -1 = the slot's current upgrade level, otherwise that level; bAssumeFullDurability ignores wear.
	// The weapon base is DA_BalanceReference StartingAttackPower (client-safe), so it can differ from the live attribute only
	// if the attribute was changed at runtime.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetSlotEffectiveStat(uint8 Slot, int32 LevelOverride, bool bAssumeFullDurability) const;

	// Total Upgrade Shards (FMDEconomyTuning ShardRow) in the bag.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetShardCount() const;

	// Slot of the weapon in use: TwoHanded if equipped, else MainHand, else 255.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	uint8 GetPrimaryWeaponSlot() const;

	// ---- Stations (server validates everything; Station must be an AMDStation within range that offers the service) ----
	// Slot 255 = repair every damaged equipped item (all-or-nothing on gold).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void RequestRepair(AActor* Station, uint8 Slot);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void RequestUpgrade(AActor* Station, uint8 Slot);

	// The same checks the server runs, for the UI to grey buttons. OutCost / OutGold / OutShards are filled whenever they
	// could be computed, including for NotEnoughGold / NotEnoughShards.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	EMDStationReject GetRepairReject(const AActor* Station, uint8 Slot, int32& OutCost) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	EMDStationReject GetUpgradeReject(const AActor* Station, uint8 Slot, int32& OutGold, int32& OutShards) const;

	// Pure: player-facing text for a rejection. Needed / Have feed the funds messages ("Not enough gold (need 34)",
	// "Not enough Shards (2/3)"); ignored for other reasons.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	static FText GetStationRejectText(EMDStationReject Reason, int32 Needed = 0, int32 Have = 0);

	// Authority: subscribes to the owner's ASC so hits on the owner wear armor. Idempotent; call whenever the ASC may have
	// changed (AMDPlayerState does after every pawn set).
	void BindToASC();

	// Authority: every equipped item with durability loses ceil(Max * DeathDurabilityLossPct). Called on death.
	void ApplyDeathPenalty();

	// Authority: Instigator's main weapon wears by one (no-op for self damage, instigators without an inventory, or weapons
	// without durability). Called from the victim's ApplyDamage.
	static void NotifyHitLanded(AActor* Instigator, const AActor* Victim);

	// Pure rule: maps the Blueprint enum byte to a rarity; anything outside 0..4 is Common.
	static EMDRarity RarityFromByte(int32 Value);

	// ---- Consumable cooldown (readable on the owning client) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetConsumableCooldownRemaining() const;

	// 1 = just used, 0 = ready.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	float GetConsumableCooldownFraction() const;

	// Pure rule: fraction of the cooldown still remaining in [0,1]; 0 when Duration <= 0.
	static float CooldownFraction(double Now, double ReadyAt, float Duration);

	// Pure rule: a heal is accepted only for a living owner below max health once the cooldown has elapsed.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	static bool CanUseHeal(float Health, float MaxHealth, bool bDead, double Now, double ReadyAt);

	// Pure rule: why CanUseHeal would refuse (None when it would accept). Priority: Dead, OnCooldown, HealthFull.
	static EMDUseRejectReason GetUseRejectReason(float Health, float MaxHealth, bool bDead, double Now, double ReadyAt);

	// Pure: player-facing toast text for a rejection (LOCTEXT). CooldownRemaining is rounded up to whole seconds.
	static FText GetUseRejectText(EMDUseRejectReason Reason, float CooldownRemaining);

	// ---- Quick slots (consumables only). Store item row names, not inventory indices. Owner-only replication. ----
	static constexpr int32 NumQuickSlots = 2;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDQuickSlotsChanged OnQuickSlotsChanged;

	// Client entry points: direct call when authoritative, else the validated RPC. InventoryIndex INDEX_NONE clears the slot.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestAssignQuickSlot(int32 QuickIndex, int32 InventoryIndex);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestUseQuickSlot(int32 QuickIndex);

	// Item row assigned to the quick slot (NAME_None if unassigned / out of range).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	FName GetQuickSlotItem(int32 QuickIndex) const;

	// Total quantity of the slot's item across all stacks (0 = assigned but empty).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	int32 GetQuickSlotCount(int32 QuickIndex) const;

	// Pure rule: a row may be assigned only if it has a row in the consumable table.
	static bool IsValidQuickAssignment(FName Row, const UDataTable* InConsumableTable);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Inventory")
	const TArray<FName>& GetQuickSlots() const { return QuickSlots; }

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

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Repair(AActor* Station, uint8 Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Upgrade(AActor* Station, uint8 Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AssignQuickSlot(int32 QuickIndex, int32 InventoryIndex);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_UseQuickSlot(int32 QuickIndex);

	UFUNCTION()
	void OnRep_QuickSlots();

	// Item row names of the quick slots (size NumQuickSlots). Replicates to the owner only.
	UPROPERTY(ReplicatedUsing = OnRep_QuickSlots, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FName> QuickSlots;

	// Server clock (AGameStateBase::GetServerWorldTimeSeconds) at which the next consumable may be used, and the length of
	// the cooldown just started. Owner-only so the client HUD can draw the quick slot sweep.
	UPROPERTY(ReplicatedUsing = OnRep_ConsumableCooldown)
	float ConsumableReadyAtServer = 0.0f;

	UPROPERTY(Replicated)
	float ConsumableCooldownDuration = 0.0f;

	UFUNCTION()
	void OnRep_ConsumableCooldown();

	UFUNCTION()
	void OnRep_Items();

	UFUNCTION()
	void OnRep_Gold();

	UFUNCTION()
	void OnRep_Equipped();

	UFUNCTION()
	void OnRep_Condition();

	UFUNCTION()
	void OnRep_SlotUpgrades();

	UPROPERTY(ReplicatedUsing = OnRep_Items, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDItemStack> Items;

	UPROPERTY(ReplicatedUsing = OnRep_Gold, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	int32 GoldAmount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Equipped, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDEquippedSlot> Equipped;

	// Durability per equipped slot (only slots whose item has durability). Owner-only: it changes on every hit and must not
	// re-run the Equipped visual diff on every client.
	UPROPERTY(ReplicatedUsing = OnRep_Condition, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDSlotCondition> EquippedCondition;

	// Upgrade level per equipment slot (only slots above level 0). Owner-only.
	UPROPERTY(ReplicatedUsing = OnRep_SlotUpgrades, BlueprintReadOnly, Category = "Maze Demons|Inventory")
	TArray<FMDSlotUpgrade> SlotUpgrades;

private:
	// Client-side snapshot used by OnRep_Equipped to diff what changed.
	TArray<FMDEquippedSlot> PrevEquipped;

	// Lets the automation tests (Private/Tests/MDInventoryTests.cpp) reach protected/private members.
	friend struct FMDInventoryTestAccess;
	friend struct FMDQuickSlotTestAccess;
	friend struct FMDUpgradeTestAccess;
	// Debug exec commands (ListItemRows) read GetSlotForItem.
	friend class AMDPlayerController;

	bool IsValidRow(FName Row) const;

	// Durability an instance of Row should carry given a stored value: MD_NoDurability for rows without durability, Max for
	// a negative stored value, else the stored value clamped to Max.
	int32 ResolveDurabilityForRow(FName Row, int32 StoredDurability) const;
	int32 GetConditionDurability(uint8 Slot) const;
	void SetConditionDurability(uint8 Slot, int32 Durability);
	int32 GetEquippedMaxDurability(uint8 Slot) const;
	const FMDEquippedSlot* FindEquippedSlot(uint8 Slot) const;
	void SetSlotUpgradeLevel(uint8 Slot, int32 Level);

	// Like AddItem for one existing instance: keeps its Durability. Durable rows take their own stack.
	bool AddItemInstance(const FMDItemStack& Instance);

	// Re-pushes the effective armor reductions and weapon modifiers to the ASC (authority).
	void RefreshEquippedStats();
	void PushWeaponModifiers();
	void ApplyArmorWear();
	void ApplyWeaponWear();
	void HandleDamageApplied(AActor* Instigator, float Applied);

	EMDStationReject CheckStationAccess(const AActor* Station, uint8 ServiceFlag) const;
	bool Repair_Impl(AActor* Station, uint8 Slot);
	bool Upgrade_Impl(AActor* Station, uint8 Slot);
	// Authority: tells the owning client (generic HUD toast). No-op without a controller.
	void SendToast(const FText& Message, EMDColor Tone) const;

	// Debug exec support (AMDPlayerController is a friend). Slot 255 = every equipped item with durability.
	void DebugSetDurability(uint8 Slot, int32 Value);
	void DebugSetUpgradeLevel(uint8 Slot, int32 Level);

	TWeakObjectPtr<UMDAbilitySystemComponent> BoundASC;
	void ApplyArmorReduction(uint8 Slot, FName Row);
	// Slots 0 MainHand, 1 OffHand, 3 TwoHanded feed the ASC's weapon attack-power bonuses; other slots are ignored.
	void ApplyWeaponBonus(uint8 Slot, FName Row);
	const FMDItemStatsRow* FindItemStats(FName Row) const;
	// Current server clock: GameState server time when available, else world time.
	double GetServerNow() const;
	bool UseItem_Impl(int32 Index);
	// Authority: tells the owning client why a use was refused (toast). No-op without a controller.
	void NotifyUseRejected(EMDUseRejectReason Reason, float CooldownRemaining) const;
	// Server logic (no authority check; callers are the authority paths). Tests call these directly.
	bool AssignQuickSlot_Impl(int32 QuickIndex, int32 InventoryIndex);
	bool UseQuickSlot_Impl(int32 QuickIndex);
	// Index of the first stack of the slot's item, or INDEX_NONE (unassigned / out of stock).
	int32 ResolveQuickSlotIndex(int32 QuickIndex) const;
	// Clears a slot only if its row is no longer a consumable; empty consumables stay assigned.
	void ValidateQuickSlots();
	class UMDAbilitySystemComponent* ResolveASC() const;
	const FMDConsumableRow* FindConsumable(FName Row) const;

	// Server world time (seconds) at which the next consumable may be used.
	double ConsumableReadyAt = 0.0;

	// Set once DT_ItemStats is known not to exist, so pure reads do not hit the disk again.
	mutable bool bItemStatsMissing = false;
	void EquipItem_Impl(int32 Index);
	void UnequipSlot_Impl(uint8 Slot);
	void DropItem_Impl(int32 Index, int32 Quantity);
	bool HasRoomForItem(FName Row, int32 Qty) const;
};
