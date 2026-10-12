#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDEquipmentComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FMDArmorPiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|Equipment")
	FName Slot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|Equipment")
	TObjectPtr<USkeletalMesh> Mesh = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDArmorChanged, FName, Slot, USkeletalMesh*, Mesh);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDPickupNotify, FName, ItemRowName);

// Server-authoritative, replicated armor visuals plus GAS armor reduction, and owning-client pickup notifications.
UCLASS(ClassGroup = (MazeDemons), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDEquipmentComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server only. Upserts the piece, applies GAS damage reduction and updates visuals.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Equipment")
	void SetArmorPiece(FName Slot, USkeletalMesh* Mesh, float DamageReductionPercent);

	// Server only. Removes the piece, its damage reduction and its mesh.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Equipment")
	void ClearArmorPiece(FName Slot);

	// Server only. Notifies the owning client (or the local listen-server host) of a pickup.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Equipment")
	void NotifyPickup(FName ItemRowName);

	UPROPERTY(ReplicatedUsing = OnRep_ArmorPieces, VisibleInstanceOnly, BlueprintReadOnly, Category = "Maze Demons|Equipment")
	TArray<FMDArmorPiece> ArmorPieces;

	// Armor slot name -> name of the owner's skeletal mesh component that displays it (e.g. Helmet -> Torso).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Maze Demons|Equipment")
	TMap<FName, FName> SlotToMeshComponent;

	// See BeginPlay: keeps montage notifies and weapon sockets updating on the server for unseen players.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Maze Demons|Equipment")
	bool bForceServerBoneRefresh = true;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Equipment")
	FMDArmorChanged OnArmorChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Equipment")
	FMDPickupNotify OnPickupNotify;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_ArmorPieces(const TArray<FMDArmorPiece>& OldPieces);

	UFUNCTION(Client, Reliable)
	void ClientPickupNotify(FName ItemRowName);

private:
	void ApplyArmorVisuals();
	USkeletalMeshComponent* FindMeshComponentForSlot(FName Slot) const;
};
