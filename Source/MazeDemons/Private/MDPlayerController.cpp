#include "MDPlayerController.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameMenuWidget.h"
#include "MDGameplayStatics.h"
#include "MDFogComponent.h"
#include "MDGameState.h"
#include "MDMapComponent.h"
#include "MDMapZone.h"
#include "MDQuestComponent.h"
#include "MDQuestSetData.h"
#include "MDPlayerState.h"
#include "MDInventoryComponent.h"
#include "MDPerkCatalog.h"
#include "MDPerkComponent.h"
#include "MDPickupBase.h"
#include "MDInteractAbility.h"
#include "MDGameSettings.h"
#include "MDStation.h"
#include "MDStationAmmoPanel.h"
#include "MDAttributeSet.h"
#include "MDStaminaComponent.h"
#include "Ranged/MDRangedComponent.h"
#include "MDStatCalculator.h"
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
#include "EnhancedInputComponent.h"
#include "InputCoreTypes.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"

AMDPlayerController::AMDPlayerController()
{
	MapComponent = CreateDefaultSubobject<UMDMapComponent>(TEXT("MapComponent"));
}

void AMDPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		GetWorldTimerManager().SetTimer(FocusTraceTimer, this, &AMDPlayerController::UpdateInteractFocus,
			FMath::Max(0.02f, FocusTraceInterval), true);
	}

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

void AMDPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FocusTraceTimer);
	GetWorldTimerManager().ClearTimer(StationWatchTimer);
	Super::EndPlay(EndPlayReason);
}

FText AMDPlayerController::BuildInteractPrompt(const FText& ItemName)
{
	if (ItemName.IsEmpty())
	{
		return NSLOCTEXT("MDInteract", "InteractPrompt", "Interact");
	}
	return FText::Format(NSLOCTEXT("MDInteract", "PickUpPrompt", "Pick up {0}"), ItemName);
}

bool AMDPlayerController::WouldFocusChange(const AActor* OldFocus, const FText& OldPrompt, const AActor* NewFocus, const FText& NewPrompt)
{
	if (!NewFocus)
	{
		return OldFocus != nullptr || !OldPrompt.IsEmpty();
	}
	return OldFocus != NewFocus || !OldPrompt.EqualTo(NewPrompt);
}

void AMDPlayerController::UpdateInteractFocus()
{
	AActor* Target = nullptr;
	FText Prompt;

	const APawn* LocalPawn = GetPawn();
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	const bool bAlive = LocalPawn && !(ASC && ASC->IsDead());

	if (!bAlive && bGameMenuOpen)
	{
		CloseGameMenu(); // dead or between pawns: never leave the menu (and the cursor) up
	}

	if (bAlive && !bPauseMenuOpen && !bGameMenuOpen)
	{
		float Range = 250.0f;
		float Radius = 80.0f;
		FName Tag = NAME_None;
		if (InteractAbilityClass)
		{
			const UMDInteractAbility* Defaults = GetDefault<UMDInteractAbility>(InteractAbilityClass);
			Range = Defaults->InteractRange;
			Radius = Defaults->InteractRadius;
			Tag = Defaults->RequiredActorTag;
		}

		AActor* Candidate = UMDInteractAbility::FindInteractTargetFor(LocalPawn, Range, Radius, Tag);
		if (Candidate)
		{
			const AMDPickupBase* Pickup = Cast<AMDPickupBase>(Candidate);
			if (const AMDStation* Station = Cast<AMDStation>(Candidate))
			{
				Target = Candidate;
				Prompt = FText::Format(NSLOCTEXT("MDInteract", "UseStationPrompt", "Use {0}"), Station->StationName);
			}
			else if (!Pickup)
			{
				Target = Candidate;
				Prompt = BuildInteractPrompt(FText::GetEmpty());
			}
			else if (Pickup->CanInteract())
			{
				Target = Candidate;
				Prompt = BuildInteractPrompt(Pickup->ItemRowName.IsNone() ? FText::GetEmpty() : UMDGameplayStatics::GetItemDisplayName(Pickup->ItemRowName));
			}
		}
	}

	if (WouldFocusChange(InteractFocus.Get(), InteractPrompt, Target, Prompt))
	{
		SetInteractFocus(Target, Prompt);
	}
}

void AMDPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (QuickSlot1Action)
		{
			EIC->BindAction(QuickSlot1Action, ETriggerEvent::Started, this, &AMDPlayerController::OnQuickSlot1);
		}
		if (QuickSlot2Action)
		{
			EIC->BindAction(QuickSlot2Action, ETriggerEvent::Started, this, &AMDPlayerController::OnQuickSlot2);
		}
		if (PauseAction)
		{
			EIC->BindAction(PauseAction, ETriggerEvent::Started, this, &AMDPlayerController::OnPause);
		}
		if (ToggleMenuAction)
		{
			EIC->BindAction(ToggleMenuAction, ETriggerEvent::Started, this, &AMDPlayerController::OnToggleMenu);
		}
		if (OpenPerksAction)
		{
			EIC->BindAction(OpenPerksAction, ETriggerEvent::Started, this, &AMDPlayerController::OnOpenPerks);
		}
		if (OpenRunAction)
		{
			EIC->BindAction(OpenRunAction, ETriggerEvent::Started, this, &AMDPlayerController::OnOpenRun);
		}
	}

	// Fallback keys for any menu action the Blueprint defaults do not set (legacy key bindings still run beside Enhanced Input).
	if (InputComponent)
	{
		if (!ToggleMenuAction)
		{
			InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AMDPlayerController::ToggleGameMenu);
			InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AMDPlayerController::ToggleGameMenu);
		}
		if (!OpenPerksAction)
		{
			InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AMDPlayerController::OpenPerksFallback);
		}
		if (!OpenRunAction)
		{
			InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AMDPlayerController::OpenRunFallback);
		}
		// R0 mode swap (no asset needed): Q / D-pad Up.
		InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMDPlayerController::RangedSwapPressed);
		InputComponent->BindKey(EKeys::Gamepad_DPad_Up, IE_Pressed, this, &AMDPlayerController::RangedSwapPressed);
		// R3 reload: D-pad Down on a gamepad (R is shared with the Run tab: in ranged mode R reloads, see TryReloadFromKey).
		InputComponent->BindKey(EKeys::Gamepad_DPad_Down, IE_Pressed, this, &AMDPlayerController::RangedReloadPressed);
		// R1 fire: LMB / right trigger. Not consumed, so melee's own LMB binding keeps working outside ranged mode.
		for (const FKey& FireKey : { EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger })
		{
			InputComponent->BindKey(FireKey, IE_Pressed, this, &AMDPlayerController::RangedFirePressed).bConsumeInput = false;
			InputComponent->BindKey(FireKey, IE_Released, this, &AMDPlayerController::RangedFireReleased).bConsumeInput = false;
		}
	}
}

void AMDPlayerController::OnToggleMenu(const FInputActionValue& Value)
{
	ToggleGameMenu();
}

void AMDPlayerController::OnOpenPerks(const FInputActionValue& Value)
{
	OpenGameMenuTab(EMDMenuTab::Perks);
}

void AMDPlayerController::OnOpenRun(const FInputActionValue& Value)
{
	if (TryReloadFromKey())
	{
		return;
	}
	OpenGameMenuTab(EMDMenuTab::Run);
}

void AMDPlayerController::OpenPerksFallback()
{
	OpenGameMenuTab(EMDMenuTab::Perks);
}

void AMDPlayerController::OpenRunFallback()
{
	if (TryReloadFromKey())
	{
		return;
	}
	OpenGameMenuTab(EMDMenuTab::Run);
}

bool AMDPlayerController::TryReloadFromKey()
{
	// R3: in ranged mode (with no menu open) R is the reload key; the Run tab stays on the game menu (Tab).
	UMDRangedComponent* Ranged = UMDRangedComponent::Find(this);
	if (!Ranged || !Ranged->IsRangedMode() || IsAnyMenuOpen())
	{
		return false;
	}
	Ranged->TryActivateReload();
	return true;
}

void AMDPlayerController::RangedReloadPressed()
{
	if (IsAnyMenuOpen())
	{
		return;
	}
	if (UMDRangedComponent* Ranged = UMDRangedComponent::Find(this))
	{
		Ranged->TryActivateReload();
	}
}

void AMDPlayerController::ToggleGameMenu()
{
	if (bGameMenuOpen)
	{
		CloseGameMenu();
	}
	else
	{
		OpenGameMenuTab(EMDMenuTab::None);
	}
}

void AMDPlayerController::OpenGameMenuTab(EMDMenuTab Tab)
{
	if (!IsLocalController())
	{
		return;
	}
	if (Tab == EMDMenuTab::None)
	{
		Tab = LastMenuTab;
	}
	if (!UMDGameMenuWidget::IsTabSelectable(Tab))
	{
		Tab = EMDMenuTab::Inventory;
	}

	if (bGameMenuOpen && GameMenuWidget)
	{
		if (GameMenuWidget->ActiveTab == Tab)
		{
			CloseGameMenu(); // pressing the key of the tab you are on closes
		}
		else
		{
			GameMenuWidget->SetActiveTab(Tab);
			GameMenuWidget->SyncActiveTab();
			LastMenuTab = GameMenuWidget->ActiveTab;
		}
		return;
	}

	// Never over the pause menu, a station panel or any other menu that shows the cursor (legacy W_Inventory), nor while dead.
	if (bPauseMenuOpen || bStationOpen || bShowMouseCursor)
	{
		return;
	}
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (!GetPawn() || (ASC && ASC->IsDead()))
	{
		return;
	}

	if (!GameMenuWidget)
	{
		if (!GameMenuClass)
		{
			UE_LOG(LogMDInventory, Warning, TEXT("OpenGameMenuTab: GameMenuClass is not set on %s (set W_MDGameMenu in BP_MDPlayerController defaults)"), *GetNameSafe(this));
			return;
		}
		GameMenuWidget = CreateWidget<UMDGameMenuWidget>(this, GameMenuClass);
	}
	if (!GameMenuWidget)
	{
		return;
	}

	GameMenuWidget->AddToViewport(90); // below the pause menu (100)
	GameMenuWidget->SetActiveTab(Tab);
	GameMenuWidget->SyncActiveTab(); // always resync tab bar + switcher on open, even if the tab is unchanged
	GameMenuWidget->PlayOpen();
	LastMenuTab = GameMenuWidget->ActiveTab;
	bGameMenuOpen = true;

	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(GameMenuWidget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true); // the project's "a menu is open" rule: BP_Player and quick slots block gameplay input on it
}

void AMDPlayerController::CloseGameMenu()
{
	if (!bGameMenuOpen)
	{
		return;
	}
	bGameMenuOpen = false;
	if (GameMenuWidget)
	{
		LastMenuTab = GameMenuWidget->ActiveTab;
		GameMenuWidget->RemoveFromParent();
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

void AMDPlayerController::OpenMenu()
{
	ToggleGameMenu();
}

void AMDPlayerController::OpenMenuTab(int32 TabNumber)
{
	if (TabNumber < 1 || TabNumber > UMDGameMenuWidget::NumTabs)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("OpenMenuTab: %d is not a tab (1 Inventory, 2 Perks, 3 Run)"), TabNumber);
		return;
	}
	OpenGameMenuTab(static_cast<EMDMenuTab>(TabNumber));
}

void AMDPlayerController::RangedSwapPressed()
{
	if (IsAnyMenuOpen())
	{
		return;
	}
	Server_RequestRangedSwap();
}

void AMDPlayerController::RangedFirePressed()
{
	if (IsAnyMenuOpen())
	{
		return;
	}
	if (UMDRangedComponent* Ranged = UMDRangedComponent::Find(this))
	{
		Ranged->SetFireHeld(true);
	}
}

void AMDPlayerController::RangedFireReleased()
{
	if (UMDRangedComponent* Ranged = UMDRangedComponent::Find(this))
	{
		Ranged->SetFireHeld(false);
	}
}

void AMDPlayerController::Server_RequestRangedSwap_Implementation()
{
	if (UMDRangedComponent* Ranged = UMDRangedComponent::Find(this))
	{
		Ranged->TrySwapMode();
	}
}

void AMDPlayerController::OnQuickSlot1(const FInputActionValue& Value)
{
	if (IsAnyMenuOpen())
	{
		return; // a menu is open (matches BP_Player's IsMenuOpen gate)
	}
	UseQuickSlot(1);
}

void AMDPlayerController::OnQuickSlot2(const FInputActionValue& Value)
{
	if (IsAnyMenuOpen())
	{
		return; // a menu is open (matches BP_Player's IsMenuOpen gate)
	}
	UseQuickSlot(2);
}

void AMDPlayerController::OnPause(const FInputActionValue& Value)
{
	TogglePauseMenu(); // closes the game menu first when it is open
}

void AMDPlayerController::UseQuickSlot(int32 Slot)
{
	UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetPawn());
	if (!Inv)
	{
		Inv = UMDGameplayStatics::GetInventory(this);
	}
	if (Inv)
	{
		Inv->RequestUseQuickSlot(Slot - 1);
	}
}

void AMDPlayerController::AssignQuickSlot(int32 Slot, int32 InvIndex)
{
	UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetPawn());
	if (!Inv)
	{
		Inv = UMDGameplayStatics::GetInventory(this);
	}
	if (Inv)
	{
		Inv->RequestAssignQuickSlot(Slot - 1, InvIndex);
	}
}

void AMDPlayerController::SetInteractFocus(AActor* NewFocus, FText Prompt)
{
	if (!IsLocalController())
	{
		return;
	}
	if (!NewFocus)
	{
		Prompt = FText::GetEmpty();
	}
	if (InteractFocus.Get() == NewFocus && InteractPrompt.EqualTo(Prompt))
	{
		return;
	}
	InteractFocus = NewFocus;
	InteractPrompt = Prompt;
	OnInteractFocusChanged.Broadcast(NewFocus, Prompt);
}

bool AMDPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (IsLocalController() && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
	{
		const bool bGamepad = Params.Key.IsGamepadKey();
		const bool bKeyboardMouse = !bGamepad && !Params.Key.IsTouch();
		if ((bGamepad && !bUsingGamepad) || (bKeyboardMouse && bUsingGamepad))
		{
			bUsingGamepad = bGamepad;
			OnInputDeviceChanged.Broadcast(bUsingGamepad);
		}
	}
	return Super::InputKey(Params);
}

void AMDPlayerController::TogglePauseMenu()
{
	if (!IsLocalController())
	{
		return;
	}
	if (bGameMenuOpen)
	{
		CloseGameMenu(); // Esc closes the game menu; the pause menu stays separate and is not opened over it
		return;
	}
	if (bPauseMenuOpen)
	{
		if (PauseWidget)
		{
			PauseWidget->RemoveFromParent();
		}
		bPauseMenuOpen = false;
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		return;
	}
	if (!PauseWidget)
	{
		const UMDUISettings* Settings = GetDefault<UMDUISettings>();
		if (const TSubclassOf<UUserWidget> PauseClass = Settings ? Settings->PauseWidgetClass.LoadSynchronous() : nullptr)
		{
			PauseWidget = CreateWidget<UUserWidget>(this, PauseClass);
		}
	}
	if (!PauseWidget)
	{
		return; // no pause widget configured yet; do nothing (never pauses the game)
	}
	PauseWidget->AddToViewport(100);
	bPauseMenuOpen = true;
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(PauseWidget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

void AMDPlayerController::Client_ResetMovementState_Implementation()
{
	ApplyMovementReset();
}

void AMDPlayerController::ApplyMovementReset()
{
	ACharacter* Char = Cast<ACharacter>(GetPawn());
	if (!Char)
	{
		// The new pawn may not have replicated yet; try once more next frame.
		TWeakObjectPtr<AMDPlayerController> WeakThis(this);
		GetWorldTimerManager().SetTimerForNextTick([WeakThis]()
		{
			if (WeakThis.IsValid() && Cast<ACharacter>(WeakThis->GetPawn()))
			{
				WeakThis->ApplyMovementReset();
			}
		});
		return;
	}
	if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
	{
		if (const ACharacter* CDO = Char->GetClass()->GetDefaultObject<ACharacter>())
		{
			if (const UCharacterMovementComponent* DefaultMove = CDO->GetCharacterMovement())
			{
				Move->MaxWalkSpeed = DefaultMove->MaxWalkSpeed;
			}
		}
	}
	OnRespawnResetMovement(Char);
}

void AMDPlayerController::UseItem(int32 Index)
{
	if (UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetPawn()))
	{
		Inv->RequestUseItem(Index);
	}
	else if (UMDInventoryComponent* ControllerInv = UMDGameplayStatics::GetInventory(this))
	{
		ControllerInv->RequestUseItem(Index);
	}
}

void AMDPlayerController::EquipItem(int32 Index)
{
	if (UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetPawn()))
	{
		Inv->RequestEquip(Index);
	}
	else if (UMDInventoryComponent* ControllerInv = UMDGameplayStatics::GetInventory(this))
	{
		ControllerInv->RequestEquip(Index);
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
		UE_LOG(LogMDInventory, Warning, TEXT("  Gold: %d  Shards: %d"), Inv->GetGold(), Inv->GetShardCount());
		const TArray<FMDItemStack>& Items = Inv->GetItems();
		UE_LOG(LogMDInventory, Warning, TEXT("  Items (%d):"), Items.Num());
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			int32 Cur = 0;
			int32 Max = 0;
			Inv->GetItemDurability(i, Cur, Max);
			UE_LOG(LogMDInventory, Warning, TEXT("    %d %s x%d%s"), i, *Items[i].ItemRowName.ToString(), Items[i].Quantity,
				Max > 0 ? *FString::Printf(TEXT("  dur %d/%d"), Cur, Max) : TEXT(""));
		}
		const TArray<FMDEquippedSlot>& Equipped = Inv->GetEquipped();
		UE_LOG(LogMDInventory, Warning, TEXT("  Equipped (%d):"), Equipped.Num());
		for (const FMDEquippedSlot& E : Equipped)
		{
			int32 Cur = 0;
			int32 Max = 0;
			Inv->GetSlotDurability(E.Slot, Cur, Max);
			UE_LOG(LogMDInventory, Warning, TEXT("    slot %d %s%s  upgrade +%d"), E.Slot, *E.ItemRowName.ToString(),
				Max > 0 ? *FString::Printf(TEXT("  dur %d/%d"), Cur, Max) : TEXT(""), Inv->GetSlotUpgradeLevel(E.Slot));
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

void AMDPlayerController::NotifyUseRejected(EMDUseRejectReason Reason, float CooldownRemaining)
{
	if (!HasAuthority() || Reason == EMDUseRejectReason::None)
	{
		return;
	}
	if (IsLocalController())
	{
		OnUseRejected.Broadcast(Reason, CooldownRemaining); // listen-server host: no RPC hop
	}
	else
	{
		Client_NotifyUseRejected(Reason, CooldownRemaining);
	}
}

void AMDPlayerController::Client_NotifyUseRejected_Implementation(EMDUseRejectReason Reason, float CooldownRemaining)
{
	OnUseRejected.Broadcast(Reason, CooldownRemaining);
}

void AMDPlayerController::NotifyToast(const FText& Message, EMDColor Tone)
{
	if (!HasAuthority() || Message.IsEmpty())
	{
		return;
	}
	if (IsLocalController())
	{
		OnToastNotified.Broadcast(Message, Tone); // listen-server host: no RPC hop
	}
	else
	{
		Client_NotifyToast(Message, Tone);
	}
}

void AMDPlayerController::NotifyCrit(float FinalDamage)
{
	if (!HasAuthority())
	{
		return;
	}
	if (IsLocalController())
	{
		OnCritNotified(FinalDamage); // listen-server host: no RPC hop
	}
	else
	{
		Client_NotifyCrit(FinalDamage);
	}
}

void AMDPlayerController::Client_NotifyCrit_Implementation(float FinalDamage)
{
	OnCritNotified(FinalDamage);
}

void AMDPlayerController::Client_NotifyToast_Implementation(const FText& Message, EMDColor Tone)
{
	OnToastNotified.Broadcast(Message, Tone);
}

AMDStation* AMDPlayerController::GetOpenStation() const
{
	return OpenStation.Get();
}

void AMDPlayerController::NotifyStationOpened(AMDStation* Station)
{
	if (!HasAuthority() || !Station)
	{
		return;
	}
	if (IsLocalController())
	{
		OpenStationLocal(Station);
	}
	else
	{
		Client_OpenStation(Station);
	}
}

void AMDPlayerController::Client_OpenStation_Implementation(AMDStation* Station)
{
	OpenStationLocal(Station);
}

void AMDPlayerController::OpenStationLocal(AMDStation* Station)
{
	if (!Station)
	{
		return;
	}
	CloseGameMenu(); // the station panel and the game menu never show together
	OpenStation = Station;
	bStationOpen = true;
	GetWorldTimerManager().SetTimer(StationWatchTimer, this, &AMDPlayerController::WatchStation, 0.25f, true);
	// R3: stations with the Ammo service get the ammo section beside the panel.
	if (IsLocalController() && Station->Supports(EMDStationService::Ammo) && !StationAmmoPanel)
	{
		StationAmmoPanel = CreateWidget<UMDStationAmmoPanel>(this);
		if (StationAmmoPanel)
		{
			StationAmmoPanel->SetStation(Station);
			StationAmmoPanel->AddToViewport(60);
		}
	}
	OnStationOpened.Broadcast(Station);
}

void AMDPlayerController::CloseStation()
{
	GetWorldTimerManager().ClearTimer(StationWatchTimer);
	if (StationAmmoPanel)
	{
		StationAmmoPanel->RemoveFromParent();
		StationAmmoPanel = nullptr;
	}
	OpenStation.Reset();
	if (bStationOpen)
	{
		bStationOpen = false;
		OnStationClosed.Broadcast();
	}
}

void AMDPlayerController::WatchStation()
{
	const AMDStation* Station = OpenStation.Get();
	const APawn* LocalPawn = GetPawn();
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	const bool bDead = !LocalPawn || (ASC && ASC->IsDead());
	// Close with some slack beyond the server's range so walking along the edge does not flicker the panel.
	const float CloseRange = UMDGameSettings::GetEconomy().StationRange + 50.0f;
	if (!Station || bDead || !AMDStation::IsWithinRange(LocalPawn->GetActorLocation(), Station->GetActorLocation(), CloseRange))
	{
		CloseStation();
	}
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
	Client_ResetMovementState();
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
		// Goes straight to 0 Health: a lethal hit through ApplyDamage would be stopped by Bulwark or Second Wind.
		ASC->DebugKill(GetPawn());
	}
#endif
}

void AMDPlayerController::DamageSelf(float Amount)
{
#if !UE_BUILD_SHIPPING
	Server_DamageSelf(Amount);
#endif
}

void AMDPlayerController::Server_DamageSelf_Implementation(float Amount)
{
#if !UE_BUILD_SHIPPING
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (ASC && !ASC->IsDead() && Amount > 0.0f)
	{
		ASC->ApplyDamage(FMath::Min(Amount, 10000.0f), GetPawn(), GetPawn(), false); // self-damage never crits
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

void AMDPlayerController::GiveGold(int32 Amount)
{
#if !UE_BUILD_SHIPPING
	Server_GiveGold(Amount);
#endif
}

void AMDPlayerController::Server_GiveGold_Implementation(int32 Amount)
{
#if !UE_BUILD_SHIPPING
	if (UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this))
	{
		Inv->AddGold(FMath::Clamp(Amount, -1000000, 1000000));
		UE_LOG(LogMDInventory, Warning, TEXT("GiveGold %d -> %d gold"), Amount, Inv->GetGold());
	}
#endif
}

void AMDPlayerController::GiveShards(int32 Qty)
{
#if !UE_BUILD_SHIPPING
	Server_GiveShards(Qty);
#endif
}

void AMDPlayerController::Server_GiveShards_Implementation(int32 Qty)
{
#if !UE_BUILD_SHIPPING
	UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this);
	if (!Inv)
	{
		return;
	}
	const FName ShardRow = UMDGameSettings::GetEconomy().ShardRow;
	const bool bOk = Inv->AddItem(ShardRow, Qty);
	UE_LOG(LogMDInventory, Warning, TEXT("GiveShards %s x%d -> %s"), *ShardRow.ToString(), Qty, bOk ? TEXT("added") : TEXT("rejected (no such DT_Items row yet, qty or inventory full)"));
#endif
}

void AMDPlayerController::SetDurability(int32 Slot, int32 Value)
{
#if !UE_BUILD_SHIPPING
	Server_SetDurability(static_cast<uint8>(FMath::Clamp(Slot, 0, 255)), Value);
#endif
}

void AMDPlayerController::Server_SetDurability_Implementation(uint8 Slot, int32 Value)
{
#if !UE_BUILD_SHIPPING
	if (UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this))
	{
		Inv->DebugSetDurability(Slot, Value);
	}
#endif
}

void AMDPlayerController::SetUpgradeLevel(int32 Slot, int32 Level)
{
#if !UE_BUILD_SHIPPING
	Server_SetUpgradeLevel(static_cast<uint8>(FMath::Clamp(Slot, 0, 255)), Level);
#endif
}

void AMDPlayerController::Server_SetUpgradeLevel_Implementation(uint8 Slot, int32 Level)
{
#if !UE_BUILD_SHIPPING
	if (UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this))
	{
		Inv->DebugSetUpgradeLevel(Slot, Level);
	}
#endif
}

void AMDPlayerController::DumpCondition()
{
#if !UE_BUILD_SHIPPING
	const UMDInventoryComponent* Inv = FindInventoryFor(GetPawn(), this);
	if (!Inv)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("DumpCondition: no UMDInventoryComponent for local player %s"), *GetNameSafe(this));
		return;
	}
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	const FMDEconomyTuning& Tuning = UMDGameSettings::GetEconomy();
	UE_LOG(LogMDInventory, Warning, TEXT("==== Condition [%s] Gold %d  Shards %d  Defense %.2f%% (cap %.0f)  AttackPower %.2f ===="),
		NetModeName(GetNetMode()), Inv->GetGold(), Inv->GetShardCount(),
		ASC ? ASC->GetDefense() : 0.0f, ASC ? ASC->DefenseCap : 0.0f, ASC ? ASC->GetAttackPower() : 0.0f);
	for (const FMDEquippedSlot& E : Inv->GetEquipped())
	{
		int32 Cur = 0;
		int32 Max = 0;
		Inv->GetSlotDurability(E.Slot, Cur, Max);
		const int32 Level = Inv->GetSlotUpgradeLevel(E.Slot);
		const float Mult = Inv->GetSlotConditionMultiplier(E.Slot);
		UE_LOG(LogMDInventory, Warning, TEXT("  slot %d %s  dur %d/%d (x%.2f)  +%d  rowDR %.1f -> effDR %.2f  rowAP %.1f"),
			E.Slot, *E.ItemRowName.ToString(), Cur, Max, Mult, Level, Inv->GetItemArmorReduction(E.ItemRowName),
			UMDStatCalculator::EffectivePieceDR(Inv->GetItemArmorReduction(E.ItemRowName), Level, Mult, Tuning),
			Inv->GetItemAttackBonus(E.ItemRowName));
	}
	if (const UMDPerkComponent* Perk = UMDPerkComponent::FindFor(PS))
	{
		const FMDPerkModifiers Mods = Perk->ComputeCurrentModifiers();
		UE_LOG(LogMDInventory, Warning, TEXT("  perks: spent %d/%d  DR +%.1f pp (Stalwart +%.1f)  flat -%.1f  AP +%.0f%%  crit %.0f%% x%.2f  exec +%.0f%%  regen %.1f/s  potion x%.2f  bulwark %d  secondWind %d (used %d)  stamCost x%.2f  sprint x%.2f"),
			Perk->GetPointsSpent(), Perk->GetPointsEarned(), Mods.DamageReductionPP, Mods.StalwartPP, Mods.FlatDamageReduction,
			Mods.AttackPowerPct * 100.0f, Mods.CritChance * 100.0f, Mods.CritDamage, Mods.ExecutionerPct * 100.0f, Mods.HealthRegen,
			1.0f + Mods.PotionHealPct, Mods.bBulwark ? 1 : 0, Mods.bSecondWind ? 1 : 0, Perk->bSecondWindUsed ? 1 : 0,
			Mods.StaminaCostScale, Mods.SprintSpeedScale);
	}
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

// ---- Perk debug execs ----

namespace
{
#if !UE_BUILD_SHIPPING
	UMDPerkComponent* FindPerkComponentFor(const AMDPlayerController* Controller)
	{
		return UMDPerkComponent::FindFor(Controller);
	}
#endif
}

void AMDPlayerController::GivePerkPoints(int32 N)
{
#if !UE_BUILD_SHIPPING
	Server_GivePerkPoints(N);
#endif
}

void AMDPlayerController::Server_GivePerkPoints_Implementation(int32 N)
{
#if !UE_BUILD_SHIPPING
	if (UMDPerkComponent* Perk = FindPerkComponentFor(this))
	{
		Perk->DebugAddPoints(FMath::Clamp(N, -1000, 1000));
		UE_LOG(LogMDInventory, Warning, TEXT("GivePerkPoints %d -> earned %d, spent %d, available %d"),
			N, Perk->GetPointsEarned(), Perk->GetPointsSpent(), Perk->GetPointsAvailable());
	}
#endif
}

void AMDPlayerController::SetZonesCompleted(int32 Zones)
{
#if !UE_BUILD_SHIPPING
	Server_SetZonesCompleted(Zones);
#endif
}

void AMDPlayerController::Server_SetZonesCompleted_Implementation(int32 Zones)
{
#if !UE_BUILD_SHIPPING
	if (AMDGameState* State = GetWorld() ? GetWorld()->GetGameState<AMDGameState>() : nullptr)
	{
		State->SetZonesCompleted(Zones);
		UE_LOG(LogMDInventory, Warning, TEXT("SetZonesCompleted %d -> zones %d, souls %d"), Zones, State->ZonesCompleted, State->SoulsSaved);
		UE_LOG(LogMDInventory, Warning, TEXT("SetZonesCompleted: the quests were NOT changed and own ZonesCompleted from now on (use CompleteQuest / ResetQuests); the next quest completion overwrites this value"));
	}
	else
	{
		UE_LOG(LogMDInventory, Warning, TEXT("SetZonesCompleted: no AMDGameState in this world (set GameStateClass)"));
	}
#endif
}

void AMDPlayerController::SetSoulsSaved(int32 Souls)
{
#if !UE_BUILD_SHIPPING
	Server_SetSoulsSaved(Souls);
#endif
}

void AMDPlayerController::Server_SetSoulsSaved_Implementation(int32 Souls)
{
#if !UE_BUILD_SHIPPING
	if (AMDGameState* State = GetWorld() ? GetWorld()->GetGameState<AMDGameState>() : nullptr)
	{
		State->SetSoulsSaved(Souls);
		UE_LOG(LogMDInventory, Warning, TEXT("SetSoulsSaved %d -> zones %d, souls %d"), Souls, State->ZonesCompleted, State->SoulsSaved);
	}
	else
	{
		UE_LOG(LogMDInventory, Warning, TEXT("SetSoulsSaved: no AMDGameState in this world (set GameStateClass)"));
	}
#endif
}

// ---- Quests and minimap (U4 debug) ----

void AMDPlayerController::CompleteQuest(int32 Zone)
{
#if !UE_BUILD_SHIPPING
	Server_CompleteQuest(Zone);
#endif
}

void AMDPlayerController::Server_CompleteQuest_Implementation(int32 Zone)
{
#if !UE_BUILD_SHIPPING
	if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
	{
		Quests->ForceCompleteZone(Zone);
		UE_LOG(LogMDInventory, Warning, TEXT("CompleteQuest %d -> zones completed %d"), Zone, UMDQuestRules::CountZonesCompleted(Quests->Quests));
	}
	else
	{
		UE_LOG(LogMDInventory, Warning, TEXT("CompleteQuest: no quest component (set GameStateClass to a MDGameState)"));
	}
#endif
}

void AMDPlayerController::AddQuestProgress(int32 Delta)
{
#if !UE_BUILD_SHIPPING
	Server_AddQuestProgress(Delta);
#endif
}

void AMDPlayerController::Server_AddQuestProgress_Implementation(int32 Delta)
{
#if !UE_BUILD_SHIPPING
	if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
	{
		Quests->DebugAddProgress(FMath::Clamp(Delta, 1, 1000));
		FMDQuestProgress Active;
		if (Quests->GetActiveQuest(Active))
		{
			UE_LOG(LogMDInventory, Warning, TEXT("AddQuestProgress %d -> zone %d at %d/%d"), Delta, Active.ZoneIndex, Active.Count, Active.Required);
		}
		else
		{
			UE_LOG(LogMDInventory, Warning, TEXT("AddQuestProgress %d -> no active quest"), Delta);
		}
	}
#endif
}

void AMDPlayerController::ResetQuests()
{
#if !UE_BUILD_SHIPPING
	Server_ResetQuests();
#endif
}

void AMDPlayerController::Server_ResetQuests_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
	{
		Quests->ResetQuests();
		UE_LOG(LogMDInventory, Warning, TEXT("ResetQuests: zone 1 active, zones completed 0"));
	}
#endif
}

void AMDPlayerController::DumpQuests()
{
#if !UE_BUILD_SHIPPING
	const UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this);
	if (!Quests)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("DumpQuests: no quest component (set GameStateClass to a MDGameState)"));
		return;
	}
	const AMDGameState* State = GetWorld() ? GetWorld()->GetGameState<AMDGameState>() : nullptr;
	UE_LOG(LogMDInventory, Warning, TEXT("DumpQuests: %d quests, zones completed %d, quest set %s"),
		Quests->Quests.Num(), State ? State->ZonesCompleted : 0, *GetNameSafe(Quests->QuestSet));
	for (const FMDQuestProgress& Quest : Quests->Quests)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("  Zone %d: %s %d/%d  title '%s'  objective '%s'"), Quest.ZoneIndex,
			*StaticEnum<EMDQuestState>()->GetNameStringByValue(static_cast<int64>(Quest.State)), Quest.Count, Quest.Required,
			*Quests->GetQuestTitle(Quest.ZoneIndex).ToString(), *Quests->GetObjectiveText(Quest.ZoneIndex).ToString());
	}
#endif
}

void AMDPlayerController::RevealMap()
{
#if !UE_BUILD_SHIPPING
	Server_RevealMap();
#endif
}

void AMDPlayerController::Server_RevealMap_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (UMDFogComponent* Fog = UMDFogComponent::FindFor(this))
	{
		Fog->RevealAll();
		UE_LOG(LogMDInventory, Warning, TEXT("RevealMap: shared level fog revealed"));
	}
#endif
}

void AMDPlayerController::ResetFog()
{
#if !UE_BUILD_SHIPPING
	Server_ResetFog();
#endif
}

void AMDPlayerController::Server_ResetFog_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (UMDFogComponent* Fog = UMDFogComponent::FindFor(this))
	{
		Fog->ResetFog();
		UE_LOG(LogMDInventory, Warning, TEXT("ResetFog: shared level fog cleared"));
	}
#endif
}

void AMDPlayerController::CaptureMap()
{
#if !UE_BUILD_SHIPPING
	if (MapComponent)
	{
		MapComponent->ForceRecaptureLevelMap();
		UE_LOG(LogMDInventory, Warning, TEXT("CaptureMap: %s"), *MapComponent->BuildDump());
	}
#endif
}

void AMDPlayerController::DumpMap()
{
#if !UE_BUILD_SHIPPING
	if (MapComponent)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("DumpMap: %s"), *MapComponent->BuildDump());
	}
#endif
}

void AMDPlayerController::ToggleMinimapRotation()
{
	// Not compiled out in shipping: this is the player's option until an options menu exists.
	if (MapComponent)
	{
		MapComponent->ToggleMinimapRotation();
		UE_LOG(LogMDInventory, Log, TEXT("ToggleMinimapRotation: minimap is now %s"),
			MapComponent->IsMinimapNorthUp() ? TEXT("north-up") : TEXT("rotating with the camera"));
	}
}

void AMDPlayerController::SetPerkRank(int32 Node, int32 Rank)
{
#if !UE_BUILD_SHIPPING
	Server_SetPerkRank(Node, Rank);
#endif
}

void AMDPlayerController::Server_SetPerkRank_Implementation(int32 Node, int32 Rank)
{
#if !UE_BUILD_SHIPPING
	if (UMDPerkComponent* Perk = FindPerkComponentFor(this))
	{
		Perk->DebugSetRank(Node, Rank);
		UE_LOG(LogMDInventory, Warning, TEXT("SetPerkRank node %d -> rank %d (spent %d, earned %d)"), Node, Perk->GetRank(Node), Perk->GetPointsSpent(), Perk->GetPointsEarned());
	}
#endif
}

void AMDPlayerController::ResetPerks()
{
#if !UE_BUILD_SHIPPING
	Server_ResetPerks();
#endif
}

void AMDPlayerController::Server_ResetPerks_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (UMDPerkComponent* Perk = FindPerkComponentFor(this))
	{
		Perk->DebugReset();
		UE_LOG(LogMDInventory, Warning, TEXT("ResetPerks -> spent %d, available %d"), Perk->GetPointsSpent(), Perk->GetPointsAvailable());
	}
#endif
}

void AMDPlayerController::DumpPerks()
{
#if !UE_BUILD_SHIPPING
	const AMDPlayerState* PS = GetPlayerState<AMDPlayerState>();
	const UMDPerkComponent* Perk = UMDPerkComponent::FindFor(this);
	const UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (!Perk || !ASC)
	{
		UE_LOG(LogMDInventory, Warning, TEXT("DumpPerks: no UMDPerkComponent / ASC for local player %s"), *GetNameSafe(this));
		return;
	}

	const UMDPerkTree* Tree = UMDGameSettings::GetPerkTree();
	UE_LOG(LogMDInventory, Warning, TEXT("==== Perks [%s] points earned %d  spent %d  available %d  (debug bonus %d)  second wind used %d ===="),
		NetModeName(GetNetMode()), Perk->GetPointsEarned(), Perk->GetPointsSpent(), Perk->GetPointsAvailable(),
		Perk->DebugBonusPoints, Perk->bSecondWindUsed ? 1 : 0);
	for (int32 PathIndex = 1; PathIndex <= 4; ++PathIndex)
	{
		const EMDPerkPath Path = static_cast<EMDPerkPath>(PathIndex);
		UE_LOG(LogMDInventory, Warning, TEXT("  %s: %d point(s)"), *UMDPerkCatalog::GetPathText(Path).ToString(), Perk->GetPathPoints(Path));
	}
	for (int32 i = 0; i < Tree->Nodes.Num(); ++i)
	{
		const FMDPerkNode& Node = Tree->Nodes[i];
		UE_LOG(LogMDInventory, Warning, TEXT("  [%2d] %-15s rank %d/%d  (%s)"), i, *Node.Id.ToString(), Perk->GetRank(i), Node.MaxRank,
			*UMDPerkCatalog::GetEffectDetailText(Node, FMath::Max(Perk->GetRank(i), 1)).ToString());
	}

	const UMDStaminaComponent* Stamina = PS->StaminaComponent;
	UE_LOG(LogMDInventory, Warning, TEXT("  MaxHealth %.1f  Defense %.2f%% (cap %.0f)  AttackPower %.2f  CritChance %.3f  CritDamage %.2f  HealthRegen %.2f/s"),
		ASC->GetMaxHealth(), ASC->GetDefense(), ASC->DefenseCap, ASC->GetAttackPower(), ASC->GetCritChance(), ASC->GetCritDamage(), ASC->GetHealthRegen());
	UE_LOG(LogMDInventory, Warning, TEXT("  MaxStamina %.1f  StaminaRegen %.2f/s  RegenDelay %s  StaminaCostScale %.2f  SprintSpeedScale %.2f  timeSinceDamage %.1fs"),
		ASC->GetMaxStamina(), ASC->GetStaminaRegenRate(),
		Stamina ? *FString::Printf(TEXT("%.2fs (base %.2f)"), Stamina->GetEffectiveRegenDelay(), Stamina->RegenDelay) : TEXT("n/a"),
		ASC->GetStaminaCostScale(), ASC->GetSprintSpeedScale(), Perk->GetTimeSinceDamage());
#endif
}
