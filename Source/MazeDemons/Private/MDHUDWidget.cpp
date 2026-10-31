#include "MDHUDWidget.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDMapComponent.h"
#include "MDLevelMap.h"
#include "MDMapZone.h"
#include "MDQuestComponent.h"
#include "MDPerkComponent.h"
#include "MDPlayerController.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"
#include "MDStatusBarWidget.h"
#include "Ranged/MDHUDOptions.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

float UMDHUDWidget::SafePercent(float Value, float Max)
{
	return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(Value / Max, 0.0f, 1.0f) : 0.0f;
}

void UMDHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UMDHUDOptions::ApplySavedOptions(); // R7: pushes the saved off-screen threat toggle into md.UI.OffscreenThreat
	TryBind();
}

void UMDHUDWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UMDHUDWidget::Unbind()
{
	if (UMDAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->OnHealthChanged.RemoveDynamic(this, &UMDHUDWidget::HandleHealthChanged);
		ASC->OnMaxHealthChanged.RemoveDynamic(this, &UMDHUDWidget::HandleMaxHealthChanged);
		ASC->OnStaminaChanged.RemoveDynamic(this, &UMDHUDWidget::HandleStaminaChanged);
		ASC->OnMaxStaminaChanged.RemoveDynamic(this, &UMDHUDWidget::HandleMaxStaminaChanged);
	}
	if (UMDStaminaComponent* Comp = BoundStamina.Get())
	{
		Comp->OnExhaustedChanged.RemoveDynamic(this, &UMDHUDWidget::HandleExhaustedChanged);
	}
	if (UMDPerkComponent* Perks = BoundPerks.Get())
	{
		Perks->OnPerksChanged.RemoveDynamic(this, &UMDHUDWidget::HandlePerksChanged);
	}
	if (AMDPlayerController* PC = BoundController.Get())
	{
		PC->OnPickupNotified.RemoveDynamic(this, &UMDHUDWidget::HandlePickupNotified);
		PC->OnUseRejected.RemoveDynamic(this, &UMDHUDWidget::HandleUseRejected);
		PC->OnToastNotified.RemoveDynamic(this, &UMDHUDWidget::HandleToast);
	}
	if (StatusBar)
	{
		StatusBar->RemoveFromParent();
		StatusBar = nullptr;
	}
	BoundASC.Reset();
	BoundStamina.Reset();
	BoundController.Reset();
	BoundPerks.Reset();
	if (UMDQuestComponent* Quests = BoundQuests.Get())
	{
		Quests->OnQuestsChanged.RemoveDynamic(this, &UMDHUDWidget::HandleQuestsChanged);
	}
	if (UMDMapComponent* Map = BoundMap.Get())
	{
		Map->OnZoneChanged.RemoveDynamic(this, &UMDHUDWidget::HandleMapZoneChanged);
		Map->OnLevelMapChanged.RemoveDynamic(this, &UMDHUDWidget::HandleLevelMapChanged);
	}
	BoundQuests.Reset();
	BoundMap.Reset();
	LastCompletedZones = -1;
	bBound = false;
}

void UMDHUDWidget::TryBindQuests()
{
	// The game state can replicate after the PlayerState, so this retries from NativeTick until both are found.
	if (!BoundMap.IsValid())
	{
		if (UMDMapComponent* Map = UMDMapComponent::FindFor(GetOwningPlayer()))
		{
			BoundMap = Map;
			Map->OnZoneChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleMapZoneChanged);
			Map->OnLevelMapChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleLevelMapChanged);
			if (Map->GetLevelMap() && Map->GetMapTexture())
			{
				OnLevelMapReady(Map->GetLevelMap());
			}
			if (AMDMapZone* Zone = Map->GetCurrentZone())
			{
				OnMapZoneChanged(Zone);
			}
		}
	}
	if (!BoundQuests.IsValid())
	{
		if (UMDQuestComponent* Quests = UMDQuestComponent::FindFor(this))
		{
			if (Quests->Quests.Num() == 0)
			{
				return; // not replicated yet; OnQuestsChanged will come, but bind only once there is something to show
			}
			BoundQuests = Quests;
			Quests->OnQuestsChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleQuestsChanged);
			RefreshQuest(false);
		}
	}
}

void UMDHUDWidget::RefreshQuest(bool bAllowJustCompleted)
{
	const UMDQuestComponent* Quests = BoundQuests.Get();
	if (!Quests)
	{
		return;
	}
	const int32 Completed = UMDQuestRules::CountZonesCompleted(Quests->Quests);
	const bool bJustCompleted = bAllowJustCompleted && LastCompletedZones >= 0 && Completed > LastCompletedZones;
	LastCompletedZones = Completed;

	FMDQuestProgress Active;
	if (Quests->GetActiveQuest(Active))
	{
		QuestZone = Active.ZoneIndex;
		QuestTitle = Quests->GetQuestTitle(Active.ZoneIndex);
		ObjectiveText = Quests->GetObjectiveText(Active.ZoneIndex);
		ObjectiveCount = Active.Count;
		ObjectiveRequired = Active.Required;
	}
	else
	{
		QuestZone = 0;
		QuestTitle = FText::GetEmpty();
		ObjectiveText = FText::GetEmpty();
		ObjectiveCount = 0;
		ObjectiveRequired = 1;
	}
	bAllQuestsDone = Quests->AreAllQuestsDone();
	OnQuestUpdated(bJustCompleted);
}

void UMDHUDWidget::HandleQuestsChanged()
{
	RefreshQuest(true);
}

void UMDHUDWidget::HandleMapZoneChanged(AMDMapZone* Zone)
{
	OnMapZoneChanged(Zone);
}

void UMDHUDWidget::HandleLevelMapChanged(AMDLevelMap* LevelMap)
{
	OnLevelMapReady(LevelMap);
}

void UMDHUDWidget::TryBind()
{
	AMDPlayerController* PC = Cast<AMDPlayerController>(GetOwningPlayer());
	AMDPlayerState* PS = PC ? PC->GetPlayerState<AMDPlayerState>() : nullptr;
	UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
	if (!PC || !PS || !ASC)
	{
		return; // PlayerState not replicated yet; NativeTick retries.
	}
	if (bBound && BoundASC.Get() == ASC)
	{
		return;
	}
	Unbind();

	BoundASC = ASC;
	BoundController = PC;
	BoundStamina = PS->StaminaComponent;

	ASC->OnHealthChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleHealthChanged);
	ASC->OnMaxHealthChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleMaxHealthChanged);
	ASC->OnStaminaChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleStaminaChanged);
	ASC->OnMaxStaminaChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleMaxStaminaChanged);
	if (UMDStaminaComponent* Comp = BoundStamina.Get())
	{
		Comp->OnExhaustedChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandleExhaustedChanged);
		bExhausted = Comp->IsExhausted();
	}
	BoundPerks = PS->PerkComponent;
	if (UMDPerkComponent* Perks = BoundPerks.Get())
	{
		Perks->OnPerksChanged.AddUniqueDynamic(this, &UMDHUDWidget::HandlePerksChanged);
		PerkPointsAvailable = Perks->GetPointsAvailable();
	}
	PC->OnPickupNotified.AddUniqueDynamic(this, &UMDHUDWidget::HandlePickupNotified);
	PC->OnUseRejected.AddUniqueDynamic(this, &UMDHUDWidget::HandleUseRejected);
	PC->OnToastNotified.AddUniqueDynamic(this, &UMDHUDWidget::HandleToast);

	// Seed the cache so the first frame is correct even if no attribute change follows.
	Health = ASC->GetHealth();
	MaxHealth = ASC->GetMaxHealth();
	Stamina = ASC->GetStamina();
	MaxStamina = ASC->GetMaxStamina();
	bDead = ASC->IsDead();
	bBound = true;

	CreateStatusBar(PC);

	OnBound();
	OnHealthUpdated(GetHealthPercent(), 0.0f);
	OnStaminaUpdated(GetStaminaPercent(), bExhausted);
	OnDeathStateChanged(bDead);
	OnPerkPointsChanged(PerkPointsAvailable);
}

void UMDHUDWidget::CreateStatusBar(APlayerController* PC)
{
	// Only a real local player's HUD gets one (not a dedicated-server or headless widget).
	if (!bShowStatusBar || StatusBar || !PC || !PC->IsLocalController() || !GetWorld() || !GetWorld()->GetGameViewport())
	{
		return;
	}
	StatusBar = CreateWidget<UMDStatusBarWidget>(PC);
	if (StatusBar)
	{
		StatusBar->AddToViewport(40);
		StatusBar->SetAnchorsInViewport(FAnchors(0.0f, 1.0f));
		StatusBar->SetAlignmentInViewport(FVector2D(0.0f, 1.0f));
		StatusBar->SetPositionInViewport(StatusBarOffset, false);
	}
}

void UMDHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Rebinds after a PlayerState change and covers the not-yet-replicated case.
	TryBind();
	TryBindQuests();
	if (!bBound)
	{
		return;
	}

	// Death / respawn: the ASC fires OnDeath once per death but nothing on revival, so poll the (replicated) flag.
	const UMDAbilitySystemComponent* ASC = BoundASC.Get();
	const bool bNowDead = ASC && ASC->IsDead();
	if (bNowDead != bDead)
	{
		bDead = bNowDead;
		OnDeathStateChanged(bDead);
	}

	// Death countdown from the replicated server respawn time.
	float Remaining = 0.0f;
	if (bDead)
	{
		const AMDPlayerController* PC = BoundController.Get();
		const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		if (PC && GS && PC->RespawnAtServerTime > 0.0)
		{
			Remaining = FMath::Max(0.0f, static_cast<float>(PC->RespawnAtServerTime - GS->GetServerWorldTimeSeconds()));
		}
	}
	RespawnSecondsRemaining = Remaining;

	// Gold: the inventory component can live on the pawn, controller or PlayerState and changes with the pawn on
	// respawn, so read it by value instead of binding to one instance.
	const AMDPlayerController* PC = BoundController.Get();
	UMDInventoryComponent* Inv = PC ? UMDGameplayStatics::GetInventory(PC->GetPawn()) : nullptr;
	if (!Inv && PC)
	{
		Inv = UMDGameplayStatics::GetInventory(PC);
	}
	if (Inv)
	{
		const int32 NewGold = Inv->GetGold();
		if (NewGold != Gold)
		{
			const int32 Delta = NewGold - Gold;
			Gold = NewGold;
			OnGoldUpdated(Gold, Delta);
		}
	}
}

void UMDHUDWidget::HandleHealthChanged(float OldValue, float NewValue)
{
	Health = NewValue;
	if (const UMDAbilitySystemComponent* ASC = BoundASC.Get())
	{
		MaxHealth = ASC->GetMaxHealth();
	}
	OnHealthUpdated(GetHealthPercent(), NewValue - OldValue);
}

void UMDHUDWidget::HandleMaxHealthChanged(float OldValue, float NewValue)
{
	MaxHealth = NewValue;
	OnHealthUpdated(GetHealthPercent(), 0.0f);
}

void UMDHUDWidget::HandleStaminaChanged(float OldValue, float NewValue)
{
	Stamina = NewValue;
	if (const UMDAbilitySystemComponent* ASC = BoundASC.Get())
	{
		MaxStamina = ASC->GetMaxStamina();
	}
	OnStaminaUpdated(GetStaminaPercent(), bExhausted);
}

void UMDHUDWidget::HandleMaxStaminaChanged(float OldValue, float NewValue)
{
	MaxStamina = NewValue;
	OnStaminaUpdated(GetStaminaPercent(), bExhausted);
}

void UMDHUDWidget::HandleExhaustedChanged(bool bNowExhausted)
{
	bExhausted = bNowExhausted;
	OnStaminaUpdated(GetStaminaPercent(), bExhausted);
}

void UMDHUDWidget::HandlePerksChanged()
{
	const UMDPerkComponent* Perks = BoundPerks.Get();
	const int32 Available = Perks ? Perks->GetPointsAvailable() : 0;
	if (Available != PerkPointsAvailable)
	{
		PerkPointsAvailable = Available;
		OnPerkPointsChanged(Available);
	}
}

void UMDHUDWidget::HandlePickupNotified(FName ItemRowName, int32 Quantity)
{
	OnPickupToast(ItemRowName, Quantity);
}

void UMDHUDWidget::HandleToast(FText Message, EMDColor Tone)
{
	if (!Message.IsEmpty())
	{
		OnToast(Message, Tone);
	}
}

void UMDHUDWidget::HandleUseRejected(EMDUseRejectReason Reason, float CooldownRemaining)
{
	const FText Message = UMDInventoryComponent::GetUseRejectText(Reason, CooldownRemaining);
	if (!Message.IsEmpty())
	{
		OnUseRejectedToast(Message);
	}
}
