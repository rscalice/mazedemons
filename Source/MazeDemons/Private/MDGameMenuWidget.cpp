#include "MDGameMenuWidget.h"

#include "AbilitySystemInterface.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameSettings.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPerkComponent.h"
#include "MDPlayerController.h"
#include "MDPlayerState.h"

// ---- Pure rules ----

bool UMDGameMenuWidget::IsTabSelectable(EMDMenuTab Tab)
{
	return Tab == EMDMenuTab::Inventory || Tab == EMDMenuTab::Perks || Tab == EMDMenuTab::Run;
}

EMDMenuTab UMDGameMenuWidget::StepTab(EMDMenuTab Current, int32 Direction)
{
	if (Direction == 0)
	{
		return Current;
	}
	const int32 Step = Direction > 0 ? 1 : -1;
	// Tabs are 1..NumTabs; None (0) steps as if it sat just outside the ring.
	int32 Index = static_cast<int32>(Current);
	if (Index < 1 || Index > NumTabs)
	{
		Index = Step > 0 ? 0 : NumTabs + 1;
	}
	for (int32 i = 0; i < NumTabs; ++i)
	{
		Index += Step;
		if (Index > NumTabs)
		{
			Index = 1;
		}
		else if (Index < 1)
		{
			Index = NumTabs;
		}
		const EMDMenuTab Candidate = static_cast<EMDMenuTab>(Index);
		if (IsTabSelectable(Candidate))
		{
			return Candidate;
		}
	}
	return Current;
}

int32 UMDGameMenuWidget::ComputeTabBadge(EMDMenuTab Tab, int32 PerkPointsAvailable)
{
	return Tab == EMDMenuTab::Perks ? FMath::Max(PerkPointsAvailable, 0) : 0;
}

// ---- Binding ----

void UMDGameMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!IsTabSelectable(ActiveTab))
	{
		ActiveTab = EMDMenuTab::Inventory;
	}
	TryBind();
}

void UMDGameMenuWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UMDGameMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TryBind(); // PlayerState may not be replicated yet at construct time
}

void UMDGameMenuWidget::Unbind()
{
	if (UMDPerkComponent* Perks = PerkComponent.Get())
	{
		Perks->OnPerksChanged.RemoveDynamic(this, &UMDGameMenuWidget::HandlePerksChanged);
	}
	PerkComponent.Reset();
}

void UMDGameMenuWidget::TryBind()
{
	const AMDPlayerState* PS = Cast<AMDPlayerState>(GetOwningPlayerState());
	UMDPerkComponent* Perks = PS ? PS->PerkComponent.Get() : nullptr;
	if (!Perks || PerkComponent.Get() == Perks)
	{
		return;
	}
	Unbind();
	PerkComponent = Perks;
	Perks->OnPerksChanged.AddUniqueDynamic(this, &UMDGameMenuWidget::HandlePerksChanged);
	HandlePerksChanged();
}

void UMDGameMenuWidget::HandlePerksChanged()
{
	OnBadgesChanged();
}

// ---- Tabs ----

void UMDGameMenuWidget::SetActiveTab(EMDMenuTab NewTab)
{
	if (!IsTabAvailable(NewTab) || NewTab == ActiveTab)
	{
		return;
	}
	const EMDMenuTab Old = ActiveTab;
	ActiveTab = NewTab;
	OnActiveTabChanged(NewTab, Old);
}

void UMDGameMenuWidget::NextTab()
{
	// Skip unavailable tabs so a hidden tab can never be landed on.
	EMDMenuTab Tab = ActiveTab;
	for (int32 i = 0; i < NumTabs; ++i)
	{
		Tab = StepTab(Tab, 1);
		if (IsTabAvailable(Tab))
		{
			SetActiveTab(Tab);
			return;
		}
	}
}

void UMDGameMenuWidget::PrevTab()
{
	EMDMenuTab Tab = ActiveTab;
	for (int32 i = 0; i < NumTabs; ++i)
	{
		Tab = StepTab(Tab, -1);
		if (IsTabAvailable(Tab))
		{
			SetActiveTab(Tab);
			return;
		}
	}
}

bool UMDGameMenuWidget::IsTabAvailable(EMDMenuTab Tab) const
{
	return IsTabSelectable(Tab); // all three tabs are always available in M1
}

int32 UMDGameMenuWidget::GetTabBadge(EMDMenuTab Tab) const
{
	const UMDPerkComponent* Perks = PerkComponent.Get();
	return ComputeTabBadge(Tab, Perks ? Perks->GetPointsAvailable() : 0);
}

FText UMDGameMenuWidget::GetTabLabel(EMDMenuTab Tab) const
{
	switch (Tab)
	{
	case EMDMenuTab::Inventory: return NSLOCTEXT("MDMenu", "TabInventory", "Inventory");
	case EMDMenuTab::Perks: return NSLOCTEXT("MDMenu", "TabPerks", "Perks");
	case EMDMenuTab::Run: return NSLOCTEXT("MDMenu", "TabRun", "Run");
	default: return FText::GetEmpty();
	}
}

void UMDGameMenuWidget::CloseMenu()
{
	if (AMDPlayerController* PC = Cast<AMDPlayerController>(GetOwningPlayer()))
	{
		PC->CloseGameMenu();
	}
}

// ---- Keys ----

FReply UMDGameMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Only consume keys while the menu is on screen.
	if (!IsInViewport() || !IsVisible())
	{
		return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Tab || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		if (!InKeyEvent.IsRepeat())
		{
			CloseMenu();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
	{
		PrevTab();
		return FReply::Handled();
	}
	if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)
	{
		NextTab();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// ---- Run tab data ----

void UMDGameMenuWidget::GetRunSummary(FMDRunSummary& Out) const
{
	Out = FMDRunSummary();
	Out.ZoneTotal = UMDGameSettings::GetPerkTuning().MaxZones;

	if (const UWorld* World = GetWorld())
	{
		if (const AMDGameState* State = World->GetGameState<AMDGameState>())
		{
			Out.ZonesCompleted = FMath::Clamp(State->ZonesCompleted, 0, Out.ZoneTotal);
			Out.SoulsSaved = State->SoulsSaved;
		}
	}
	// RunTimeSeconds stays 0: AMDGameState has no run start time yet.

	const UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetOwningPlayerPawn());
	if (!Inv)
	{
		Inv = UMDGameplayStatics::GetInventory(GetOwningPlayerState());
	}
	if (Inv)
	{
		Out.Gold = Inv->GetGold();
		Out.Shards = Inv->GetShardCount();
	}
}

TArray<FMDPartyMemberView> UMDGameMenuWidget::GetPartyMembers() const
{
	TArray<FMDPartyMemberView> Result;
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	if (!State)
	{
		return Result;
	}
	const APlayerState* Local = GetOwningPlayerState();
	for (const APlayerState* PS : State->PlayerArray)
	{
		const AMDPlayerState* MDState = Cast<AMDPlayerState>(PS);
		if (!MDState)
		{
			continue;
		}
		FMDPartyMemberView View;
		View.Name = MDState->GetPlayerName();
		View.bIsLocal = (MDState == Local);
		if (const UMDAbilitySystemComponent* ASC = MDState->GetMDAbilitySystemComponent())
		{
			const float Max = ASC->GetMaxHealth();
			View.HealthFraction = Max > 0.0f ? FMath::Clamp(ASC->GetHealth() / Max, 0.0f, 1.0f) : 0.0f;
			View.bAlive = !ASC->IsDead();
		}
		Result.Add(View);
	}
	return Result;
}

FMDPlayerRunStats UMDGameMenuWidget::GetLocalRunStats() const
{
	const AMDPlayerState* PS = Cast<AMDPlayerState>(GetOwningPlayerState());
	return PS ? PS->RunStats : FMDPlayerRunStats();
}
