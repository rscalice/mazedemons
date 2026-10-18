#include "MDHUDWidget.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPlayerController.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"

#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

float UMDHUDWidget::SafePercent(float Value, float Max)
{
	return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(Value / Max, 0.0f, 1.0f) : 0.0f;
}

void UMDHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
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
	if (AMDPlayerController* PC = BoundController.Get())
	{
		PC->OnPickupNotified.RemoveDynamic(this, &UMDHUDWidget::HandlePickupNotified);
		PC->OnUseRejected.RemoveDynamic(this, &UMDHUDWidget::HandleUseRejected);
	}
	BoundASC.Reset();
	BoundStamina.Reset();
	BoundController.Reset();
	bBound = false;
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
	PC->OnPickupNotified.AddUniqueDynamic(this, &UMDHUDWidget::HandlePickupNotified);
	PC->OnUseRejected.AddUniqueDynamic(this, &UMDHUDWidget::HandleUseRejected);

	// Seed the cache so the first frame is correct even if no attribute change follows.
	Health = ASC->GetHealth();
	MaxHealth = ASC->GetMaxHealth();
	Stamina = ASC->GetStamina();
	MaxStamina = ASC->GetMaxStamina();
	bDead = ASC->IsDead();
	bBound = true;

	OnBound();
	OnHealthUpdated(GetHealthPercent(), 0.0f);
	OnStaminaUpdated(GetStaminaPercent(), bExhausted);
	OnDeathStateChanged(bDead);
}

void UMDHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Rebinds after a PlayerState change and covers the not-yet-replicated case.
	TryBind();
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

void UMDHUDWidget::HandlePickupNotified(FName ItemRowName, int32 Quantity)
{
	OnPickupToast(ItemRowName, Quantity);
}

void UMDHUDWidget::HandleUseRejected(EMDUseRejectReason Reason, float CooldownRemaining)
{
	const FText Message = UMDInventoryComponent::GetUseRejectText(Reason, CooldownRemaining);
	if (!Message.IsEmpty())
	{
		OnUseRejectedToast(Message);
	}
}
