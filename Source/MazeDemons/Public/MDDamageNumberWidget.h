#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MDDamageNumberWidget.generated.h"

class APlayerController;
class UTextBlock;

// A floating damage number, projected from a world location to the screen. Pooled by UMDCombatFeedbackComponent (reused, never
// destroyed). The text block is created in code, so no widget Blueprint is needed. The color comes from the hit's damage type
// (MDDamageNumbers: Physical white, elemental colors); crits are red, larger and pop in. Defaults live in MDDamageNumbers.cpp; the
// override properties below let a Blueprint child of this class change them.
UCLASS()
class MAZEDEMONS_API UMDDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	// Shows Damage at WorldLocation and starts the rise / fade. Adds the widget to the viewport. DamageType is a Damage.Type.* tag
	// (empty = Physical).
	void Start(APlayerController* Controller, const FVector& WorldLocation, float Damage, bool bCrit, const FGameplayTag& DamageType = FGameplayTag());

	// The color the number currently shows (for tests and Blueprint hooks).
	FLinearColor GetShownColor() const { return ShownColor; }

	// True once the text block exists. It is built when the widget is created, before it is ever added to the viewport.
	bool HasText() const { return Text != nullptr; }

	bool IsActive() const { return bActive; }
	double GetStartTime() const { return StartTime; }
	void Stop();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number", meta = (ClampMin = "0.1"))
	float Lifetime = 0.9f;

	// Screen pixels the number rises over its lifetime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number")
	float RisePixels = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number")
	int32 NormalFontSize = 26;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number")
	int32 CritFontSize = 42;

	// Data override of the per-type colors (key = Damage.Type.* tag). A type that is not listed uses the MDDamageNumbers default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number|Colors")
	TMap<FGameplayTag, FLinearColor> TypeColorOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number|Colors")
	bool bOverrideCritColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number|Colors", meta = (EditCondition = "bOverrideCritColor"))
	FLinearColor CritColorOverride = FLinearColor(1.0f, 0.08f, 0.05f, 1.0f);

	// A crit starts this much bigger and settles to 1 over CritPunchSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number", meta = (ClampMin = "1"))
	float CritPunchScale = 1.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number", meta = (ClampMin = "0.01"))
	float CritPunchSeconds = 0.14f;

	// "12", never "0": damage that landed shows at least 1.
	static FString FormatDamage(float Damage);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// Creates the text block in the widget tree if it is not there yet.
	void EnsureText();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	TWeakObjectPtr<APlayerController> Owner;
	FVector Location = FVector::ZeroVector;
	FVector2D Drift = FVector2D::ZeroVector;
	double StartTime = 0.0;
	FLinearColor ShownColor = FLinearColor::White;
	bool bActive = false;
	bool bShownCrit = false;
};
