#pragma once

#include "CoreMinimal.h"
#include "MDUITypes.generated.h"

// Design tokens for the MazeDemons UI. Values live in UMDUIStyle (see assets/MazeDemons/UI_STYLE_GUIDE.md).

UENUM(BlueprintType)
enum class EMDColor : uint8
{
	Bg0,
	Bg1,
	Bg2,
	StrokeSubtle,
	StrokeStrong,
	TextPrimary,
	TextSecondary,
	TextDisabled,
	Accent,
	DisabledFill,
	Health,
	HealthLag,
	Stamina,
	StaminaExhausted,
	Gold,
	Upgrade,
	Warning,
	Danger,
	Success,
	RarityCommon,
	RarityUncommon,
	RarityRare,
	RarityEpic,
	RarityLegendary,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMDText : uint8
{
	Display,
	H1,
	H2,
	Body,
	Caption,
	Numeric,
	KeyHint,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMDButton : uint8
{
	Primary,
	Secondary,
	Danger,
	Tab,
	Slot,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMDRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary
};

USTRUCT(BlueprintType)
struct FMDTextDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	int32 Size = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	bool bBold = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	bool bUppercase = false;

	// Use the display font (Barlow Condensed) instead of the body font when one is assigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	bool bDisplayFont = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	int32 Tracking = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	EMDColor Color = EMDColor::TextPrimary;
};

// Durations in seconds. Snappy: ease-out only (see style guide); nothing here is allowed to bounce.
USTRUCT(BlueprintType)
struct FMDMotion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float HoverTint = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float Press = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float PanelOpen = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float PanelClose = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float PanelSlidePx = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float TabUnderline = 0.10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float BarLagHold = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float BarLagDrain = 0.30f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float HealFlash = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float ToastIn = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float ToastHold = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float ToastOut = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float GoldPop = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float GoldFade = 0.40f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float DeathFade = 0.20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") float LowHealthPulseHz = 1.0f;
};

// Tabs of the in-game menu (W_MDGameMenu). None is "no selection" and is never a valid active tab.
UENUM(BlueprintType)
enum class EMDMenuTab : uint8
{
	None = 0,
	Inventory,
	Perks,
	Run
};
