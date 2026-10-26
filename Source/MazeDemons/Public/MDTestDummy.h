#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "MDCombatTypes.h"
#include "MDTestDummy.generated.h"

class UMDAbilitySystemComponent;
class UMDAttributeSet;
class UMDPoiseComponent;
class UCapsuleComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

// One armor piece worn by the dummy. Slot is one of: Helmet, Chest, Legs, Boots.
USTRUCT(BlueprintType)
struct FMDDummyArmorSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FName Slot = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TObjectPtr<USkeletalMesh> Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor", meta = (ClampMin = "0", ClampMax = "90"))
	float DamageReductionPercent = 0.0f;
};

// Armorable humanoid combat test target (UE5 mannequin skeleton). Equip armor to test damage reduction.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDTestDummy : public AActor, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AMDTestDummy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	UFUNCTION(BlueprintCallable, Category = "Test Dummy")
	void ResetDummy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Armor")
	TObjectPtr<USkeletalMeshComponent> HelmetMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Armor")
	TObjectPtr<USkeletalMeshComponent> ChestMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Armor")
	TObjectPtr<USkeletalMeshComponent> LegsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Armor")
	TObjectPtr<USkeletalMeshComponent> BootsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDAttributeSet> Attributes;

	// C1: poise and hit reactions. Its Profile defaults to /Game/Data/Combat/DA_Poise_Dummy (set in the constructor).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDPoiseComponent> Poise;

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

	// Gold shared among the players in range when the dummy dies (0 = none). Server only; see UMDGameplayStatics::AwardKillGold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test Dummy", meta = (ClampMin = "0"))
	int32 KillGold = 0;

	// Slot: Helmet | Chest | Legs | Boots. Server only.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Test Dummy|Armor")
	void EquipArmorPiece(FName Slot, USkeletalMesh* Mesh, float DamageReductionPercent);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Test Dummy|Armor")
	void UnequipArmorPiece(FName Slot);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Test Dummy|Armor")
	void UnequipAllArmor();

	// Applied on the server in BeginPlay so placed dummies can be preset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dummy|Armor")
	TArray<FMDDummyArmorSlot> StartingArmor;

	UPROPERTY(ReplicatedUsing = OnRep_Armor, BlueprintReadOnly, Category = "Dummy|Armor")
	TArray<FMDDummyArmorSlot> EquippedArmor;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Armor();

private:
	USkeletalMeshComponent* GetArmorComponent(FName Slot) const;
	void ApplyArmorVisuals();

	UFUNCTION()
	void HandleHealthChanged(float OldValue, float NewValue);

	// The dummy body runs a single-node animation (no slot to play montages in): play the reaction montage's sequence instead.
	UFUNCTION()
	void HandleReaction(const FMDHitResultRep& Hit);

	void EndReactionPlayback();

	FTimerHandle ResetTimer;
	FTimerHandle ReactionTimer;
};
