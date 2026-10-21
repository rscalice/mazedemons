#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "MDRunStats.h"
#include "MDUITypes.h"
#include "MDUIWidgets.h"
#include "MDGameMenuWidget.generated.h"

class UMDPerkComponent;

// One line of the Run tab's party list. Built from replicated data only: the attribute set replicates Health / MaxHealth to
// everyone, so the fraction is real for every player. Deaths are not tracked anywhere yet (always 0).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPartyMemberView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	FString Name;

	// 0..1.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float HealthFraction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bIsLocal = false;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 Deaths = 0;
};

// Run tab header numbers.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDRunSummary
{
	GENERATED_BODY()

	// Zones finished / zones in a run (4).
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 ZonesCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 ZoneTotal = 4;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 SoulsSaved = 0;

	// Always 0 until the game state records a run start time.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float RunTimeSeconds = 0.0f;

	// The local player's gold and Upgrade Shards.
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 Gold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 Shards = 0;
};

// Native base of the tabbed game menu (W_MDGameMenu, menu-design.md). Owns the active tab, wrap-around switching, badges
// and the Run tab data. The Blueprint child is layout only (tab bar + WidgetSwitcher): react to OnActiveTabChanged by
// switching the WidgetSwitcher and highlighting the button. Opened and closed by AMDPlayerController (ToggleGameMenu /
// OpenGameMenuTab / CloseGameMenu).
UCLASS(Abstract)
class MAZEDEMONS_API UMDGameMenuWidget : public UMDUserWidget
{
	GENERATED_BODY()

public:
	// ---- Pure rules (unit tested, no world) ----

	// Number of real tabs (Inventory, Perks, Run).
	static constexpr int32 NumTabs = 3;

	// A tab that can be selected. None never is.
	static bool IsTabSelectable(EMDMenuTab Tab);

	// Next (Direction > 0) or previous (Direction < 0) available tab with wrap-around. From None, next is the first tab and
	// previous the last. Returns Current when Direction is 0.
	static EMDMenuTab StepTab(EMDMenuTab Current, int32 Direction);

	// Badge number for a tab: perk points for Perks (never negative), 0 for the others.
	static int32 ComputeTabBadge(EMDMenuTab Tab, int32 PerkPointsAvailable);

	// ---- Tabs ----
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|Menu")
	EMDMenuTab ActiveTab = EMDMenuTab::Inventory;

	// Ignored for None / unavailable tabs. Fires OnActiveTabChanged when the tab really changes.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Menu")
	void SetActiveTab(EMDMenuTab NewTab);

	// Re-fires OnActiveTabChanged(ActiveTab, ActiveTab) so the Blueprint resyncs the tab bar and content switcher
	// even when the tab did not change (menu reopened on the same tab, construct ordering).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Menu")
	void SyncActiveTab();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Menu")
	void NextTab();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Menu")
	void PrevTab();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	bool IsTabAvailable(EMDMenuTab Tab) const;

	// Perks: unspent perk points of the local player. Others: 0. Show a dot / number when > 0.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	int32 GetTabBadge(EMDMenuTab Tab) const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	FText GetTabLabel(EMDMenuTab Tab) const;

	// Tab icons, set in the widget Blueprint.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Menu")
	TMap<EMDMenuTab, TSoftObjectPtr<UTexture2D>> TabIcons;

	// Icon for a tab, null when unset or not loadable.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	UTexture2D* GetTabIcon(EMDMenuTab Tab) const;

	// Asks the owning player controller to close the menu (Esc / Tab / B are handled natively; this is for a close button).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Menu")
	void CloseMenu();

	// ---- Run tab data (stub from existing replicated state) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	void GetRunSummary(FMDRunSummary& Out) const;

	// One entry per player in the game state's PlayerArray (local player flagged).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	TArray<FMDPartyMemberView> GetPartyMembers() const;

	// The local player's detailed stats (owner-only replicated on the PlayerState). Zeroes before the PlayerState exists.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Menu")
	FMDPlayerRunStats GetLocalRunStats() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// The active tab changed (not fired for a no-op). Switch the WidgetSwitcher and highlight the tab button.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Menu")
	void OnActiveTabChanged(EMDMenuTab NewTab, EMDMenuTab OldTab);

	// A badge value changed (perk points; also fired once when the widget binds). Re-read GetTabBadge.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Menu")
	void OnBadgesChanged();

private:
	void TryBind();
	void Unbind();

	UFUNCTION()
	void HandlePerksChanged();

	TWeakObjectPtr<UMDPerkComponent> PerkComponent;
};
