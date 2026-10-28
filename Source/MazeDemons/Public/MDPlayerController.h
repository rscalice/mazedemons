#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MDInventoryTypes.h"
#include "MDUITypes.h"
#include "MDPlayerController.generated.h"

class AMDStation;
class UMDGameMenuWidget;
class UMDMapComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDPickupNotified, FName, ItemRowName, int32, Quantity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDUseRejected, EMDUseRejectReason, Reason, float, CooldownRemaining);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDToastNotified, FText, Message, EMDColor, Tone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDStationOpened, AMDStation*, Station);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDStationClosed);

class UInputAction;
struct FInputActionValue;

// Local only: the interactable the player is looking at changed (nullptr = none). Prompt is the text for the HUD.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDInteractFocusChanged, AActor*, Focus, FText, Prompt);
// Local only: the last input came from a gamepad (true) or keyboard/mouse (false) - drives glyph switching.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDInputDeviceChanged, bool, bGamepad);

UCLASS()
class MAZEDEMONS_API AMDPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMDPlayerController();

	// Local minimap state: current map zone, shared-fog texture, minimap options (U4). Not replicated.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Demons|Map")
	TObjectPtr<UMDMapComponent> MapComponent;

	// ---- Quick slots / pause input (assets set in BP_MDPlayerController defaults) ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> QuickSlot1Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> QuickSlot2Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> PauseAction;

	// Game menu (M1). Assets set in BP_MDPlayerController defaults. When an action is left empty, C++ binds a fallback key
	// instead (Tab and gamepad Menu / View toggle, P opens Perks, R opens Run).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> ToggleMenuAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> OpenPerksAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> OpenRunAction;

	// ---- Game menu (W_MDGameMenu): Inventory / Perks / Run tabs. Local only; the game is never paused. ----

	// W_MDGameMenu (parent UMDGameMenuWidget), set in BP_MDPlayerController defaults. Created lazily on first open.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Maze Demons|UI")
	TSubclassOf<UMDGameMenuWidget> GameMenuClass;

	// Opens on the last-used tab (Inventory the first time), or closes when open. Refused while the station panel, the pause
	// menu or another cursor-showing menu is open, and while dead.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void ToggleGameMenu();

	// Opens the menu on Tab; when already open on another tab, switches to it; when already on that tab, closes. None = last used tab.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void OpenGameMenuTab(EMDMenuTab Tab);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void CloseGameMenu();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	bool IsGameMenuOpen() const { return bGameMenuOpen && GameMenuWidget != nullptr; }

	// True while any menu owns the player's input: game menu, pause menu, station panel, or any widget showing the cursor.
	// Local only (menus never replicate). Quick slots and the melee gate use it.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	bool IsAnyMenuOpen() const { return IsGameMenuOpen() || IsPauseMenuOpen() || bStationOpen || bShowMouseCursor; }

	// Console / debug: `OpenMenu` toggles; `OpenMenuTab 2` opens Perks (1 Inventory, 2 Perks, 3 Run).
	UFUNCTION(Exec)
	void OpenMenu();

	UFUNCTION(Exec)
	void OpenMenuTab(int32 TabNumber);

	// Console / debug: Slot is 1 or 2.
	UFUNCTION(Exec)
	void UseQuickSlot(int32 Slot);

	// Console / debug: assigns inventory item InvIndex to quick slot Slot (1 or 2). InvIndex -1 clears.
	UFUNCTION(Exec)
	void AssignQuickSlot(int32 Slot, int32 InvIndex);

	// ---- Interaction focus (local only, never replicated) ----
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Interaction")
	FMDInteractFocusChanged OnInteractFocusChanged;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Interaction")
	AActor* GetInteractFocus() const { return InteractFocus.Get(); }

	// Called by the local focus trace (and callable from BP). Broadcasts only when focus or prompt changes.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Interaction")
	void SetInteractFocus(AActor* NewFocus, FText Prompt);

	// Optional: the interact ability BP (GA_PlayerInteract). Its InteractRange / InteractRadius / RequiredActorTag defaults
	// drive the local focus trace so it matches what F will actually interact with. Unset = class defaults.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Interaction")
	TSubclassOf<class UMDInteractAbility> InteractAbilityClass;

	// Seconds between local focus traces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Interaction", meta = (ClampMin = "0.02"))
	float FocusTraceInterval = 0.1f;

	// "Pick up <ItemName>", or "Interact" when ItemName is empty. Pure; used by the focus trace.
	static FText BuildInteractPrompt(const FText& ItemName);

	// True when focus (or its prompt) differs, i.e. SetInteractFocus would broadcast. Pure; used by tests.
	static bool WouldFocusChange(const AActor* OldFocus, const FText& OldPrompt, const AActor* NewFocus, const FText& NewPrompt);

	// ---- Input device (glyph switching) ----
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Input")
	FMDInputDeviceChanged OnInputDeviceChanged;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Input")
	bool IsUsingGamepad() const { return bUsingGamepad; }

	// ---- Pause menu: only a UI toggle, never SetPause / time dilation (co-op safe) ----
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void TogglePauseMenu();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	bool IsPauseMenuOpen() const { return PauseWidget != nullptr && bPauseMenuOpen; }

	// Called after a respawn so the pawn can drop sprint state. Implement in BP_MDPlayerController: call StopSprint
	// on the pawn (C++ already resets MaxWalkSpeed to the class default).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Respawn")
	void OnRespawnResetMovement(APawn* RespawnedPawn);

	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void SetupInputComponent() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server world time (AGameStateBase::GetServerWorldTimeSeconds) at which this player respawns; 0 while alive.
	// Replicated to the owner only; the HUD derives the death countdown from it.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Maze Demons|Respawn")
	double RespawnAtServerTime = 0.0;

	// Owning client: the server added Quantity of ItemRowName to this player's inventory (HUD toast).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDPickupNotified OnPickupNotified;

	// Authority only: tells the owning client a pickup succeeded.
	void NotifyPickup(FName ItemRowName, int32 Quantity);

	// Owning client: the server refused a consumable use (HUD toast).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Inventory")
	FMDUseRejected OnUseRejected;

	// Authority only: tells the owning client why a consumable use was refused.
	void NotifyUseRejected(EMDUseRejectReason Reason, float CooldownRemaining);

	// Owning client: a generic HUD toast from the server (gold, repair / upgrade results, station rejections).
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|UI")
	FMDToastNotified OnToastNotified;

	// Authority only: shows Message as a toast on the owning client; Tone picks the accent colour.
	void NotifyToast(const FText& Message, EMDColor Tone);

	// Authority only: a critical hit by this player's pawn landed (U3 crit feedback hook, no UI yet).
	void NotifyCrit(float FinalDamage);

	// Owning client: implement in Blueprint for crit feedback (floating number, sound). Unbound in U3.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnCritNotified(float FinalDamage);

	// ---- Stations (owning client only; the server just opens / the client closes by distance) ----
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Station")
	FMDStationOpened OnStationOpened;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Station")
	FMDStationClosed OnStationClosed;

	// Authority only: opens the station panel on the owning client.
	void NotifyStationOpened(AMDStation* Station);

	// Local: closes the panel state and fires OnStationClosed (also called when the player walks away or dies).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void CloseStation();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	AMDStation* GetOpenStation() const;

	// Seconds between death and respawn.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Respawn", meta = (ClampMin = "0"))
	float RespawnDelay = 5.0f;

	// Radius around a living party member within which a respawning player is placed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Respawn", meta = (ClampMin = "50"))
	float PartySpawnRadius = 250.0f;

	// Authority only. Respawns a dead player: near a living party member if there is one, else at the last zone
	// checkpoint (AMDGameState), else at a level PlayerStart. The old pawn (corpse) is destroyed.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Respawn")
	void Respawn();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Local-only HUD instance (created from UMDUISettings::HUDWidgetClass).
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Maze Demons|UI")
	TObjectPtr<class UUserWidget> HUDWidget;

	// Max distance between the pawn and a pickup for a client pickup request to be honoured.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Inventory", meta = (ClampMin = "0"))
	float MaxPickupDistance = 400.0f;

	// Local call (any machine): asks the server to pick Pickup up for this player. Interact input runs locally, but
	// pickups are server-authoritative, so clients must go through this RPC.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Inventory")
	void RequestPickup(AActor* Pickup);

	UFUNCTION(Server, Reliable)
	void Server_RequestPickup(AActor* Pickup);

	// Debug console command: kills the local player via the server. Body is compiled out in shipping builds.
	UFUNCTION(Exec)
	void KillPlayer();

	// Debug (non-shipping): server applies lethal self-damage. Body is compiled out in shipping builds.
	UFUNCTION(Server, Reliable)
	void Server_KillPlayer();

	// Debug (non-shipping): `DamageSelf 40` applies 40 raw damage to the local player via the server (Defense still applies).
	UFUNCTION(Exec)
	void DamageSelf(float Amount = 25.0f);

	UFUNCTION(Server, Reliable)
	void Server_DamageSelf(float Amount);

	// Debug (non-shipping): asks the server to add Qty of DT_Items row RowName to this player's inventory. Bodies compiled out in shipping.
	UFUNCTION(Exec)
	void GiveItem(FName RowName, int32 Qty = 1);

	UFUNCTION(Server, Reliable)
	void Server_GiveItem(FName RowName, int32 Qty);

	// Console: uses the consumable at inventory Index (until the S5 quick slots exist).
	UFUNCTION(Exec)
	void UseItem(int32 Index);

	// Console: equips the item at inventory Index (any slot, including the Ranged slot) through the same path as the
	// inventory UI. Lets guns be equipped without the menu (editor MCP / PIE checks).
	UFUNCTION(Exec)
	void EquipItem(int32 Index);

	// Debug (non-shipping): adds gold to this player's inventory (server).
	UFUNCTION(Exec)
	void GiveGold(int32 Amount);

	UFUNCTION(Server, Reliable)
	void Server_GiveGold(int32 Amount);

	// Debug (non-shipping): adds Upgrade Shards (same as GiveItem UpgradeShard).
	UFUNCTION(Exec)
	void GiveShards(int32 Qty = 1);

	UFUNCTION(Server, Reliable)
	void Server_GiveShards(int32 Qty);

	// Debug (non-shipping): sets the durability of the item in an equipment slot (255 = all equipped), clamped to its max.
	UFUNCTION(Exec)
	void SetDurability(int32 Slot, int32 Value);

	UFUNCTION(Server, Reliable)
	void Server_SetDurability(uint8 Slot, int32 Value);

	// Debug (non-shipping): sets a slot's upgrade level, clamped to 0..MaxUpgradeLevel.
	UFUNCTION(Exec)
	void SetUpgradeLevel(int32 Slot, int32 Level);

	UFUNCTION(Server, Reliable)
	void Server_SetUpgradeLevel(uint8 Slot, int32 Level);

	// Debug (non-shipping): logs THIS machine's view of every equipped slot: durability, upgrade level, effective DR / AP.
	UFUNCTION(Exec)
	void DumpCondition();

	// ---- Perks (U3). Debug, non-shipping: the Server_ RPCs mutate on the server; bodies are compiled out in shipping. ----

	// Adds N (may be negative) bonus perk points to this player.
	UFUNCTION(Exec)
	void GivePerkPoints(int32 N = 1);

	UFUNCTION(Server, Reliable)
	void Server_GivePerkPoints(int32 N);

	// Sets the run's completed zones (0..4); points are derived from it.
	UFUNCTION(Exec)
	void SetZonesCompleted(int32 Zones);

	UFUNCTION(Server, Reliable)
	void Server_SetZonesCompleted(int32 Zones);

	// Sets the run's Souls Saved; points are derived from it (milestones 5 / 10 / 15).
	UFUNCTION(Exec)
	void SetSoulsSaved(int32 Souls);

	UFUNCTION(Server, Reliable)
	void Server_SetSoulsSaved(int32 Souls);

	// Sets a node's rank (Node 0..15, clamped to its max rank), bypassing every rule. Re-applies the perk effect.
	UFUNCTION(Exec)
	void SetPerkRank(int32 Node, int32 Rank);

	UFUNCTION(Server, Reliable)
	void Server_SetPerkRank(int32 Node, int32 Rank);

	// Sets every rank to 0 (no refund rules, no station needed).
	UFUNCTION(Exec)
	void ResetPerks();

	UFUNCTION(Server, Reliable)
	void Server_ResetPerks();

	// Logs THIS machine's view: ranks, points earned / spent / available, path points and the resulting stats.
	UFUNCTION(Exec)
	void DumpPerks();

	// Debug (non-shipping): logs THIS machine's view of the local player's inventory (LogMDInventory, Warning).
	UFUNCTION(Exec)
	void DumpInventory();

	// Debug (non-shipping): same, for every pawn with an inventory component in the local world.
	UFUNCTION(Exec)
	void DumpAllInventories();

	// Debug (non-shipping): lists DT_Items row names with their equip slot, for use with GiveItem.
	UFUNCTION(Exec)
	void ListItemRows();

	// ---- Quests and minimap (U4). Debug execs, non-shipping unless noted: bodies are compiled out in shipping. ----

	// Completes Zone's quest (1..4) on the server whatever its state; ZonesCompleted and perk points follow.
	UFUNCTION(Exec)
	void CompleteQuest(int32 Zone);

	UFUNCTION(Server, Reliable)
	void Server_CompleteQuest(int32 Zone);

	// Adds Delta progress to the Active quest, ignoring its objective type and tag.
	UFUNCTION(Exec)
	void AddQuestProgress(int32 Delta = 1);

	UFUNCTION(Server, Reliable)
	void Server_AddQuestProgress(int32 Delta);

	// Zone 1 Active, the rest Locked, progress and ZonesCompleted back to 0, objective markers usable again.
	UFUNCTION(Exec)
	void ResetQuests();

	UFUNCTION(Server, Reliable)
	void Server_ResetQuests();

	// Logs THIS machine's view of the four quests.
	UFUNCTION(Exec)
	void DumpQuests();

	// Reveals the whole shared level fog for the party (server).
	UFUNCTION(Exec)
	void RevealMap();

	UFUNCTION(Server, Reliable)
	void Server_RevealMap();

	// Hides the whole shared level fog again (server).
	UFUNCTION(Exec)
	void ResetFog();

	UFUNCTION(Server, Reliable)
	void Server_ResetFog();

	// Local: re-renders the whole-level map (AMDLevelMap) and logs the result.
	UFUNCTION(Exec)
	void CaptureMap();

	// Local: logs the level map (bounds, floor height, plan classes), minimap texture, zone, fog grid and minimap option.
	UFUNCTION(Exec)
	void DumpMap();

	// Local (also in shipping): flips the minimap between rotating with the camera (default) and north-up. Saved per user.
	UFUNCTION(Exec)
	void ToggleMinimapRotation();

protected:
	// Server: bound to the ASC's OnDeath; starts the respawn timer.
	UFUNCTION()
	void HandleOwnerDeath();

	// Owning client: undo anything the death left behind (disabled input, ignore flags).
	UFUNCTION(Client, Reliable)
	void Client_OnRespawned();

	UFUNCTION(Client, Reliable)
	void Client_NotifyPickup(FName ItemRowName, int32 Quantity);

	UFUNCTION(Client, Reliable)
	void Client_NotifyUseRejected(EMDUseRejectReason Reason, float CooldownRemaining);

	UFUNCTION(Client, Unreliable)
	void Client_NotifyCrit(float FinalDamage);

	UFUNCTION(Client, Reliable)
	void Client_NotifyToast(const FText& Message, EMDColor Tone);

	UFUNCTION(Client, Reliable)
	void Client_OpenStation(AMDStation* Station);

	// Owning client: drops leftover sprint state after a respawn (MaxWalkSpeed back to default + OnRespawnResetMovement).
	UFUNCTION(Client, Reliable)
	void Client_ResetMovementState();

private:
	// R0: Q / D-pad Up swaps melee and ranged mode. The server validates and flips UMDRangedComponent.
	void RangedSwapPressed();
	// R3: gamepad D-pad Down.
	void RangedReloadPressed();
	// R3: true when R was consumed as a reload (ranged mode, no menu open).
	bool TryReloadFromKey();
	// R1: LMB / right trigger -> UMDRangedComponent::SetFireHeld (ignored while a menu is open).
	void RangedFirePressed();
	void RangedFireReleased();

	UFUNCTION(Server, Reliable)
	void Server_RequestRangedSwap();

	void OnQuickSlot1(const FInputActionValue& Value);
	void OnQuickSlot2(const FInputActionValue& Value);
	void OnPause(const FInputActionValue& Value);
	void OnToggleMenu(const FInputActionValue& Value);
	void OnOpenPerks(const FInputActionValue& Value);
	void OnOpenRun(const FInputActionValue& Value);
	void OpenPerksFallback();
	void OpenRunFallback();
	void ApplyMovementReset();
	void UpdateInteractFocus();
	FTimerHandle FocusTraceTimer;

	void OpenStationLocal(AMDStation* Station);
	void WatchStation();
	TWeakObjectPtr<AMDStation> OpenStation;

	bool bStationOpen = false;
	FTimerHandle StationWatchTimer;

	TWeakObjectPtr<AActor> InteractFocus;
	FText InteractPrompt;
	bool bUsingGamepad = false;
	bool bPauseMenuOpen = false;
	bool bGameMenuOpen = false;
	EMDMenuTab LastMenuTab = EMDMenuTab::Inventory;

	UPROPERTY(Transient)
	TObjectPtr<UMDGameMenuWidget> GameMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> PauseWidget;

	bool FindRespawnTransform(FTransform& OutTransform) const;

	FTimerHandle RespawnTimer;
};
