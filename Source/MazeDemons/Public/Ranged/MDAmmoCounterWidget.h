// R3: the minimal ammo counter, "mag / reserve", bottom right above the quick slots. Only exists in ranged mode (the ranged component
// creates it and removes it, like the reticle). The mag number turns red at 20% or less; at 0 a RELOAD prompt pulses; a thin bar
// shows reload progress. Built natively from the MD style widgets (no widget Blueprint, so W_MDHUD stays untouched).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDAmmoCounterWidget.generated.h"

class UMDRangedComponent;
class UMDText;
class UMDResourceBar;

UCLASS()
class MAZEDEMONS_API UMDAmmoCounterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	void SetRanged(UMDRangedComponent* InRanged) { Ranged = InRanged; }

	// Pulls the numbers from the ranged component and restyles the texts. Runs every tick; callable directly (tests).
	void Refresh();

	// The strings and flags the last Refresh produced (read by tests).
	FText GetMagText() const;
	FText GetReserveText() const;
	bool IsMagRed() const { return bMagRed; }
	bool IsReloadPromptVisible() const { return bReloadPrompt; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TWeakObjectPtr<UMDRangedComponent> Ranged;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> MagText;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> ReserveText;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> PromptText;

	UPROPERTY(Transient)
	TObjectPtr<UMDResourceBar> ReloadBar;

	bool bMagRed = false;
	bool bReloadPrompt = false;
};
