// R3 + R7: the "Ammo" tab of the station panel. The content of the fourth tab of W_MDStationPanel (placed there as W_MDStationAmmoPane, a
// child Blueprint that only holds this class): one row per ammo type with the bag / cap count and a Buy button (a bundle of 10, never past
// one full stack). Every rule and price lives in UMDInventoryComponent::GetBuyAmmoReject / RequestBuyAmmo (server validated); the pane only
// shows the verdict. It reads the open station from the local player controller, so the Blueprint has nothing to hand over.
//
// The tab bookkeeping (which tab is next on LB / RB, which one a station opens on) is native too, as pure statics, so the Blueprint graphs
// only forward the four "service enabled" flags. Gamepad: D-pad Up / Down move the row focus (MoveFocus), A buys it (ConfirmSelected).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDStationAmmoPanel.generated.h"

class AMDStation;
class UMDButton;
class UMDText;
class UWidget;

USTRUCT()
struct FMDAmmoRowUI
{
	GENERATED_BODY()

	FName AmmoRow = NAME_None;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> Label;

	UPROPERTY(Transient)
	TObjectPtr<UMDButton> Button;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> ButtonLabel;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> Reason;
};

UCLASS()
class MAZEDEMONS_API UMDStationAmmoPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	// Tab numbers of W_MDStationPanel (StationState.CurrentTab).
	static constexpr int32 RepairTab = 0;
	static constexpr int32 UpgradeTab = 1;
	static constexpr int32 RespecTab = 2;
	static constexpr int32 AmmoTab = 3;
	static constexpr int32 NumTabs = 4;

	virtual bool Initialize() override;

	// Tests: use this station instead of the controller's open one.
	void SetStation(AMDStation* InStation);
	AMDStation* GetStation() const;

	// Re-reads the bag and the verdicts. Runs a few times a second; callable directly (tests).
	void Refresh();

	// The ammo rows this panel sells, in display order.
	static TArray<FName> GetSoldRows();

	// Button handler: asks the server to buy a bundle of the row.
	void BuyRow(FName AmmoRow);

	int32 GetRowCount() const { return Rows.Num(); }
	int32 GetSelectedRow() const { return SelectedRow; }
	bool IsRowBuyEnabled(int32 Index) const;

	// ---- Tab bookkeeping (pure) ----
	// The tab LB (Delta -1) / RB (+1) lands on: steps over tabs whose service is off, wraps at the ends (Repair <-> Ammo), stays put when
	// no other tab is enabled.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	static int32 NextEnabledTab(int32 Current, int32 Delta, bool bRepair, bool bUpgrade, bool bRespec, bool bAmmo);

	// The tab a station opens on: the first offered of Repair, Upgrade, Respec, Ammo (Repair when nothing is offered).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Station")
	static int32 PickDefaultTab(bool bRepair, bool bUpgrade, bool bRespec, bool bAmmo);

	// ---- Called from W_MDStationPanel ----
	// RefreshTabs hook: shows this pane only on the Ammo tab, restyles the tab label like the other tabs, enables the tab button when
	// the open station sells ammo, and hides the item list (ListPanel) on the Ammo tab like it does on Respec. Call it last in RefreshTabs.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void UpdateTabState(int32 CurrentTab, UMDButton* TabButton, UMDText* TabLabel, UWidget* ListPanel);

	// D-pad Up / Down on the Ammo tab: moves the focused row (clamped) and gives its Buy button the focus ring.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void MoveFocus(int32 Delta);

	// A on the Ammo tab: buys a bundle of the focused row when its button is enabled.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Station")
	void ConfirmSelected();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void OnBuyLight();

	UFUNCTION()
	void OnBuyHeavy();

private:
	void FocusSelectedRow();

	TWeakObjectPtr<AMDStation> StationOverride;

	UPROPERTY(Transient)
	TArray<FMDAmmoRowUI> Rows;

	float RefreshTimer = 0.0f;
	int32 SelectedRow = 0;
	bool bWasActive = false;
};
