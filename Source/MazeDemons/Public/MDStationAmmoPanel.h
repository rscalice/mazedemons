// R3: the "Ammo" section of the station UI. A small native panel that sits beside W_MDStationPanel while the open station offers the
// Ammo service (EMDStationService::Ammo): one row per ammo type with the bag / cap count and a Buy button (a bundle of 10, never past
// one full stack). Every rule and price lives in UMDInventoryComponent::GetBuyAmmoReject / RequestBuyAmmo (server validated); the
// panel only shows the verdict. Created and removed by AMDPlayerController with the station open / close.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDStationAmmoPanel.generated.h"

class AMDStation;
class UMDButton;
class UMDText;

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
	virtual bool Initialize() override;

	void SetStation(AMDStation* InStation) { Station = InStation; }

	// Re-reads the bag and the verdicts. Runs a few times a second; callable directly (tests).
	void Refresh();

	// The ammo rows this panel sells, in display order.
	static TArray<FName> GetSoldRows();

	// Button handler: asks the server to buy a bundle of the row.
	void BuyRow(FName AmmoRow);

	int32 GetRowCount() const { return Rows.Num(); }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void OnBuyLight();

	UFUNCTION()
	void OnBuyHeavy();

private:
	TWeakObjectPtr<AMDStation> Station;

	UPROPERTY(Transient)
	TArray<FMDAmmoRowUI> Rows;

	float RefreshTimer = 0.0f;
};
