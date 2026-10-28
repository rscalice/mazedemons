// R7: the ranged ammo counter, placed in W_MDHUD beside the quick slots (W_MDAmmoCounter, a child Blueprint that only places it).
// Weapon icon, "mag / reserve" (mag turns red at 20% or less), a RELOAD / NO AMMO pulse, the reload progress bar and, for beam weapons,
// the heat / charge meter. It finds the local player's UMDRangedComponent (PlayerState) by itself every tick and reads replicated state
// only, so a late joiner or a respawn needs no rebinding. Hidden outside ranged mode: fades in on the swap, out on the swap back.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Ranged/MDRangedUIRules.h"
#include "MDAmmoCounterWidget.generated.h"

class UImage;
class UMDRangedComponent;
class UMDText;
class UMDResourceBar;
class USizeBox;
class UHorizontalBox;

UCLASS()
class MAZEDEMONS_API UMDAmmoCounterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	// Tests / special cases: use this component instead of the owning player's.
	void SetRanged(UMDRangedComponent* InRanged) { RangedOverride = InRanged; }

	// The component the counter reads: the override, else the owning player's.
	UMDRangedComponent* GetRanged() const;

	// Pulls the numbers from the ranged component and restyles the texts, prompt, bars and meter. Runs every tick; callable directly (tests).
	void Refresh();

	// Moves the fade toward its target (shown in ranged mode with a gun, hidden otherwise). Runs every tick; callable directly (tests).
	void TickFade(float DeltaSeconds);

	FText GetMagText() const;
	FText GetReserveText() const;
	bool IsMagRed() const { return bMagRed; }
	EMDAmmoPrompt GetPrompt() const { return Prompt; }
	bool IsReloadPromptVisible() const { return Prompt != EMDAmmoPrompt::None; }
	EMDHeatMeterState GetHeatState() const { return HeatState; }
	float GetHeatFraction() const { return HeatFraction; }
	bool IsHeatMeterVisible() const { return HeatState != EMDHeatMeterState::Hidden; }
	// True when the numbers / reload bar are shown (a gun that uses ammo); a beam weapon shows the meter instead.
	bool IsAmmoSectionVisible() const { return bAmmoSection; }
	float GetShownOpacity() const { return ShownOpacity; }
	float GetTargetOpacity() const { return TargetOpacity; }
	// Weapon row whose icon is shown (NAME_None without a gun).
	FName GetIconRow() const { return IconRow; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TWeakObjectPtr<UMDRangedComponent> RangedOverride;

	UPROPERTY(Transient)
	TObjectPtr<UImage> WeaponIcon;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> MagText;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> SlashText;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> ReserveText;

	UPROPERTY(Transient)
	TObjectPtr<UMDText> PromptText;

	UPROPERTY(Transient)
	TObjectPtr<UMDResourceBar> ReloadBar;

	UPROPERTY(Transient)
	TObjectPtr<UMDResourceBar> HeatBar;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> HeatBox;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> NumberRow;

	FName IconRow = NAME_None;
	bool bMagRed = false;
	bool bAmmoSection = true;
	EMDAmmoPrompt Prompt = EMDAmmoPrompt::None;
	EMDHeatMeterState HeatState = EMDHeatMeterState::Hidden;
	float HeatFraction = 0.0f;
	float ShownOpacity = 0.0f;
	float TargetOpacity = 0.0f;
};
