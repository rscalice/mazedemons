#include "MDGameplayStatics.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDInventoryComponent.h"
#include "MDPlayerState.h"
#include "MDPlayerController.h"
#include "MDGameSettings.h"
#include "MDStaminaComponent.h"
#include "MDStatCalculator.h"
#include "MDUISettings.h"
#include "Engine/DataTable.h"

UMDInventoryComponent* UMDGameplayStatics::GetInventory(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	// Accepts a pawn, controller or PlayerState. Checks the actor itself, then controller, then PlayerState.
	if (UMDInventoryComponent* Inv = Actor->FindComponentByClass<UMDInventoryComponent>())
	{
		return Inv;
	}
	const AController* Controller = Cast<AController>(Actor);
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		Controller = Pawn->GetController();
		PS = Pawn->GetPlayerState();
	}
	if (Controller)
	{
		if (UMDInventoryComponent* Inv = Controller->FindComponentByClass<UMDInventoryComponent>())
		{
			return Inv;
		}
		if (!PS)
		{
			PS = Controller->PlayerState;
		}
	}
	return PS ? PS->FindComponentByClass<UMDInventoryComponent>() : nullptr;
}

FString UMDGameplayStatics::SplitCamel(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len() + 4);
	for (int32 i = 0; i < In.Len(); ++i)
	{
		const TCHAR C = In[i];
		if (C == TEXT('_'))
		{
			Out.AppendChar(TEXT(' '));
			continue;
		}
		if (i > 0 && FChar::IsUpper(C))
		{
			const TCHAR Prev = In[i - 1];
			const TCHAR Next = (i + 1 < In.Len()) ? In[i + 1] : TEXT('\0');
			const bool bAfterLowerOrDigit = FChar::IsLower(Prev) || FChar::IsDigit(Prev);
			const bool bAcronymEnd = FChar::IsUpper(Prev) && FChar::IsLower(Next);
			if (bAfterLowerOrDigit || bAcronymEnd)
			{
				Out.AppendChar(TEXT(' '));
			}
		}
		Out.AppendChar(C);
	}
	return Out;
}

FText UMDGameplayStatics::GetItemDisplayName(FName Row)
{
	if (Row.IsNone())
	{
		return FText::GetEmpty();
	}
	const UMDUISettings* Settings = GetDefault<UMDUISettings>();
	if (const UDataTable* Table = Settings ? Settings->ItemDisplayTable.LoadSynchronous() : nullptr)
	{
		if (const FMDItemDisplayRow* Found = Table->FindRow<FMDItemDisplayRow>(Row, TEXT("GetItemDisplayName"), false))
		{
			if (!Found->DisplayName.IsEmpty())
			{
				return Found->DisplayName;
			}
		}
	}
	return FText::FromString(SplitCamel(Row.ToString()));
}

#define LOCTEXT_NAMESPACE "MDStats"

float UMDGameplayStatics::SwapDamageReduction(float CurrentPct, float OldPiecePct, float NewPiecePct)
{
	// Defense is 1 - product(1 - piece). Divide the old piece out, multiply the new one in.
	const float OldFraction = FMath::Max(1.0f - OldPiecePct / 100.0f, 0.01f);
	const float Remaining = (1.0f - CurrentPct / 100.0f) * (1.0f - NewPiecePct / 100.0f) / OldFraction;
	return FMath::Clamp((1.0f - Remaining) * 100.0f, 0.0f, 90.0f);
}

float UMDGameplayStatics::HealthAfterHeal(float Health, float MaxHealth, float HealAmount)
{
	return FMath::Min(MaxHealth, Health + FMath::Max(HealAmount, 0.0f));
}

namespace
{
	int32 SignOfDelta(float Current, float New)
	{
		const float Delta = New - Current;
		return Delta > 0.005f ? 1 : (Delta < -0.005f ? -1 : 0);
	}

	FMDStatLine MakeLine(const FText& Label, float Current, float New, bool bPercent, bool bRatio, float Max, float NewMax, const TCHAR* Suffix)
	{
		FMDStatLine Line;
		Line.Label = Label;
		Line.Current = Current;
		Line.New = New;
		Line.bPercent = bPercent;
		Line.bRatio = bRatio;
		Line.Max = Max;
		Line.DeltaSign = SignOfDelta(Current, New);

		auto Format = [&](float Value, float InMax) -> FText
		{
			if (bRatio)
			{
				return FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Value), FMath::RoundToInt(InMax)));
			}
			if (bPercent)
			{
				return FText::FromString(FString::Printf(TEXT("%.1f%%"), Value));
			}
			return FText::FromString(FString::Printf(TEXT("%d%s"), FMath::RoundToInt(Value), Suffix));
		};
		Line.CurrentText = Format(Current, Max);
		Line.NewText = Format(New, NewMax);
		return Line;
	}

	const FMDEquippedSlot* FindEquipped(const TArray<FMDEquippedSlot>& Equipped, uint8 Slot)
	{
		return Equipped.FindByPredicate([Slot](const FMDEquippedSlot& E) { return E.Slot == Slot; });
	}
}

TArray<FMDStatLine> UMDGameplayStatics::GetCharacterStatLines(APlayerState* PS, FName CompareRow)
{
	TArray<FMDStatLine> Lines;
	const UMDAbilitySystemComponent* ASC = GetMDAbilitySystemComponent(PS);
	if (!ASC)
	{
		return Lines;
	}
	const UMDInventoryComponent* Inv = GetInventory(PS);

	const float Health = ASC->GetHealth();
	const float MaxHealth = ASC->GetMaxHealth();
	const float Stamina = ASC->GetStamina();
	const float MaxStamina = ASC->GetMaxStamina();
	const float Regen = ASC->GetStaminaRegenRate();
	const float AttackPower = ASC->GetAttackPower();
	const float BaseDefense = ASC->GetBaseDefense();
	const float Defense = ASC->GetDefense();

	float NewHealth = Health;
	float NewAttackPower = AttackPower;
	float NewDefense = Defense;

	if (Inv && !CompareRow.IsNone())
	{
		if (Inv->IsConsumable(CompareRow))
		{
			NewHealth = HealthAfterHeal(Health, MaxHealth, Inv->GetConsumableHeal(CompareRow));
		}
		else
		{
			const uint8 Slot = Inv->GetItemEquipSlot(CompareRow);
			const TArray<FMDEquippedSlot>& Equipped = Inv->GetEquipped();
			if (Slot >= 4 && Slot <= 8)
			{
				const FMDEquippedSlot* Old = FindEquipped(Equipped, Slot);
				const float OldPct = Old ? Inv->GetItemArmorReduction(Old->ItemRowName) : 0.0f;
				NewDefense = SwapDamageReduction(Defense, OldPct, Inv->GetItemArmorReduction(CompareRow));
			}
			else if (Slot == 0 || Slot == 1 || Slot == 3)
			{
				auto BonusIn = [&](uint8 S) -> float
				{
					const FMDEquippedSlot* E = FindEquipped(Equipped, S);
					return E ? Inv->GetItemAttackBonus(E->ItemRowName) : 0.0f;
				};
				// A two-handed weapon occupies both hands (it is the only bonus that counts while equipped), so it is what
				// gets replaced; otherwise the bonus removed is the item in the same slot.
				const bool bHasTwoHanded = FindEquipped(Equipped, 3) != nullptr;
				const float HandsBonus = bHasTwoHanded ? BonusIn(3) : BonusIn(0) + BonusIn(1);
				const float Removed = (Slot == 3 || bHasTwoHanded) ? HandsBonus : BonusIn(Slot);
				NewAttackPower = FMath::Max(0.0f, AttackPower - Removed + Inv->GetItemAttackBonus(CompareRow));
			}
		}
	}

	NewDefense = FMath::Min(NewDefense, ASC->DefenseCap); // the preview obeys the player cap too

	Lines.Add(MakeLine(LOCTEXT("StatHealth", "Health"), Health, NewHealth, false, true, MaxHealth, MaxHealth, TEXT("")));
	Lines.Add(MakeLine(LOCTEXT("StatStamina", "Stamina"), Stamina, Stamina, false, true, MaxStamina, MaxStamina, TEXT("")));
	Lines.Add(MakeLine(LOCTEXT("StatStaminaRegen", "Stamina regen"), Regen, Regen, false, false, 0.0f, 0.0f, TEXT(" /s")));
	Lines.Add(MakeLine(LOCTEXT("StatAttackPower", "Attack power"), AttackPower, NewAttackPower, false, false, 0.0f, 0.0f, TEXT("")));
	Lines.Add(MakeLine(LOCTEXT("StatDefense", "Defense"), BaseDefense, BaseDefense, true, false, 0.0f, 0.0f, TEXT("")));
	Lines.Add(MakeLine(LOCTEXT("StatDamageReduction", "Damage reduction"), Defense, NewDefense, true, false, 0.0f, 0.0f, TEXT("")));
	return Lines;
}

#undef LOCTEXT_NAMESPACE

EMDColor UMDGameplayStatics::DurabilityColor(float Fraction)
{
	const float Clamped = FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (Clamped >= 0.5f)
	{
		return EMDColor::Success;
	}
	return Clamped >= 0.25f ? EMDColor::Warning : EMDColor::Danger;
}

void UMDGameplayStatics::AwardKillGold(const AActor* Victim, const AController* Killer, int32 Gold)
{
	const UWorld* World = Victim ? Victim->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState || Gold <= 0 || !Victim->HasAuthority())
	{
		return;
	}

	TArray<AMDPlayerState*> Players;
	TArray<float> Distances; // -1 = no pawn, never a recipient
	int32 KillerIndex = INDEX_NONE;
	for (APlayerState* State : GameState->PlayerArray)
	{
		AMDPlayerState* PlayerState = Cast<AMDPlayerState>(State);
		if (!PlayerState)
		{
			continue;
		}
		const APawn* Pawn = PlayerState->GetPawn(); // a dead player's corpse still counts until it respawns
		Distances.Add(Pawn ? FVector::Dist(Pawn->GetActorLocation(), Victim->GetActorLocation()) : -1.0f);
		if (Killer && Killer->PlayerState.Get() == PlayerState)
		{
			KillerIndex = Players.Num();
		}
		Players.Add(PlayerState);
	}

	TArray<int32> Shares;
	UMDStatCalculator::DistributeKillGold(Gold, Distances, UMDGameSettings::GetEconomy().KillGoldRadius, KillerIndex, Shares);
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (Shares[i] <= 0)
		{
			continue;
		}
		UMDInventoryComponent* Inventory = GetInventory(Players[i]);
		if (!Inventory)
		{
			Inventory = GetInventory(Players[i]->GetPawn());
		}
		if (!Inventory)
		{
			continue;
		}
		Inventory->AddGold(Shares[i]);
		if (AMDPlayerController* Controller = Cast<AMDPlayerController>(Players[i]->GetOwner()))
		{
			Controller->NotifyToast(FText::Format(NSLOCTEXT("MDGold", "KillGoldToast", "+{0} Gold"), FText::AsNumber(Shares[i])), EMDColor::Gold);
		}
	}
}

UMDAbilitySystemComponent* UMDGameplayStatics::GetMDAbilitySystemComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	if (!ASC)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (const IAbilitySystemInterface* PSInterface = Cast<IAbilitySystemInterface>(Pawn->GetPlayerState()))
			{
				ASC = PSInterface->GetAbilitySystemComponent();
			}
		}
	}
	if (!ASC)
	{
		return Actor->FindComponentByClass<UMDAbilitySystemComponent>();
	}
	return Cast<UMDAbilitySystemComponent>(ASC);
}

UMDStaminaComponent* UMDGameplayStatics::GetStaminaComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDStaminaComponent* Comp = Actor->FindComponentByClass<UMDStaminaComponent>())
	{
		return Comp;
	}
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		PS = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		PS = Controller->PlayerState;
	}
	return PS ? PS->FindComponentByClass<UMDStaminaComponent>() : nullptr;
}

void UMDGameplayStatics::GetStamina(const AActor* Actor, float& Stamina, float& MaxStamina)
{
	const UMDAbilitySystemComponent* ASC = GetMDAbilitySystemComponent(Actor);
	Stamina = ASC ? ASC->GetStamina() : 0.0f;
	MaxStamina = ASC ? ASC->GetMaxStamina() : 0.0f;
}

bool UMDGameplayStatics::CanSprint(const APawn* Pawn)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Pawn);
	return !Comp || Comp->IsSprintAllowedFor(Pawn && Pawn->IsLocallyControlled());
}

bool UMDGameplayStatics::HasStaminaForJump(const AActor* Actor)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Actor);
	return !Comp || Comp->CanJump();
}

bool UMDGameplayStatics::HasStaminaForVault(const AActor* Actor)
{
	const UMDStaminaComponent* Comp = GetStaminaComponent(Actor);
	return !Comp || Comp->CanVault();
}

void UMDGameplayStatics::NotifyJumped(const AActor* Actor)
{
	if (UMDStaminaComponent* Comp = GetStaminaComponent(Actor))
	{
		Comp->NotifyJumped();
	}
}

void UMDGameplayStatics::NotifyTraversalStarted(const AActor* Actor)
{
	if (UMDStaminaComponent* Comp = GetStaminaComponent(Actor))
	{
		Comp->NotifyTraversalStarted();
	}
}

uint8 UMDGameplayStatics::GetTeamId(const AActor* Actor)
{
	if (!Actor)
	{
		return MDTeams::Demons;
	}

	const APawn* Pawn = Cast<APawn>(Actor);
	const AController* Controller = Cast<AController>(Actor);
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (Pawn)
	{
		PS = Pawn->GetPlayerState();
		Controller = Pawn->GetController();
	}
	else if (Controller)
	{
		PS = Controller->PlayerState;
	}

	if (Cast<AMDPlayerState>(PS) || (Controller && Controller->IsPlayerController()))
	{
		return MDTeams::Players;
	}

	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Actor))
	{
		const FGenericTeamId Id = TeamAgent->GetGenericTeamId();
		if (Id != FGenericTeamId::NoTeam)
		{
			return Id.GetId();
		}
	}
	if (const IGenericTeamAgentInterface* ControllerAgent = Cast<IGenericTeamAgentInterface>(Controller))
	{
		const FGenericTeamId Id = ControllerAgent->GetGenericTeamId();
		if (Id != FGenericTeamId::NoTeam)
		{
			return Id.GetId();
		}
	}
	return MDTeams::Demons;
}

bool UMDGameplayStatics::CanDamage(const AActor* DamageInstigator, const AActor* Target)
{
	if (!DamageInstigator || DamageInstigator == Target)
	{
		return true;
	}

	const uint8 InstigatorTeam = GetTeamId(DamageInstigator);
	const uint8 TargetTeam = GetTeamId(Target);
	if (InstigatorTeam != TargetTeam)
	{
		return true;
	}
	if (InstigatorTeam == MDTeams::Players)
	{
		return AMDGameState::IsFriendlyFireEnabled(DamageInstigator);
	}
	return false;
}

void UMDGameplayStatics::RequestPickup(AActor* Pickup, const APawn* Interactor)
{
	if (!Pickup || !Interactor)
	{
		return;
	}
	if (AMDPlayerController* Controller = Cast<AMDPlayerController>(Interactor->GetController()))
	{
		Controller->RequestPickup(Pickup);
	}
}