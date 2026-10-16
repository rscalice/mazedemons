#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MDUITypes.h"
#include "MDUIWidgets.generated.h"

// Native base widgets. Each applies the active UMDUIStyle in SynchronizeProperties, so the designer preview and the
// game agree and no Blueprint ever hard-codes a colour, font or corner radius.

UCLASS(meta = (DisplayName = "MD Text"))
class MAZEDEMONS_API UMDText : public UTextBlock
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMDText TextStyle = EMDText::Body;

	// Optional colour token that replaces the style's default colour for this text.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	bool bOverrideColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (EditCondition = "bOverrideColor"))
	EMDColor ColorToken = EMDColor::TextPrimary;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void SetTextStyle(EMDText NewStyle);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void SetColorToken(EMDColor NewToken);

	virtual void SynchronizeProperties() override;
};

UCLASS(meta = (DisplayName = "MD Button"))
class MAZEDEMONS_API UMDButton : public UButton
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMDButton ButtonKind = EMDButton::Secondary;

	virtual void SynchronizeProperties() override;
};

// Sharp-cornered container: Bg1 fill, 1 px StrokeSubtle.
UCLASS(meta = (DisplayName = "MD Panel"))
class MAZEDEMONS_API UMDPanel : public UBorder
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMDColor FillToken = EMDColor::Bg1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMDColor StrokeToken = EMDColor::StrokeSubtle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0", ClampMax = "4"))
	float StrokeWidth = 1.0f;

	virtual void SynchronizeProperties() override;
};

// Flat bar: Bg1 track with a 1 px stroke, square ends, fill from a colour token. Current value snaps (no tween).
UCLASS(meta = (DisplayName = "MD Resource Bar"))
class MAZEDEMONS_API UMDResourceBar : public UProgressBar
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMDColor FillToken = EMDColor::Health;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void SetFillToken(EMDColor NewToken);

	virtual void SynchronizeProperties() override;
};

// Base for every MD screen/panel. PlayOpen/PlayClose tween opacity and an 8 px slide with ease-out cubic, using the
// durations in the style's FMDMotion.
UCLASS(Abstract)
class MAZEDEMONS_API UMDUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void PlayOpen();

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void PlayClose();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	bool IsPlayingTransition() const { return Phase != EPhase::Idle; }

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDPanelClosed);
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|UI")
	FMDPanelClosed OnClosed;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class EPhase : uint8 { Idle, Opening, Closing };
	EPhase Phase = EPhase::Idle;
	float Elapsed = 0.0f;
};

// Base for one inventory/quick slot. Holds display data and the rarity colour; the Blueprint child lays it out.
UCLASS(Abstract)
class MAZEDEMONS_API UMDItemSlot : public UMDUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void SetItem(const FText& InName, int32 InCount, EMDRarity InRarity, UTexture2D* InIcon);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|UI")
	void ClearItem();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	FLinearColor GetRarityColor() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	bool IsEmpty() const { return Count <= 0; }

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FText ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	EMDRarity Rarity = EMDRarity::Common;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	TObjectPtr<UTexture2D> Icon;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|UI")
	void OnItemChanged();
};
