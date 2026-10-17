#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MDPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDPickupNotified, FName, ItemRowName, int32, Quantity);

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
	// ---- Quick slots / pause input (assets set in BP_MDPlayerController defaults) ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> QuickSlot1Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> QuickSlot2Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Input")
	TObjectPtr<UInputAction> PauseAction;

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

	// Debug (non-shipping): logs THIS machine's view of the local player's inventory (LogMDInventory, Warning).
	UFUNCTION(Exec)
	void DumpInventory();

	// Debug (non-shipping): same, for every pawn with an inventory component in the local world.
	UFUNCTION(Exec)
	void DumpAllInventories();

	// Debug (non-shipping): lists DT_Items row names with their equip slot, for use with GiveItem.
	UFUNCTION(Exec)
	void ListItemRows();

protected:
	// Server: bound to the ASC's OnDeath; starts the respawn timer.
	UFUNCTION()
	void HandleOwnerDeath();

	// Owning client: undo anything the death left behind (disabled input, ignore flags).
	UFUNCTION(Client, Reliable)
	void Client_OnRespawned();

	UFUNCTION(Client, Reliable)
	void Client_NotifyPickup(FName ItemRowName, int32 Quantity);

	// Owning client: drops leftover sprint state after a respawn (MaxWalkSpeed back to default + OnRespawnResetMovement).
	UFUNCTION(Client, Reliable)
	void Client_ResetMovementState();

private:
	void OnQuickSlot1(const FInputActionValue& Value);
	void OnQuickSlot2(const FInputActionValue& Value);
	void OnPause(const FInputActionValue& Value);
	void ApplyMovementReset();
	void UpdateInteractFocus();
	FTimerHandle FocusTraceTimer;

	TWeakObjectPtr<AActor> InteractFocus;
	FText InteractPrompt;
	bool bUsingGamepad = false;
	bool bPauseMenuOpen = false;

	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> PauseWidget;

	bool FindRespawnTransform(FTransform& OutTransform) const;

	FTimerHandle RespawnTimer;
};
