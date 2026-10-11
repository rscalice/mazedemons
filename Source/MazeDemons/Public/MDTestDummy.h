#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDTestDummy.generated.h"

class UMDAbilitySystemComponent;
class UStaticMeshComponent;

// Reskinnable combat test target. Swap the Body mesh in a Blueprint child to change its look.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDTestDummy : public AActor
{
	GENERATED_BODY()

public:
	AMDTestDummy();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable, Category = "Test Dummy")
	void ResetDummy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDAbilitySystemComponent> AbilitySystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy", meta = (ClampMin = "1"))
	float StartingMaxHealth = 100.0f;

	// Percent damage reduction, 0-90.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy", meta = (ClampMin = "0", ClampMax = "90"))
	float StartingDefense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy", meta = (ClampMin = "0"))
	float StartingAttackPower = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy", meta = (ClampMin = "0"))
	float RespawnDelay = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy")
	bool bShowDamageNumbers = true;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleHealthChanged(float OldValue, float NewValue);

	FTimerHandle ResetTimer;
};
