#include "MDCombatNotifies.h"

#include "AI/MDEnemyAttackComponent.h"
#include "MDCombatDebug.h"
#include "MDCombatStatics.h"
#include "MDComboComponent.h"
#include "MDComboSet.h"
#include "MDImpactMath.h"
#include "MDUISettings.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "UObject/SoftObjectPath.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace MDCombatNotifyLocal
{
	// Calls a parameterless Blueprint function on AC_Combat. Anything with parameters or a return value is refused.
	void CallCombatFunction(const AActor* Owner, FName FunctionName)
	{
		if (FunctionName == NAME_None)
		{
			return;
		}
		UActorComponent* Combat = UMDGameplayStatics::FindCombatComponent(Owner);
		UFunction* Function = Combat ? Combat->FindFunction(FunctionName) : nullptr;
		if (Function && Function->NumParms == 0)
		{
			Combat->ProcessEvent(Function, nullptr);
		}
	}
}

namespace MDCombatTrailLocal
{
	const TCHAR* const DefaultTrailPath = TEXT("/Game/Fab/SwordTrailVFX/VFX/NS_Trail_01.NS_Trail_01");

	// Notify instances are shared between meshes, so active trails are keyed by (mesh, notify instance).
	struct FTrailKey
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		const UObject* Notify = nullptr;
		bool operator==(const FTrailKey& O) const { return Mesh == O.Mesh && Notify == O.Notify; }
	};
	inline uint32 GetTypeHash(const FTrailKey& K) { return HashCombine(GetTypeHash(K.Mesh), ::GetTypeHash(K.Notify)); }

	struct FTrailEntry
	{
		TArray<TWeakObjectPtr<UNiagaraComponent>> Comps;
		bool bFromSwordTrail = false;
	};

	TMap<FTrailKey, FTrailEntry>& Active()
	{
		static TMap<FTrailKey, FTrailEntry> Map;
		return Map;
	}

	void DeactivateEntry(FTrailEntry& Entry)
	{
		for (const TWeakObjectPtr<UNiagaraComponent>& Comp : Entry.Comps)
		{
			if (UNiagaraComponent* C = Comp.Get())
			{
				C->Deactivate(); // lets the ribbon fade, auto-destroys when empty
			}
		}
	}

	void StopTrails(USkeletalMeshComponent* MeshComp, const UObject* Notify)
	{
		FTrailEntry Entry;
		if (Active().RemoveAndCopyValue(FTrailKey{ MeshComp, Notify }, Entry))
		{
			DeactivateEntry(Entry);
		}
	}

	// Drops entries whose mesh is gone or whose trail components have all been destroyed. A missed NotifyEnd (montage interrupted,
	// notify skipped) would otherwise leave a stale bFromSwordTrail entry that suppresses every later weapon-trace trail.
	void PurgeStale()
	{
		for (auto It = Active().CreateIterator(); It; ++It)
		{
			bool bLive = It.Key().Mesh.IsValid();
			if (bLive)
			{
				bLive = false;
				for (const TWeakObjectPtr<UNiagaraComponent>& Comp : It.Value().Comps)
				{
					if (Comp.IsValid() && Comp->IsActive())
					{
						bLive = true;
						break;
					}
				}
			}
			if (!bLive)
			{
				It.RemoveCurrent();
			}
		}
	}

	bool HasSwordTrailNotify(const USkeletalMeshComponent* MeshComp)
	{
		PurgeStale();
		for (const TPair<FTrailKey, FTrailEntry>& Pair : Active())
		{
			if (Pair.Key.Mesh.Get() == MeshComp && Pair.Value.bFromSwordTrail)
			{
				return true;
			}
		}
		return false;
	}

	// How a pack ribbon system is authored, in system units at scale 1, along the component's local +Z (the blade axis of the pack's
	// demo sword, which runs 0..118 along +Z with the Sword socket at its origin). Span is the ribbon length; CenterOffset is where
	// its middle sits. Unknown systems are assumed to be centred on a demo-sized blade.
	struct FTrailShape
	{
		double Span = 118.0;
		double CenterOffset = 59.0;
	};

	FTrailShape GetTrailShape(const UNiagaraSystem* System)
	{
		const FString Name = System ? System->GetName() : FString();
		if (Name == TEXT("NS_Trail_10"))
		{
			return FTrailShape{ 100.0, 65.0 }; // Ribbon Width 100, Position Offset (0,0,65) local
		}
		if (Name == TEXT("NS_Trail_01"))
		{
			return FTrailShape{ 200.0, 0.0 }; // Ribbon Width 200, no offset: centred on the origin
		}
		return FTrailShape();
	}

	// Cosmetic trail start shared by MD Sword Trail and MD Weapon Trace. Every non-dedicated machine.
	void StartTrails(USkeletalMeshComponent* MeshComp, const UObject* Notify, bool bFromSwordTrail, EMDTraceHandOverride TraceHand,
		const TSoftObjectPtr<UNiagaraSystem>& FallbackTrail, const FRotator& RotationOffset, const FVector& LocationOffset)
	{
		const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
		if (!Owner || !World || World->GetNetMode() == NM_DedicatedServer || !World->IsGameWorld() || !MDCombatDebug::TrailsEnabled())
		{
			return;
		}
		if (bFromSwordTrail)
		{
			// An explicit trail window wins: drop anything the weapon-trace path started on this mesh.
			TArray<FTrailKey> Keys;
			for (const TPair<FTrailKey, FTrailEntry>& Pair : Active())
			{
				if (Pair.Key.Mesh.Get() == MeshComp)
				{
					Keys.Add(Pair.Key);
				}
			}
			for (const FTrailKey& Key : Keys)
			{
				StopTrails(MeshComp, Key.Notify);
			}
		}
		else
		{
			if (HasSwordTrailNotify(MeshComp))
			{
				return;
			}
			StopTrails(MeshComp, Notify);
		}

		const UMDComboComponent* Combo = UMDComboComponent::FindFor(Owner);
		const UMDComboSet* Set = Combo ? Combo->ComboSet : nullptr;
		UNiagaraSystem* System = Set ? Set->TrailVFX.LoadSynchronous() : nullptr;
		if (!System)
		{
			System = FallbackTrail.LoadSynchronous();
		}
		if (!System)
		{
			System = Cast<UNiagaraSystem>(FSoftObjectPath(DefaultTrailPath).TryLoad());
		}
		if (!System)
		{
			return;
		}
		const EMDVFXLevel Level = GetDefault<UMDLocalSettings>()->VFXLevel;
		const float Width = MDImpactMath::TrailScaleForLevel(Set ? Set->TrailWidthScale : 1.0f, Level);
		if (Width <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		TArray<AActor*> Weapons;
		UMDCombatStatics::GetSwingWeapons(Owner, TraceHand, Weapons);
		const FTrailShape Shape = GetTrailShape(System);
		FTrailEntry& Entry = Active().FindOrAdd(FTrailKey{ MeshComp, Notify });
		Entry.bFromSwordTrail = bFromSwordTrail;
		for (AActor* Weapon : Weapons)
		{
			USceneComponent* Root = Weapon ? Weapon->GetRootComponent() : nullptr;
			if (!Root)
			{
				continue;
			}
			FVector Top, Bottom;
			UMDCombatStatics::GetWeaponTracePoints(Weapon, Top, Bottom);
			const FTransform RootXf = Root->GetComponentTransform();

			// The pack's ribbons are authored for a blade running along the component's local +Z (see FTrailShape), so align local +Z
			// with the blade instead of assuming the weapon mesh is modelled that way, and size/centre the ribbon on the actual blade.
			FRotator RelRot = RotationOffset;
			FVector RelLoc = RootXf.InverseTransformPosition((Top + Bottom) * 0.5);
			double TrailScale = Width;
			const FVector Blade = Top - Bottom;
			const double BladeLen = Blade.Size();
			if (BladeLen > 1.0)
			{
				const FVector BladeDirWorld = Blade / BladeLen;
				const FVector BladeDirLocal = RootXf.InverseTransformVectorNoScale(BladeDirWorld);
				const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, BladeDirLocal);
				RelRot = (Align * RotationOffset.Quaternion()).Rotator();

				const double RootScale = FMath::Max(KINDA_SMALL_NUMBER, Root->GetComponentScale().GetAbsMax());
				const double LengthScale = FMath::Clamp(BladeLen / Shape.Span, 0.25, 4.0) / RootScale;
				TrailScale = Width * LengthScale;
				// Slide the origin back so the system's own centre offset lands on the blade midpoint.
				const FVector OriginWorld = (Top + Bottom) * 0.5 - BladeDirWorld * (Shape.CenterOffset * TrailScale * RootScale);
				RelLoc = RootXf.InverseTransformPosition(OriginWorld);
			}
			RelLoc += LocationOffset;
			if (UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Root, NAME_None, RelLoc, RelRot,
					FVector(TrailScale), EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None))
			{
				Entry.Comps.Add(Comp);
			}
		}
	}
}

void UMDANS_WeaponTrace::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (bSpawnTrail)
	{
		MDCombatTrailLocal::StartTrails(MeshComp, this, false, TraceHand, TSoftObjectPtr<UNiagaraSystem>(), FRotator::ZeroRotator, FVector::ZeroVector);
	}
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	UMDEnemyAttackComponent::NotifyTraceWindowBegan(Owner); // every machine: an enemy's wind-up is over, play rate back to 1
	if (Owner && Owner->HasAuthority())
	{
		if (bUseCppTrace)
		{
			UMDCombatStatics::BeginWeaponTrace(Owner);
		}
		else
		{
			MDCombatNotifyLocal::CallCombatFunction(Owner, BeginFunctionName);
		}
	}
}

void UMDANS_WeaponTrace::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (Owner && Owner->HasAuthority())
	{
		if (bUseCppTrace)
		{
			UMDCombatStatics::TraceEquippedWeapons(const_cast<AActor*>(Owner), TraceHand);
		}
		else
		{
			MDCombatNotifyLocal::CallCombatFunction(Owner, TickFunctionName);
		}
	}
}

void UMDANS_WeaponTrace::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (bSpawnTrail && MeshComp)
	{
		MDCombatTrailLocal::StopTrails(MeshComp, this);
	}
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (Owner && Owner->HasAuthority())
	{
		if (bUseCppTrace)
		{
			UMDCombatStatics::EndWeaponTrace(Owner);
		}
		else
		{
			MDCombatNotifyLocal::CallCombatFunction(Owner, EndFunctionName);
		}
	}
}

void UMDANS_SwordTrail::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	MDCombatTrailLocal::StartTrails(MeshComp, this, true, TraceHand, FallbackTrail, RotationOffset, LocationOffset);
}

void UMDANS_SwordTrail::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (MeshComp)
	{
		MDCombatTrailLocal::StopTrails(MeshComp, this);
	}
}
void UMDANS_WeaponNiagara::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
	if (!World || !Template || World->GetNetMode() == NM_DedicatedServer || !MDCombatDebug::TrailsEnabled())
	{
		return;
	}
	using namespace MDCombatTrailLocal;
	StopTrails(MeshComp, this);

	// The weapon actor attached to the owner at SocketName.
	USceneComponent* WeaponRoot = nullptr;
	if (const AActor* Owner = MeshComp->GetOwner())
	{
		TArray<AActor*> Attached;
		Owner->GetAttachedActors(Attached, true);
		for (AActor* Actor : Attached)
		{
			if (Actor && Actor->GetAttachParentSocketName() == SocketName && Actor->GetRootComponent())
			{
				WeaponRoot = Actor->GetRootComponent();
				break;
			}
		}
	}

	UNiagaraComponent* Comp = nullptr;
	if (WeaponRoot)
	{
		Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(Template, WeaponRoot, NAME_None, LocationOffset, RotationOffset, Scale,
			EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None);
	}
	else if (MeshComp->DoesSocketExist(SocketName))
	{
		// Editor preview (no owner weapons) or a mesh that carries the socket: same as the stock notify.
		Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(Template, MeshComp, SocketName, LocationOffset, RotationOffset, Scale,
			EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None);
	}
	if (Comp)
	{
		Active().FindOrAdd(FTrailKey{ MeshComp, this }).Comps.Add(Comp);
	}
}

void UMDANS_WeaponNiagara::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (!MeshComp)
	{
		return;
	}
	using namespace MDCombatTrailLocal;
	FTrailEntry Entry;
	if (Active().RemoveAndCopyValue(FTrailKey{ MeshComp, this }, Entry))
	{
		for (const TWeakObjectPtr<UNiagaraComponent>& Comp : Entry.Comps)
		{
			if (UNiagaraComponent* C = Comp.Get())
			{
				if (bDestroyAtEnd)
				{
					C->DestroyComponent();
				}
				else
				{
					C->Deactivate();
				}
			}
		}
	}
}

void UMDANS_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetComboWindow(true);
	}
}

void UMDANS_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetComboWindow(false);
	}
}

void UMDANS_CancelWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetCancelWindow(true);
	}
}

void UMDANS_CancelWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetCancelWindow(false);
	}
}

void UMDANS_HyperArmor::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Owner))
	{
		if (Owner->HasAuthority())
		{
			Poise->SetHyperArmor(TotalDuration + 0.1f); // safety expiry if NotifyEnd never arrives
		}
	}
}

void UMDANS_HyperArmor::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Owner))
	{
		if (Owner->HasAuthority())
		{
			Poise->SetHyperArmor(0.0f);
		}
	}
}

void UMDANS_InputLock::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetInputLocked(true, TotalDuration);
	}
}

void UMDANS_InputLock::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMDComboComponent* Combo = MeshComp ? UMDComboComponent::FindFor(MeshComp->GetOwner()) : nullptr)
	{
		Combo->SetInputLocked(false);
	}
}
