#include "Ranged/MDRangedComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "MDComboComponent.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPoiseComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Ranged/MDRangedWeaponData.h"

UMDRangedComponent::UMDRangedComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMDRangedComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDRangedComponent, bRangedMode);
	DOREPLIFETIME(UMDRangedComponent, bAimed);
	DOREPLIFETIME_CONDITION(UMDRangedComponent, Mags, COND_OwnerOnly);
}

UMDRangedComponent* UMDRangedComponent::Find(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDRangedComponent* Comp = Actor->FindComponentByClass<UMDRangedComponent>())
	{
		return Comp;
	}
	const AController* Controller = Cast<AController>(Actor);
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		Controller = Pawn->GetController();
		PS = Pawn->GetPlayerState();
	}
	if (Controller && !PS)
	{
		PS = Controller->PlayerState;
	}
	return PS ? PS->FindComponentByClass<UMDRangedComponent>() : nullptr;
}

bool UMDRangedComponent::IsActorInRangedMode(const AActor* Actor)
{
	const UMDRangedComponent* Comp = Find(Actor);
	return Comp && Comp->IsRangedMode();
}

UMDInventoryComponent* UMDRangedComponent::GetInventory() const
{
	if (!BoundInventory.IsValid())
	{
		return UMDGameplayStatics::GetInventory(GetOwner());
	}
	return BoundInventory.Get();
}

void UMDRangedComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetOwner()))
	{
		BoundInventory = Inv;
		Inv->OnEquipmentChanged.AddUniqueDynamic(this, &UMDRangedComponent::HandleEquipmentChanged);
	}
	ApplyModeLocally();
}

void UMDRangedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMDInventoryComponent* Inv = BoundInventory.Get())
	{
		Inv->OnEquipmentChanged.RemoveDynamic(this, &UMDRangedComponent::HandleEquipmentChanged);
	}
	Super::EndPlay(EndPlayReason);
}

UMDRangedWeaponData* UMDRangedComponent::GetEquippedWeaponData() const
{
	const UMDInventoryComponent* Inv = GetInventory();
	return Inv ? Inv->GetEquippedRangedData() : nullptr;
}

EMDWeaponStance UMDRangedComponent::GetRangedStance() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	return Data ? Data->Stance : EMDWeaponStance::Unarmed;
}

EMDWeaponStance UMDRangedComponent::ResolveActiveStance(EMDWeaponStance MeleeStance) const
{
	return FMDRangedRules::ResolveStance(bRangedMode, MeleeStance, GetRangedStance());
}

void UMDRangedComponent::SetAimed(bool bNewAimed)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bAimed = bNewAimed && bRangedMode;
	}
}

bool UMDRangedComponent::TrySwapMode()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const APawn* Pawn = Cast<APlayerState>(GetOwner()) ? Cast<APlayerState>(GetOwner())->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const UMDComboComponent* Combo = UMDComboComponent::FindFor(Pawn);
	const bool bWantRanged = !bRangedMode;
	if (!FMDRangedRules::CanSwapMode(bWantRanged, HasGunEquipped(), UMDGameplayStatics::IsPawnDead(Pawn),
		Combo && Combo->IsAttacking(), UMDPoiseComponent::IsActorStunned(Pawn), Now - LastSwapTime, SwapCooldown))
	{
		return false;
	}
	LastSwapTime = Now;
	SetRangedMode(bWantRanged);
	return true;
}

void UMDRangedComponent::SetRangedMode(bool bNewMode)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bRangedMode == bNewMode)
	{
		return;
	}
	bRangedMode = bNewMode;
	if (!bRangedMode)
	{
		bAimed = false;
	}
	ApplyModeLocally();
	OnRangedModeChanged.Broadcast(bRangedMode);
}

void UMDRangedComponent::OnRep_RangedMode()
{
	ApplyModeLocally();
	OnRangedModeChanged.Broadcast(bRangedMode);
}

void UMDRangedComponent::ApplyModeLocally()
{
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetOwner()))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			const bool bHasTag = ASC->HasMatchingGameplayTag(TAG_State_RangedMode);
			if (bRangedMode && !bHasTag)
			{
				ASC->AddLooseGameplayTag(TAG_State_RangedMode);
			}
			else if (!bRangedMode && bHasTag)
			{
				ASC->RemoveLooseGameplayTag(TAG_State_RangedMode);
			}
		}
	}
	SetMeleeWeaponsHidden(bRangedMode);
	RefreshWeaponMesh();
}

void UMDRangedComponent::SetMeleeWeaponsHidden(bool bHidden)
{
	// Melee weapons are spawned by the BP equip hooks as actors attached to the pawn (Content/Blueprints/Items/Weapons).
	// There are no holster sockets on the mannequin yet, so ranged mode hides them (local visuals only, replays on every client).
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached);
	for (AActor* Child : Attached)
	{
		if (Child && Child->GetClass()->GetPathName().Contains(TEXT("/Items/Weapons/")))
		{
			Child->SetActorHiddenInGame(bHidden);
		}
	}
}

void UMDRangedComponent::HandleEquipmentChanged()
{
	// A freshly equipped melee weapon spawns visible (after this broadcast): re-hide it next frame while in ranged mode.
	SetMeleeWeaponsHidden(bRangedMode);
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<UMDRangedComponent>(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->SetMeleeWeaponsHidden(WeakThis->bRangedMode);
				WeakThis->RefreshWeaponMesh();
			}
		});
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UMDInventoryComponent* Inv = GetInventory();
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	const FName GunRow = Data && Inv ? Inv->GetEquippedRangedRow() : NAME_None;
	const bool bGunJustEquipped = !GunRow.IsNone() && GunRow != LastGunRow;
	LastGunRow = GunRow;

	if (FMDRangedRules::ShouldLeaveRangedMode(bRangedMode, Data != nullptr))
	{
		SetRangedMode(false);
	}
	else if (!bRangedMode && FMDRangedRules::ShouldEnterRangedModeOnEquip(Inv && Inv->HasMeleeWeaponEquipped(), bGunJustEquipped))
	{
		SetRangedMode(true);
	}

	// A new gun starts with a full magazine for now (R1 owns real ammo flow).
	if (Data && Inv)
	{
		if (GetMagRounds(MDItemType::RangedSlot) == 0 && bGunJustEquipped)
		{
			SetMagRounds(MDItemType::RangedSlot, Data->MagSize);
		}
	}
}

USkeletalMeshComponent* UMDRangedComponent::FindVisibleMesh(const APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	TArray<USkeletalMeshComponent*> Meshes;
	Pawn->GetComponents(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->GetAnimClass() && Mesh->GetAnimClass()->GetName().Contains(TEXT("GenericRetarget")))
		{
			return Mesh;
		}
	}
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	return Character ? Character->GetMesh() : nullptr;
}

void UMDRangedComponent::RefreshWeaponMesh()
{
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	UMDRangedWeaponData* Data = GetEquippedWeaponData();
	USkeletalMeshComponent* Visible = FindVisibleMesh(Pawn);
	const UMDInventoryComponent* Inv = GetInventory();
	const FName Row = Inv ? Inv->GetEquippedRangedRow() : NAME_None;

	const bool bWantMesh = Data && Visible;
	if (WeaponMeshComp && (!bWantMesh || WeaponMeshRow != Row || WeaponMeshComp->GetAttachParent() != Visible))
	{
		WeaponMeshComp->DestroyComponent();
		WeaponMeshComp = nullptr;
		WeaponMeshRow = NAME_None;
	}
	if (!bWantMesh)
	{
		return;
	}
	if (!WeaponMeshComp)
	{
		UObject* MeshAsset = Data->LoadWeaponMesh();
		UMeshComponent* NewComp = nullptr;
		if (UStaticMesh* SM = Cast<UStaticMesh>(MeshAsset))
		{
			UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(const_cast<APawn*>(Pawn), NAME_None, RF_Transient);
			SMC->SetStaticMesh(SM);
			NewComp = SMC;
		}
		else if (USkeletalMesh* SK = Cast<USkeletalMesh>(MeshAsset))
		{
			USkeletalMeshComponent* SKC = NewObject<USkeletalMeshComponent>(const_cast<APawn*>(Pawn), NAME_None, RF_Transient);
			SKC->SetSkeletalMesh(SK);
			NewComp = SKC;
		}
		if (!NewComp)
		{
			return; // mesh did not load (Fab pack missing): no visual, rules and IK-off still work
		}
		NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewComp->SetGenerateOverlapEvents(false);
		NewComp->RegisterComponent();
		const FName Socket = Visible->DoesSocketExist(Data->HandSocket) ? Data->HandSocket : FName(TEXT("hand_r"));
		NewComp->AttachToComponent(Visible, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		// Grip_R on the mesh pins the main hand: move the mesh so Grip_R lands on the hand socket; else use the data offset.
		FTransform Rel = Data->HandOffset;
		if (!Data->bHasRightGripOffset && NewComp->DoesSocketExist(Data->GripRSocket))
		{
			Rel = NewComp->GetSocketTransform(Data->GripRSocket, RTS_Component).Inverse();
		}
		NewComp->SetRelativeTransform(Rel);
		WeaponMeshComp = NewComp;
		WeaponMeshRow = Row;
	}
	WeaponMeshComp->SetVisibility(bRangedMode, true);
}

bool UMDRangedComponent::GetSupportGripWorldTransform(FTransform& OutTransform) const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	if (!WeaponMeshComp || !Data)
	{
		return false;
	}
	if (!Data->bHasLeftGripOffset && WeaponMeshComp->DoesSocketExist(Data->GripLSocket))
	{
		OutTransform = WeaponMeshComp->GetSocketTransform(Data->GripLSocket, RTS_World);
		return true;
	}
	if (Data->bHasLeftGripOffset)
	{
		OutTransform = Data->SupportHandOffset * WeaponMeshComp->GetComponentTransform();
		return true;
	}
	return false;
}

float UMDRangedComponent::GetSupportHandAlpha(float DisableCurve) const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	return FMDRangedRules::SupportHandIKAlpha(Data ? Data->GripType : EMDGripType::OneHanded, bAimed, DisableCurve, bRangedMode,
		Data != nullptr && WeaponMeshComp != nullptr);
}

int32 UMDRangedComponent::GetMagRounds(uint8 Slot) const
{
	const FMDSlotMag* Mag = Mags.FindByPredicate([Slot](const FMDSlotMag& M) { return M.Slot == Slot; });
	return Mag ? Mag->Rounds : 0;
}

void UMDRangedComponent::SetMagRounds(uint8 Slot, int32 Rounds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Rounds = FMath::Max(0, Rounds);
	if (FMDSlotMag* Mag = Mags.FindByPredicate([Slot](const FMDSlotMag& M) { return M.Slot == Slot; }))
	{
		Mag->Rounds = Rounds;
		return;
	}
	FMDSlotMag NewMag;
	NewMag.Slot = Slot;
	NewMag.Rounds = Rounds;
	Mags.Add(NewMag);
}
