#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDDamageNumberWidget.generated.h"

class APlayerController;
class UTextBlock;

// A floating damage number, projected from a world location to the screen. Pooled by UMDCombatFeedbackComponent (reused, never
// destroyed). The text block is created in code, so no widget Blueprint is needed. Crits are gold and larger.
UCLASS()
class MAZEDEMONS_API UMDDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows Damage at WorldLocation and starts the rise / fade. Adds the widget to the viewport.
	void Start(APlayerController* Controller, const FVector& WorldLocation, float Damage, bool bCrit);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number")
	FLinearColor NormalColor = FLinearColor(1.0f, 0.95f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Number")
	FLinearColor CritColor = FLinearColor(1.0f, 0.78f, 0.1f, 1.0f);

	// "12", never "0": damage that landed shows at least 1.
	static FString FormatDamage(float Damage);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	TWeakObjectPtr<APlayerController> Owner;
	FVector Location = FVector::ZeroVector;
	FVector2D Drift = FVector2D::ZeroVector;
	double StartTime = 0.0;
	bool bActive = false;
};
