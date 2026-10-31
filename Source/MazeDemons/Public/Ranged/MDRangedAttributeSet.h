// R5: the ranged resources of a player, on the PlayerState ASC next to UMDAttributeSet. Heat builds while a heat beam (Duskray) fires and
// vents while the trigger is up; Charge drains while a staff beam fires and regenerates (V2 Ember Staff, rules and data are ready).
// Owner-only replication: other players only need the overheat flag (UMDBeamComponent), not the number. The server writes the values
// (UMDBeamComponent), the owning client predicts them locally and corrects to the replicated value.
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "MDRangedAttributeSet.generated.h"

UCLASS(BlueprintType)
class MAZEDEMONS_API UMDRangedAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMDRangedAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Ranged", ReplicatedUsing = OnRep_Heat)
	FGameplayAttributeData Heat;
	ATTRIBUTE_ACCESSORS_BASIC(UMDRangedAttributeSet, Heat)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Ranged", ReplicatedUsing = OnRep_MaxHeat)
	FGameplayAttributeData MaxHeat;
	ATTRIBUTE_ACCESSORS_BASIC(UMDRangedAttributeSet, MaxHeat)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Ranged", ReplicatedUsing = OnRep_Charge)
	FGameplayAttributeData Charge;
	ATTRIBUTE_ACCESSORS_BASIC(UMDRangedAttributeSet, Charge)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Ranged", ReplicatedUsing = OnRep_MaxCharge)
	FGameplayAttributeData MaxCharge;
	ATTRIBUTE_ACCESSORS_BASIC(UMDRangedAttributeSet, MaxCharge)

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	UFUNCTION()
	void OnRep_Heat(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHeat(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Charge(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxCharge(const FGameplayAttributeData& OldValue);
};
