#include "MDEquipmentComponent.h"

#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDEquipment, Log, All);

namespace
{
	FString StripGenVariable(const FString& Name)
	{
		FString Result = Name;
		Result.RemoveFromEnd(TEXT("_GEN_VARIABLE"));
		return Result;
	}
}

UMDEquipmentComponent::UMDEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMDEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMDEquipmentComponent, ArmorPieces);
}

void UMDEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (bForceServerBoneRefresh && Owner && Owner->HasAuthority())
	{
		// By default, meshes that are not rendered (e.g. other players off-screen for a listen-server host or
		// a dedicated server) skip pose evaluation. That freezes montage notifies and weapon socket positions,
		// so melee traces would break. Force pose tick and bone refresh on the server.
		TArray<USkeletalMeshComponent*> MeshComponents;
		Owner->GetComponents<USkeletalMeshComponent>(MeshComponents);
		for (USkeletalMeshComponent* MeshComp : MeshComponents)
		{
			if (MeshComp)
			{
				MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
		}
	}

	// Late joiners / pawns that received the array before BeginPlay.
	if (ArmorPieces.Num() > 0)
	{
		ApplyArmorVisuals();
	}
}

void UMDEquipmentComponent::SetArmorPiece(FName Slot, USkeletalMesh* Mesh, float DamageReductionPercent)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	FMDArmorPiece* Existing = ArmorPieces.FindByPredicate([Slot](const FMDArmorPiece& Piece) { return Piece.Slot == Slot; });
	if (Existing)
	{
		Existing->Mesh = Mesh;
	}
	else
	{
		FMDArmorPiece NewPiece;
		NewPiece.Slot = Slot;
		NewPiece.Mesh = Mesh;
		ArmorPieces.Add(NewPiece);
	}

	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Owner))
	{
		ASC->SetArmorPieceDamageReduction(Slot, DamageReductionPercent);
	}

	// Server does not receive OnRep, so apply locally. Plain (non push-model) replication
	// detects the array change automatically; no explicit dirty marking is required.
	ApplyArmorVisuals();
	OnArmorChanged.Broadcast(Slot, Mesh);
}

void UMDEquipmentComponent::ClearArmorPiece(FName Slot)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const int32 Removed = ArmorPieces.RemoveAll([Slot](const FMDArmorPiece& Piece) { return Piece.Slot == Slot; });
	if (Removed == 0)
	{
		return;
	}

	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Owner))
	{
		ASC->RemoveArmorPieceDamageReduction(Slot);
	}

	if (USkeletalMeshComponent* MeshComp = FindMeshComponentForSlot(Slot))
	{
		MeshComp->SetSkeletalMeshAsset(nullptr);
	}

	ApplyArmorVisuals();
	OnArmorChanged.Broadcast(Slot, nullptr);
}

void UMDEquipmentComponent::OnRep_ArmorPieces(const TArray<FMDArmorPiece>& OldPieces)
{
	TArray<FName> RemovedSlots;
	for (const FMDArmorPiece& Old : OldPieces)
	{
		const bool bStillPresent = ArmorPieces.ContainsByPredicate([&Old](const FMDArmorPiece& Piece) { return Piece.Slot == Old.Slot; });
		if (!bStillPresent)
		{
			RemovedSlots.Add(Old.Slot);
			if (USkeletalMeshComponent* MeshComp = FindMeshComponentForSlot(Old.Slot))
			{
				MeshComp->SetSkeletalMeshAsset(nullptr);
			}
		}
	}

	ApplyArmorVisuals();

	for (const FName& Slot : RemovedSlots)
	{
		OnArmorChanged.Broadcast(Slot, nullptr);
	}
	for (const FMDArmorPiece& Piece : ArmorPieces)
	{
		OnArmorChanged.Broadcast(Piece.Slot, Piece.Mesh);
	}
}

void UMDEquipmentComponent::ApplyArmorVisuals()
{
	for (const FMDArmorPiece& Piece : ArmorPieces)
	{
		if (USkeletalMeshComponent* MeshComp = FindMeshComponentForSlot(Piece.Slot))
		{
			MeshComp->SetSkeletalMeshAsset(Piece.Mesh);
		}
		else
		{
			UE_LOG(LogMDEquipment, Warning, TEXT("No mesh component mapped for armor slot '%s' on %s"),
				*Piece.Slot.ToString(), *GetNameSafe(GetOwner()));
		}
	}
}

USkeletalMeshComponent* UMDEquipmentComponent::FindMeshComponentForSlot(FName Slot) const
{
	const AActor* Owner = GetOwner();
	const FName* TargetName = SlotToMeshComponent.Find(Slot);
	if (!Owner || !TargetName)
	{
		return nullptr;
	}

	const FString Target = StripGenVariable(TargetName->ToString());

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComp : MeshComponents)
	{
		if (!MeshComp)
		{
			continue;
		}
		if (MeshComp->GetFName() == *TargetName || StripGenVariable(MeshComp->GetName()) == Target)
		{
			return MeshComp;
		}
	}
	return nullptr;
}

void UMDEquipmentComponent::NotifyPickup(FName ItemRowName)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(Owner);
	if (Pawn && Pawn->IsLocallyControlled())
	{
		OnPickupNotify.Broadcast(ItemRowName);
	}
	else
	{
		ClientPickupNotify(ItemRowName);
	}
}

void UMDEquipmentComponent::ClientPickupNotify_Implementation(FName ItemRowName)
{
	OnPickupNotify.Broadcast(ItemRowName);
}
