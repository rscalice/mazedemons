#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDStatusTypes.h"
#include "MDStatusBarWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UMDStatusEffectComponent;
class UMDText;

// V1 WP6: the row of status chips ("BLEED x2 3.4") on the player HUD, built entirely in C++ (no Blueprint asset). One chip per status type,
// tinted with the status' damage color; a chip shows only while its status is active. It reads the local player's replicated
// UMDStatusEffectComponent by itself (PlayerState), so a late joiner or a respawn needs no rebinding. UMDHUDWidget creates one and adds it
// to the viewport. The same widget can sit above an enemy health bar (WP8): call SetSource with that enemy's status component.
UCLASS()
class MAZEDEMONS_API UMDStatusBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	// Use this component instead of the owning player's (enemy bars, tests).
	void SetSource(UMDStatusEffectComponent* InSource);
	UMDStatusEffectComponent* GetSource() const;

	// Restyles the chips from the source's active statuses. Runs every tick; callable directly (tests).
	void Refresh();

	// How many chips show right now, and the text of one status' chip ("" when hidden).
	int32 GetVisibleChipCount() const;
	FText GetChipText(EMDStatusType Type) const;
	bool IsChipVisible(EMDStatusType Type) const;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	int32 IndexOf(EMDStatusType Type) const { return ChipTypes.IndexOfByKey(Type); }

	TWeakObjectPtr<UMDStatusEffectComponent> SourceOverride;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> Row;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> Frames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMDText>> Labels;

	TArray<EMDStatusType> ChipTypes; // parallel to Frames / Labels
};
