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
#include "LockOn/MDLockOnComponent.h"
#include "MDPoiseComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Ranged/MDRangedWeaponData.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatFeedbackComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Ranged/MDBeamComponent.h"
#include "Ranged/MDProjectile.h"
#include "Ranged/MDRangedFX.h"
#include "MDUISettings.h"
#include "Ranged/MDReloadAbility.h"
#include "Ranged/MDRangedFireAbility.h"
#include "Ranged/MDAimAbility.h"
#include "Ranged/MDAimRules.h"
#include "MDStaminaComponent.h"
#include "MDUISettings.h"
#include "Ranged/MDRangedReticleWidget.h"
#include "Ranged/MDRangedStatics.h"

namespace MDRangedComponentLocal
{
	TAutoConsoleVariable<int32> CVarAutoReload(TEXT("md.Ranged.AutoReload"), 1,
		TEXT("R3: 1 = reload automatically when the magazine runs empty (and when the trigger is pulled on an empty one); 0 = manual reload only."));

	TAutoConsoleVariable<float> CVarReloadScale(TEXT("md.Ranged.ReloadScale"), 1.0f, TEXT("Tuning: multiplier on every gun's reload time."));

	TAutoConsoleVariable<float> CVarDamageScale(TEXT("md.Ranged.DamageScale"), 1.0f, TEXT("Tuning: multiplier on every gun's damage per shot."));

	TAutoConsoleVariable<int32> CVarDrawShots(TEXT("md.Ranged.DrawShots"), 0, TEXT("1 = draw every shot request on the server (green accepted, red rejected)."));

	constexpr float SemiBufferSeconds = 0.15f;
	constexpr float AimTraceRange = 6000.0f;
	constexpr float MovingSpeed = 50.0f;
	constexpr double AimPredictionTimeout = 0.75; // seconds the owner trusts its own aim guess without the server confirming it
}

UMDRangedComponent::UMDRangedComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // switched on while the owner holds fire or the recoil / kick is settling
	SetIsReplicatedByDefault(true);
}

void UMDRangedComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDRangedComponent, bRangedMode);
	DOREPLIFETIME(UMDRangedComponent, bAimed);
	DOREPLIFETIME_CONDITION(UMDRangedComponent, Mags, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDRangedComponent, ShotAck, COND_OwnerOnly);
	DOREPLIFETIME(UMDRangedComponent, bReloading);
	DOREPLIFETIME(UMDRangedComponent, ReloadStartServerTime);
	DOREPLIFETIME(UMDRangedComponent, ReloadDuration);
	DOREPLIFETIME(UMDRangedComponent, bAutoReloadPref);
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
	// R1: the LocalPredicted fire ability lives on the player's ASC (PlayerState). Granted once by the server; it replicates to the owner.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
		{
			if (!ASC->FindAbilitySpecFromClass(UMDRangedFireAbility::StaticClass()))
			{
				ASC->GrantAbility(UMDRangedFireAbility::StaticClass());
			}
			if (!ASC->FindAbilitySpecFromClass(UMDReloadAbility::StaticClass()))
			{
				ASC->GrantAbility(UMDReloadAbility::StaticClass());
			}
			if (!ASC->FindAbilitySpecFromClass(UMDAimAbility::StaticClass()))
			{
				ASC->GrantAbility(UMDAimAbility::StaticClass());
			}
		}
	}
	ApplyModeLocally();
}

void UMDRangedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Reticle)
	{
		Reticle->RemoveFromParent();
		Reticle = nullptr;
	}
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimer);
	}
	if (UMDInventoryComponent* Inv = BoundInventory.Get())
	{
		Inv->OnEquipmentChanged.RemoveDynamic(this, &UMDRangedComponent::HandleEquipmentChanged);
	}
	Super::EndPlay(EndPlayReason);
}

UMDRangedWeaponData* UMDRangedComponent::GetEquippedWeaponData() const
{
	if (TestData)
	{
		return TestData;
	}
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
		const bool bNew = bNewAimed && bRangedMode;
		if (bAimed != bNew)
		{
			bAimed = bNew;
			ApplyAimTagLocally();
		}
		if (bAimed)
		{
			SetComponentTickEnabled(true); // the tick watches the sprint / stagger / death rules while aimed
		}
	}
}

void UMDRangedComponent::OnRep_Aimed()
{
	if (!bAimed)
	{
		bAimPredicted = false; // the server's answer wins over the owner's guess
	}
	ApplyAimTagLocally();
}

void UMDRangedComponent::ApplyAimTagLocally()
{
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		const bool bHasTag = ASC->HasMatchingGameplayTag(TAG_State_Aiming);
		if (bAimed && !bHasTag)
		{
			ASC->AddLooseGameplayTag(TAG_State_Aiming);
		}
		else if (!bAimed && bHasTag)
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Aiming);
		}
	}
}

bool UMDRangedComponent::IsPawnSprintingNow() const
{
	const UMDStaminaComponent* Stamina = GetOwner() ? GetOwner()->FindComponentByClass<UMDStaminaComponent>() : nullptr;
	return Stamina && Stamina->IsSprintingNow();
}

bool UMDRangedComponent::CanAimNow(bool bSprinting) const
{
	const APawn* Pawn = GetPawn();
	return FMDAimRules::CanAim(bRangedMode, HasGunEquipped(), !Pawn || UMDGameplayStatics::IsPawnDead(Pawn),
		Pawn && UMDPoiseComponent::IsActorStunned(Pawn), bSprinting);
}

void UMDRangedComponent::SetAimInput(bool bPressed, bool bToggle)
{
	if (!IsOwnerLocal())
	{
		return;
	}
	bAimToggleMode = bToggle;
	if (bToggle)
	{
		if (!bPressed)
		{
			return; // toggle mode reacts to presses only
		}
		bAimWanted = !bAimWanted;
	}
	else
	{
		bAimWanted = bPressed;
	}
	if (bAimWanted && bRangedMode)
	{
		TryActivateAim();
	}
	else
	{
		RequestAim(false);
	}
}

bool UMDRangedComponent::TryActivateAim()
{
	if (!IsOwnerLocal() || !bRangedMode)
	{
		return false;
	}
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		if (ASC->FindAbilitySpecFromClass(UMDAimAbility::StaticClass()))
		{
			if (ASC->TryActivateAbilityByClass(UMDAimAbility::StaticClass()))
			{
				return true;
			}
			SetComponentTickEnabled(true); // refused (stagger, ...): the tick retries while the wish stands
			return false;
		}
	}
	RequestAim(true);
	return true;
}

void UMDRangedComponent::RequestAim(bool bAim)
{
	if (!GetOwner())
	{
		return;
	}
	const bool bAllowed = bAim && CanAimNow(IsPawnSprintingNow());
	if (GetOwner()->HasAuthority())
	{
		SetAimed(bAllowed);
	}
	else
	{
		bAimPredicted = bAllowed;
		if (bAllowed)
		{
			AimPredictedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		}
		Server_SetAim(bAim); // the server re-checks with its own view
	}
	if (bAim || bAimWanted)
	{
		SetComponentTickEnabled(true);
	}
}

void UMDRangedComponent::Server_SetAim_Implementation(bool bAim)
{
	ProcessAimRequest(bAim);
}

bool UMDRangedComponent::ProcessAimRequest(bool bAim)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return bAimed;
	}
	SetAimed(bAim && CanAimNow(IsPawnSprintingNow()));
	return bAimed;
}

void UMDRangedComponent::EvaluateAimRules(bool bSprinting)
{
	const bool bCan = CanAimNow(bSprinting);
	if (GetOwner() && GetOwner()->HasAuthority() && bAimed && !bCan)
	{
		SetAimed(false);
	}
	if (!IsOwnerLocal())
	{
		return;
	}
	bAimWanted = FMDAimRules::WantedAfterRules(bAimWanted, bAimToggleMode, bCan, bRangedMode);
	if (bAimPredicted && !bAimed && GetWorld() && GetWorld()->GetTimeSeconds() - AimPredictedAt > MDRangedComponentLocal::AimPredictionTimeout)
	{
		bAimPredicted = false; // the server never confirmed (it refused): fall back to the real flag; a held button asks again below
	}
	if (bAimPredicted && !bCan)
	{
		bAimPredicted = false;
		if (!GetOwner()->HasAuthority())
		{
			Server_SetAim(false);
		}
	}
	else if (bAimWanted && bCan && !IsAimingLocal())
	{
		RequestAim(true); // the sprint ended / the stun wore off while the button is still down
	}
}

float UMDRangedComponent::GetSecondsSinceShot() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - LastShotSeenTime) : 1000.0f;
}

FVector2D UMDRangedComponent::GetAimOffset() const
{
	const APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return FVector2D::ZeroVector;
	}
	// GetBaseAimRotation: the controller's rotation on the owner and the server, the replicated view pitch + actor yaw elsewhere.
	return FMDAimRules::ComputeAimOffset(Pawn->GetBaseAimRotation(), Pawn->GetActorRotation());
}

float UMDRangedComponent::GetAimOffsetAlpha() const
{
	return FMDAimRules::AimOffsetAlpha(IsAimingLocal(), bRangedMode, GetSecondsSinceShot());
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
		CancelReload(); // holstering the gun aborts a reload; nothing was moved yet
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
	UpdateReticle();
	if (!bRangedMode)
	{
		bFireHeld = false;
		bSemiPending = false;
		bAimWanted = false;
		bAimPredicted = false;
	}
	ApplyAimTagLocally();
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
	const FName PreviousGunRow = LastGunRow;
	LastGunRow = GunRow;
	if (GunRow != PreviousGunRow)
	{
		CancelReload(); // a different gun in the hand: the old reload means nothing
	}

	if (FMDRangedRules::ShouldLeaveRangedMode(bRangedMode, Data != nullptr))
	{
		SetRangedMode(false);
	}
	else if (!bRangedMode && FMDRangedRules::ShouldEnterRangedModeOnEquip(Inv && Inv->HasMeleeWeaponEquipped(), bGunJustEquipped))
	{
		SetRangedMode(true);
	}

	// R3: the magazine belongs to the gun. Swapping away remembers the rounds; swapping back restores them. A gun this player has
	// never held starts with a full magazine, once.
	if (GunRow != PreviousGunRow)
	{
		if (!PreviousGunRow.IsNone())
		{
			StashedMags.Add(PreviousGunRow, GetMagRounds(MDItemType::RangedSlot));
		}
		if (Data && !GunRow.IsNone())
		{
			const int32* Stashed = StashedMags.Find(GunRow);
			SetMagRounds(MDItemType::RangedSlot, Stashed ? FMath::Min(*Stashed, Data->MagSize) : Data->MagSize);
		}
		else
		{
			SetMagRounds(MDItemType::RangedSlot, 0);
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
#if !UE_BUILD_SHIPPING
		DebugSupportOffset.Reset();
#endif
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
		WeaponBaseRelative = Rel;
		bKickApplied = false;
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
#if !UE_BUILD_SHIPPING
	if (DebugSupportOffset.IsSet())
	{
		OutTransform = DebugSupportOffset.GetValue() * WeaponMeshComp->GetComponentTransform();
		return true;
	}
#endif
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

#if !UE_BUILD_SHIPPING
void UMDRangedComponent::DebugSetHandOffset(const FTransform& HandOffset, FName Socket)
{
	if (WeaponMeshComp)
	{
		if (!Socket.IsNone())
		{
			WeaponMeshComp->AttachToComponent(WeaponMeshComp->GetAttachParent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		}
		WeaponMeshComp->SetRelativeTransform(HandOffset);
		WeaponBaseRelative = HandOffset;
	}
}

void UMDRangedComponent::DebugSetSupportOffset(const FTransform& SupportOffset)
{
	DebugSupportOffset = SupportOffset;
}
#endif

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

// =====================================================================================================================
// R1 fire pipeline
// =====================================================================================================================

APawn* UMDRangedComponent::GetPawn() const
{
	if (TestPawn.IsValid())
	{
		return TestPawn.Get();
	}
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	return PS ? PS->GetPawn() : nullptr;
}

APlayerController* UMDRangedComponent::GetOwnerController() const
{
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	return PS ? PS->GetPlayerController() : nullptr;
}

bool UMDRangedComponent::IsOwnerLocal() const
{
	const APlayerController* PC = GetOwnerController();
	return PC && PC->IsLocalController();
}

double UMDRangedComponent::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

int32 UMDRangedComponent::GetViewMagRounds() const
{
	return FMath::Max(GetMagRounds(MDItemType::RangedSlot) - PendingShotIds.Num(), 0);
}

float UMDRangedComponent::GetBloomNow(bool bServer) const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	const UWorld* World = GetWorld();
	if (!Data || !World)
	{
		return 0.0f;
	}
	const float Bloom = bServer ? ServerBloom : LocalBloom;
	const double Since = World->GetTimeSeconds() - (bServer ? ServerBloomTime : LocalBloomTime);
	return FMDRangedMath::DecayBloom(Bloom, Data->SpreadBloomDecay, static_cast<float>(Since));
}

float UMDRangedComponent::GetCurrentSpread(bool bServer) const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	if (!Data)
	{
		return 0.0f;
	}
	const APawn* Pawn = GetPawn();
	const bool bMoving = Pawn && Pawn->GetVelocity().Size2D() > MDRangedComponentLocal::MovingSpeed;
	// The owner's view reacts to its own aim at once; the server (and everyone else) uses the replicated flag.
	const float Base = (bServer ? bAimed : IsAimingLocal()) ? Data->SpreadAimed : Data->SpreadHip;
	// SpreadMoving is a multiplier on the base spread while the shooter moves.
	return FMDRangedMath::ComputeSpread(Base, bMoving, Data->SpreadMoving, GetBloomNow(bServer));
}

bool UMDRangedComponent::GetMuzzleWorld(FVector& OutMuzzle) const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	if (WeaponMeshComp && Data)
	{
		if (WeaponMeshComp->DoesSocketExist(Data->MuzzleSocket))
		{
			OutMuzzle = WeaponMeshComp->GetSocketLocation(Data->MuzzleSocket);
			return true;
		}
		if (Data->bHasMuzzleLocal)
		{
			OutMuzzle = WeaponMeshComp->GetComponentTransform().TransformPosition(Data->MuzzleLocal);
			return true;
		}
	}
	// No visual gun on this machine (dedicated server, tests): a point in front of the eyes.
	if (const APawn* Pawn = GetPawn())
	{
		OutMuzzle = Pawn->GetPawnViewLocation() + Pawn->GetBaseAimRotation().Vector() * 40.0f
			- FVector(0.0, 0.0, 20.0);
		return true;
	}
	return false;
}

bool UMDRangedComponent::ComputeAimLocal(FVector& OutMuzzle, FVector& OutAimDir, float& OutAimDistance) const
{
	const APlayerController* PC = GetOwnerController();
	const APawn* Pawn = GetPawn();
	UWorld* World = GetWorld();
	if (!PC || !Pawn || !World || !GetMuzzleWorld(OutMuzzle))
	{
		return false;
	}
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Fwd = CamRot.Vector();
	// Start the trace past the pawn so a third-person camera does not aim at the player's own back or at a wall behind them.
	const double Ahead = FMath::Max(FVector::DotProduct(Pawn->GetActorLocation() - CamLoc, Fwd), 0.0);
	const FVector Start = CamLoc + Fwd * Ahead;
	const FVector End = Start + Fwd * MDRangedComponentLocal::AimTraceRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRangedAim), false, Pawn);
	FHitResult Hit;
	const FVector AimPoint = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? FVector(Hit.ImpactPoint) : End;
	FVector ToAim = AimPoint - OutMuzzle;
	OutAimDistance = static_cast<float>(ToAim.Size());
	if (OutAimDistance < 30.0f || FVector::DotProduct(ToAim, Fwd) <= 0.0)
	{
		OutAimDir = Fwd; // aim point is behind / on the muzzle (wall right in front of the player): shoot straight along the view
		OutAimDistance = FMath::Max(OutAimDistance, 30.0f);
		return true;
	}
	OutAimDir = ToAim / OutAimDistance;
	return true;
}

bool UMDRangedComponent::ComputeAimServer(const FVector& ClaimedOrigin, FVector& OutAimDir, float& OutAimDistance) const
{
	const APawn* Pawn = GetPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return false;
	}
	const FVector Eye = Pawn->GetPawnViewLocation();
	const FVector Fwd = Pawn->GetBaseAimRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRangedAimServer), false, Pawn);
	FHitResult Hit;
	const FVector End = Eye + Fwd * MDRangedComponentLocal::AimTraceRange;
	const FVector AimPoint = World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params) ? FVector(Hit.ImpactPoint) : End;
	FVector ToAim = AimPoint - ClaimedOrigin;
	OutAimDistance = static_cast<float>(ToAim.Size());
	if (OutAimDistance < 30.0f || FVector::DotProduct(ToAim, Fwd) <= 0.0)
	{
		OutAimDir = Fwd;
		OutAimDistance = FMath::Max(OutAimDistance, 30.0f);
		return true;
	}
	OutAimDir = ToAim / OutAimDistance;
	return true;
}

void UMDRangedComponent::SetFireHeld(bool bHeld)
{
	if (bHeld)
	{
		if (!IsOwnerLocal() || !bRangedMode)
		{
			return;
		}
		bFireHeld = true;
		bSemiPending = true;
		const UWorld* World = GetWorld();
		SemiBufferUntil = (World ? World->GetTimeSeconds() : 0.0) + MDRangedComponentLocal::SemiBufferSeconds;
		SetComponentTickEnabled(true);
	}
	else
	{
		bFireHeld = false;
		bSemiPending = false;
		// R5: letting go of the trigger ends a beam at once (the server stops its 10 Hz ticks and starts venting).
		if (UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner()))
		{
			Beam->OwnerStopBeam();
		}
	}
}

bool UMDRangedComponent::TryActivateFire()
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	const UWorld* World = GetWorld();
	if (!Data || !World || !bRangedMode || bReloading)
	{
		return false;
	}
	if (Data->HitModel != EMDRangedHitModel::Beam && GetViewMagRounds() <= 0) // beams have no magazine: heat / charge gate them
	{
		// R3: an empty magazine cannot fire; pulling the trigger asks for the reload (when auto-reload is on).
		if (IsAutoReloadEnabled() && !Data->AmmoRow.IsNone())
		{
			TryActivateReload();
		}
		return false;
	}
	if (World->GetTimeSeconds() < NextLocalShotTime)
	{
		return false;
	}
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		if (ASC->FindAbilitySpecFromClass(UMDRangedFireAbility::StaticClass()))
		{
			return ASC->TryActivateAbilityByClass(UMDRangedFireAbility::StaticClass());
		}
	}
	return FireShotLocal();
}

bool UMDRangedComponent::FireShotLocal()
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	UWorld* World = GetWorld();
	APawn* Pawn = GetPawn();
	if (Data && World && Pawn && bRangedMode && Data->HitModel == EMDRangedHitModel::Beam)
	{
		// R5: a beam weapon "fires" by starting the beam (hold-to-fire; UMDBeamComponent gates it, runs the soft lock and asks the server).
		UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
		return Beam && Beam->OwnerStartBeam();
	}
	if (!Data || !World || !Pawn || !bRangedMode || Data->HitModel != EMDRangedHitModel::Projectile)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextLocalShotTime || GetViewMagRounds() <= 0 || bReloading || UMDGameplayStatics::IsPawnDead(Pawn) || UMDPoiseComponent::IsActorStunned(Pawn))
	{
		return false;
	}
	// Autonomous soft lock: firing picks / keeps / retargets the soft target (local controller only; a hard lock is left alone).
	if (Pawn->IsPlayerControlled())
	{
		if (UMDLockOnComponent* Lock = UMDLockOnComponent::FindFor(Pawn))
		{
			Lock->NotifyPlayerAttacked();
		}
	}
	FVector Muzzle;
	FVector AimDir;
	float AimDistance = 0.0f;
	if (!ComputeAimLocal(Muzzle, AimDir, AimDistance))
	{
		return false;
	}

	const int32 Seed = FMath::Rand();
	const float Spread = GetCurrentSpread(false);
	const FVector Dir = FMDRangedMath::SpreadDirection(AimDir, Spread, Seed);

	FMDShotRequest Request;
	Request.ShotId = NextShotId++;
	Request.ClientTime = static_cast<float>(GetServerNow());
	Request.Origin = Muzzle;
	Request.Dir = Dir;
	Request.SpreadSeed = Seed;

	NextLocalShotTime = Now + 1.0 / FMath::Max(Data->RoundsPerSecond, 0.1f);
	LocalBloom = FMDRangedMath::AddBloom(GetBloomNow(false), Data->SpreadBloom, Data->MaxBloom);
	LocalBloomTime = Now;

	AddRecoil(*Data, Seed);
	ApplyWeaponKick();
	PlayShotCosmetics(Muzzle, Dir);

	if (GetOwner()->HasAuthority())
	{
		ProcessShot(Request, 0.0f);
	}
	else
	{
		PendingShotIds.Add(Request.ShotId);
		FMDProjectileParams Twin;
		Twin.Data = const_cast<UMDRangedWeaponData*>(Data);
		Twin.Shooter = Pawn;
		Twin.Origin = Muzzle;
		Twin.Dir = Dir;
		Twin.ShotId = Request.ShotId;
		AMDProjectile::SpawnTwin(World, Twin);
		Server_Fire(Request);
	}
	SetComponentTickEnabled(true);
	return true;
}

void UMDRangedComponent::Server_Fire_Implementation(const FMDShotRequest& Request)
{
	ProcessShot(Request, -1.0f);
}

EMDShotReject UMDRangedComponent::ProcessShot(const FMDShotRequest& Request, float RTTOverrideSeconds)
{
	UWorld* World = GetWorld();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !World)
	{
		return EMDShotReject::State;
	}
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	APawn* Pawn = GetPawn();
	const uint8 Slot = MDItemType::RangedSlot;

	FMDShotValidationInput In;
	In.bRangedMode = bRangedMode;
	In.bGunEquipped = Data != nullptr && Data->HitModel == EMDRangedHitModel::Projectile;
	In.bDead = !Pawn || UMDGameplayStatics::IsPawnDead(Pawn);
	In.bStaggered = Pawn && UMDPoiseComponent::IsActorStunned(Pawn);
	In.bBusy = bReloading;
	In.MagRounds = GetMagRounds(Slot);
	In.RoundsPerSecond = Data ? Data->RoundsPerSecond : 0.0f;
	In.SecondsSinceLastShot = LastAcceptedClientTime < -999.0 ? 1000.0f : static_cast<float>(Request.ClientTime - LastAcceptedClientTime);
	In.ClientTimeAheadOfServer = static_cast<float>(Request.ClientTime - GetServerNow());
	In.Origin = Request.Origin;
	In.Dir = Request.Dir;
	In.SpreadDeg = GetCurrentSpread(true);

	FVector ServerMuzzle = Request.Origin;
	if (Pawn)
	{
		GetMuzzleWorld(ServerMuzzle);
	}
	In.ServerMuzzle = ServerMuzzle;

	float AimDistance = 1000.0f;
	FVector ServerAim = Request.Dir;
	if (Pawn)
	{
		ComputeAimServer(Request.Origin, ServerAim, AimDistance);
	}
	In.ServerAimDir = ServerAim;
	// The client aims along the camera ray, the server along the eye ray: at short range the two aim points differ by the camera offset.
	In.AimLagSlackDeg = FMath::RadiansToDegrees(FMath::Atan(150.0f / FMath::Max(AimDistance, 200.0f))) + 4.0f;

	const EMDShotReject Verdict = UMDRangedStatics::ValidateShot(In);
	if (MDRangedComponentLocal::CVarDrawShots.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(World, Request.Origin, Request.Origin + FVector(Request.Dir) * 500.0, Verdict == EMDShotReject::None ? FColor::Green : FColor::Red, false, 1.5f, 0, 1.0f);
	}
	if (Verdict != EMDShotReject::None)
	{
		FinishShotServer(Request, Verdict);
		return Verdict;
	}

	// ---- accepted ----
	LastAcceptedClientTime = Request.ClientTime;
	++AcceptedShots;
	LastReject = EMDShotReject::None;
	ServerBloom = FMDRangedMath::AddBloom(GetBloomNow(true), Data->SpreadBloom, Data->MaxBloom);
	ServerBloomTime = World->GetTimeSeconds();

	const int32 NewRounds = In.MagRounds - 1;
	SetMagRounds(Slot, NewRounds);
	++ShotsSinceAmmoDrop; // pity counter for the ammo drops
	if (NewRounds <= 0 && IsAutoReloadEnabled())
	{
		BeginReloadServer();
	}

	float Damage = Data->DamagePerShot * MDRangedComponentLocal::CVarDamageScale.GetValueOnGameThread();
	if (const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		Damage *= 1.0f + ASC->GetPerkAttackPct();
	}
	if (const UMDInventoryComponent* Inv = GetInventory())
	{
		Damage *= Inv->GetSlotConditionMultiplier(Slot);
	}

	float RTT = RTTOverrideSeconds;
	if (RTT < 0.0f)
	{
		const APlayerState* PS = Cast<APlayerState>(GetOwner());
		RTT = PS ? PS->GetPingInMilliseconds() / 1000.0f : 0.0f;
	}
	const float FastForward = UMDRangedStatics::ComputeSpawnFastForward(RTT);

	const int32 Pellets = FMath::Max(Data->Pellets, 1);
	for (int32 i = 0; i < Pellets; ++i)
	{
		FMDProjectileParams P;
		P.Data = const_cast<UMDRangedWeaponData*>(Data);
		P.Shooter = Pawn;
		P.Origin = Request.Origin;
		P.Dir = i == 0 ? FVector(Request.Dir) : FMDRangedMath::SpreadDirection(Request.Dir, In.SpreadDeg, Request.SpreadSeed + i);
		P.Damage = Damage;
		P.FastForward = FastForward;
		P.ShotId = Request.ShotId;
		AMDProjectile::SpawnReal(World, P);
	}

	UMDRangedStatics::ReportGunshot(World, Request.Origin, Pawn);
	Multicast_ShotFX(Request.Origin, Request.Dir);
	FinishShotServer(Request, EMDShotReject::None);
	return EMDShotReject::None;
}

void UMDRangedComponent::FinishShotServer(const FMDShotRequest& Request, EMDShotReject Reason)
{
	ShotAck = Request.ShotId;
	if (Reason != EMDShotReject::None)
	{
		LastReject = Reason;
		Client_CorrectMag(MDItemType::RangedSlot, GetMagRounds(MDItemType::RangedSlot), Request.ShotId);
	}
	// PlayerStates replicate slowly; the ack and the magazine should reach the owner now.
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UMDRangedComponent::Client_CorrectMag_Implementation(uint8 Slot, int32 Rounds, uint16 ShotId)
{
	++MagCorrections;
	LastCorrectedRounds = Rounds;
	if (FMDSlotMag* Mag = Mags.FindByPredicate([Slot](const FMDSlotMag& M) { return M.Slot == Slot; }))
	{
		Mag->Rounds = Rounds;
	}
	PendingShotIds.RemoveAll([ShotId](uint16 Id) { return FMDRangedMath::IsShotAcked(Id, ShotId); });
}

void UMDRangedComponent::Multicast_ShotFX_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || IsOwnerLocal())
	{
		return; // the shooter already played it when pulling the trigger
	}
	PlayShotCosmetics(Origin, Dir);
}

void UMDRangedComponent::OnRep_ShotAck()
{
	TrimPendingShots();
}

void UMDRangedComponent::TrimPendingShots()
{
	const uint16 Ack = ShotAck;
	PendingShotIds.RemoveAll([Ack](uint16 Id) { return FMDRangedMath::IsShotAcked(Id, Ack); });
}

void UMDRangedComponent::NotifyShotLanded()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	UMDInventoryComponent* Inv = GetInventory();
	if (!Data || !Inv)
	{
		return;
	}
	const int32 Points = FMDRangedMath::AccumulateWear(WearAccumulator, Data->WearPerHit);
	if (Points > 0)
	{
		Inv->ApplyWearToSlot(MDItemType::RangedSlot, Points);
	}
}

void UMDRangedComponent::PlayShotCosmetics(const FVector& Origin, const FVector& Dir)
{
	if (const UWorld* ShotWorld = GetWorld())
	{
		LastShotSeenTime = ShotWorld->GetTimeSeconds(); // keeps the aim offset layer on for a moment after a shot (every machine)
	}
	UWorld* World = GetWorld();
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	if (!World || !Data || World->GetNetMode() == NM_DedicatedServer || Data->MuzzleFlashFX.IsNull())
	{
		return;
	}
	if (UNiagaraSystem* FX = Data->MuzzleFlashFX.LoadSynchronous())
	{
		// Capped one-shot: a looping muzzle system would otherwise stay alive (and pile up) for the rest of the session.
		MDRangedFX::SpawnOneShot(World, FX, Origin, FRotator(Dir.Rotation()), FVector::OneVector, MDRangedFX::MuzzleMaxSeconds);
	}
}

void UMDRangedComponent::AddRecoil(const UMDRangedWeaponData& Data, int32 Seed)
{
	APlayerController* PC = GetOwnerController();
	if (!PC)
	{
		return;
	}
	const FVector2D Kick = FMDRangedMath::RecoilKick(Data.RecoilPitch, Data.RecoilYaw, Seed);
	FRotator Rot = PC->GetControlRotation();
	Rot.Pitch += Kick.X;
	Rot.Yaw += Kick.Y;
	PC->SetControlRotation(Rot);
	RecoilPitch += static_cast<float>(Kick.X);
	RecoilYaw += static_cast<float>(Kick.Y);
}

void UMDRangedComponent::ApplyWeaponKick()
{
	KickAlpha = 1.0f;
	bKickApplied = true;
}

void UMDRangedComponent::UpdateReticle()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	SyncAutoReloadPref();
	const bool bWant = bRangedMode && IsOwnerLocal() && GetEquippedWeaponData() != nullptr;
	if (bWant && !Reticle)
	{
		if (APlayerController* PC = GetOwnerController())
		{
			Reticle = CreateWidget<UMDRangedReticleWidget>(PC);
			if (Reticle)
			{
				Reticle->SetRanged(this);
				Reticle->AddToViewport(50);
				if (UMDCombatFeedbackComponent* Feedback = UMDCombatFeedbackComponent::FindLocal(World))
				{
					Feedback->OnRangedHitConfirmed.AddUniqueDynamic(Reticle, &UMDRangedReticleWidget::OnHitConfirmed);
				}
			}
		}
	}
	else if (!bWant && Reticle)
	{
		Reticle->RemoveFromParent();
		Reticle = nullptr;
	}
}

void UMDRangedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	// ---- reload: the server cancels it when the shooter is staggered or dead (nothing was moved yet, so nothing is lost) ----
	if (bReloading && GetOwner() && GetOwner()->HasAuthority())
	{
		const APawn* ReloadPawn = GetPawn();
		if (ReloadPawn && (UMDGameplayStatics::IsPawnDead(ReloadPawn) || UMDPoiseComponent::IsActorStunned(ReloadPawn)))
		{
			CancelReload();
		}
	}

	// ---- aim rules: sprint / stagger / death / holster leave aim (server), the owner's wish resumes aim after a sprint ----
	if (bAimed || bAimWanted || bAimPredicted)
	{
		EvaluateAimRules(IsPawnSprintingNow());
	}

	// ---- trigger ----
	if (bFireHeld && IsOwnerLocal() && bRangedMode && Data)
	{
		if (Data->HitModel == EMDRangedHitModel::Beam)
		{
			// R5: holding the trigger keeps the beam on; the activation only happens when it is off (the first press, or after an
			// overheat lock ended while the trigger stayed down).
			const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
			if (Beam && !Beam->IsOwnerBeamOn())
			{
				TryActivateFire();
			}
		}
		else if (Data->FireMode == EMDFireMode::Auto)
		{
			TryActivateFire();
		}
		else if (bSemiPending)
		{
			if (Now > SemiBufferUntil)
			{
				bSemiPending = false; // the press expired while the cadence cap was still running
			}
			else if (TryActivateFire())
			{
				bSemiPending = false;
			}
		}
	}

	// ---- camera recoil recovery ----
	if (RecoilPitch != 0.0f || RecoilYaw != 0.0f)
	{
		const float Recovery = Data ? Data->RecoilRecovery : 0.25f;
		const float NewPitch = FMDRangedMath::RecoilRemaining(RecoilPitch, Recovery, DeltaTime);
		const float NewYaw = FMDRangedMath::RecoilRemaining(RecoilYaw, Recovery, DeltaTime);
		if (APlayerController* PC = GetOwnerController())
		{
			FRotator Rot = PC->GetControlRotation();
			Rot.Pitch -= RecoilPitch - NewPitch;
			Rot.Yaw -= RecoilYaw - NewYaw;
			PC->SetControlRotation(Rot);
		}
		RecoilPitch = NewPitch;
		RecoilYaw = NewYaw;
	}

	// ---- additive weapon kick ----
	if (bKickApplied && WeaponMeshComp && Data)
	{
		KickAlpha = FMath::FInterpTo(KickAlpha, 0.0f, DeltaTime, 18.0f);
		const bool bDone = KickAlpha < 0.01f;
		if (bDone)
		{
			KickAlpha = 0.0f;
			bKickApplied = false;
			WeaponMeshComp->SetRelativeTransform(WeaponBaseRelative);
		}
		else if (const USceneComponent* Parent = WeaponMeshComp->GetAttachParent())
		{
			const APawn* Pawn = GetPawn();
			const FTransform ParentWorld = Parent->GetSocketTransform(WeaponMeshComp->GetAttachSocketName());
			const FTransform BaseWorld = WeaponBaseRelative * ParentWorld;
			const FVector Fwd = Pawn ? Pawn->GetActorForwardVector() : FVector::ForwardVector;
			const FVector Right = Pawn ? Pawn->GetActorRightVector() : FVector::RightVector;
			const FQuat Tip(Right, FMath::DegreesToRadians(Data->KickPitchDeg * KickAlpha));
			FTransform Kicked(Tip * BaseWorld.GetRotation(), BaseWorld.GetLocation() - Fwd * (Data->KickBackCm * KickAlpha), BaseWorld.GetScale3D());
			WeaponMeshComp->SetRelativeTransform(Kicked.GetRelativeTransform(ParentWorld));
		}
	}
	else if (bKickApplied)
	{
		KickAlpha = 0.0f;
		bKickApplied = false;
	}

	if (!bFireHeld && !bSemiPending && RecoilPitch == 0.0f && RecoilYaw == 0.0f && !bKickApplied && !bReloading
		&& !bAimed && !bAimWanted && !bAimPredicted)
	{
		SetComponentTickEnabled(false);
	}
}

// =====================================================================================================================
// R3 reload, reserves, pity
// =====================================================================================================================

int32 UMDRangedComponent::GetReserveAmmo() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	const UMDInventoryComponent* Inv = GetInventory();
	return Data && Inv && !Data->AmmoRow.IsNone() ? Inv->GetAmmoCount(Data->AmmoRow) : 0;
}

int32 UMDRangedComponent::GetMagSize() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	return Data ? Data->MagSize : 0;
}

// R5: the beam numbers come from UMDBeamComponent (heat / charge as this machine knows them; every machine knows the overheat flag).
float UMDRangedComponent::GetHeat() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsBeamWeapon() ? Beam->GetHeatView() : 0.0f;
}

float UMDRangedComponent::GetMaxHeat() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsBeamWeapon() ? Beam->GetMaxHeatView() : 0.0f;
}

float UMDRangedComponent::GetCharge() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsChargeStyleBeam() ? Beam->GetChargeView() : 0.0f;
}

float UMDRangedComponent::GetMaxCharge() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsChargeStyleBeam() ? Beam->GetMaxChargeView() : 0.0f;
}

bool UMDRangedComponent::IsOverheated() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsBeamWeapon() && Beam->IsOverheatedView();
}

float UMDRangedComponent::GetOverheatLockFraction() const
{
	const UMDBeamComponent* Beam = UMDBeamComponent::Find(GetOwner());
	return Beam && IsBeamWeapon() ? Beam->GetOverheatLockFraction() : 0.0f;
}

bool UMDRangedComponent::ComputeServerBeamAim(FVector& OutMuzzle, FVector& OutDir) const
{
	if (!GetPawn() || !GetMuzzleWorld(OutMuzzle))
	{
		return false;
	}
	float Distance = 0.0f;
	return ComputeAimServer(OutMuzzle, OutDir, Distance);
}

bool UMDRangedComponent::IsBeamWeapon() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	return Data && (Data->HitModel == EMDRangedHitModel::Beam || Data->FireMode == EMDFireMode::Beam);
}

bool UMDRangedComponent::IsChargeStyleBeam() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	return IsBeamWeapon() && Data->Beam.ChargeDrainPerSecond > 0.0f;
}

void UMDRangedComponent::SyncAutoReloadPref()
{
	if (!IsOwnerLocal())
	{
		return;
	}
	const bool bWanted = GetDefault<UMDLocalSettings>()->bAutoReload;
	if (bWanted == bAutoReloadPref)
	{
		return;
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bAutoReloadPref = bWanted;
	}
	else
	{
		Server_SetAutoReload(bWanted);
	}
}

void UMDRangedComponent::Server_SetAutoReload_Implementation(bool bAuto)
{
	bAutoReloadPref = bAuto;
}

bool UMDRangedComponent::IsAutoReloadEnabled() const
{
	return MDRangedComponentLocal::CVarAutoReload.GetValueOnGameThread() != 0 && bAutoReloadPref;
}

float UMDRangedComponent::GetReloadProgress() const
{
	if (!bReloading || ReloadDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(static_cast<float>(GetServerNow() - ReloadStartServerTime) / ReloadDuration, 0.0f, 1.0f);
}

int32 UMDRangedComponent::GetStashedMag(FName GunRow) const
{
	const int32* Rounds = StashedMags.Find(GunRow);
	return Rounds ? *Rounds : 0;
}

EMDReloadReject UMDRangedComponent::GetReloadReject() const
{
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	const APawn* Pawn = GetPawn();
	const bool bGun = Data != nullptr && Data->HitModel == EMDRangedHitModel::Projectile;
	return FMDAmmoRules::CanReload(bRangedMode, bGun, Data && !Data->AmmoRow.IsNone(), bReloading,
		!Pawn || UMDGameplayStatics::IsPawnDead(Pawn), Pawn && UMDPoiseComponent::IsActorStunned(Pawn),
		GetMagRounds(MDItemType::RangedSlot), Data ? Data->MagSize : 0, GetReserveAmmo());
}

bool UMDRangedComponent::TryActivateReload()
{
	if (!IsOwnerLocal() || GetReloadReject() != EMDReloadReject::None)
	{
		return false;
	}
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		if (ASC->FindAbilitySpecFromClass(UMDReloadAbility::StaticClass()))
		{
			return ASC->TryActivateAbilityByClass(UMDReloadAbility::StaticClass());
		}
	}
	RequestReload();
	return true;
}

void UMDRangedComponent::RequestReload()
{
	if (!GetOwner())
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		BeginReloadServer();
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now < NextReloadRequestTime)
	{
		return; // the trigger can ask every tick on an empty magazine; one request per quarter second is plenty
	}
	NextReloadRequestTime = Now + 0.25;
	Server_Reload();
}

void UMDRangedComponent::Server_Reload_Implementation()
{
	BeginReloadServer();
}

EMDReloadReject UMDRangedComponent::BeginReloadServer()
{
	UWorld* World = GetWorld();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !World)
	{
		return EMDReloadReject::NoGun;
	}
	const EMDReloadReject Verdict = GetReloadReject();
	if (Verdict != EMDReloadReject::None)
	{
		return Verdict;
	}
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	bReloading = true;
	ReloadStartServerTime = static_cast<float>(GetServerNow());
	ReloadDuration = FMath::Max(0.05f, Data->ReloadSeconds * MDRangedComponentLocal::CVarReloadScale.GetValueOnGameThread());
	World->GetTimerManager().SetTimer(ReloadTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { CompleteReload(); }), ReloadDuration, false);
	ApplyReloadTagLocally();
	SetComponentTickEnabled(true); // the tick watches for a stagger or death while the reload runs
	GetOwner()->ForceNetUpdate();
	return EMDReloadReject::None;
}

int32 UMDRangedComponent::CompleteReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bReloading)
	{
		return 0;
	}
	const APawn* Pawn = GetPawn();
	if (Pawn && (UMDGameplayStatics::IsPawnDead(Pawn) || UMDPoiseComponent::IsActorStunned(Pawn)))
	{
		CancelReload();
		return 0;
	}
	const UMDRangedWeaponData* Data = GetEquippedWeaponData();
	UMDInventoryComponent* Inv = const_cast<UMDInventoryComponent*>(GetInventory());
	int32 Moved = 0;
	if (Data && Inv && !Data->AmmoRow.IsNone())
	{
		const int32 Mag = GetMagRounds(MDItemType::RangedSlot);
		const int32 Want = FMDAmmoRules::ReloadAmount(Mag, Data->MagSize, Inv->GetAmmoCount(Data->AmmoRow));
		Moved = Inv->ConsumeAmmo(Data->AmmoRow, Want);
		SetMagRounds(MDItemType::RangedSlot, Mag + Moved);
	}
	ClearReloadState();
	GetOwner()->ForceNetUpdate();
	return Moved;
}

void UMDRangedComponent::CancelReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bReloading)
	{
		return;
	}
	ClearReloadState();
	GetOwner()->ForceNetUpdate();
}

void UMDRangedComponent::ClearReloadState()
{
	bReloading = false;
	ReloadDuration = 0.0f;
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimer);
	}
	ApplyReloadTagLocally();
}

void UMDRangedComponent::OnRep_Reloading()
{
	ApplyReloadTagLocally();
}

void UMDRangedComponent::ApplyReloadTagLocally()
{
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		const bool bHasTag = ASC->HasMatchingGameplayTag(TAG_State_Reloading);
		if (bReloading && !bHasTag)
		{
			ASC->AddLooseGameplayTag(TAG_State_Reloading);
		}
		else if (!bReloading && bHasTag)
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Reloading);
		}
	}
}


