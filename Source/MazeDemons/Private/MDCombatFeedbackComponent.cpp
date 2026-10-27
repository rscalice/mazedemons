#include "MDCombatFeedbackComponent.h"

#include "MDCombatDebug.h"
#include "MDCombatRules.h"
#include "MDComboComponent.h"
#include "MDDamageNumberWidget.h"
#include "MDImpactMath.h"
#include "MDImpactSet.h"
#include "MDPoiseComponent.h"
#include "MDPoiseProfile.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "MDUISettings.h"
#include "AbilitySystemInterface.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SoftObjectPath.h"

namespace MDFeedbackLocal
{
	// Default asset locations. Loaded lazily (BeginPlay) so a missing asset is a quiet no-op instead of a startup error.
	const TCHAR* const LightShakePath = TEXT("/Game/Data/Combat/CS_Hit_Light.CS_Hit_Light_C");
	const TCHAR* const HeavyShakePath = TEXT("/Game/Data/Combat/CS_Hit_Heavy.CS_Hit_Heavy_C");
	const TCHAR* const LightRumblePath = TEXT("/Game/Data/Combat/FFE_Hit_Light.FFE_Hit_Light");
	const TCHAR* const HeavyRumblePath = TEXT("/Game/Data/Combat/FFE_Hit_Heavy.FFE_Hit_Heavy");
	const TCHAR* const FlashOverlayPath = TEXT("/Game/Materials/Combat/M_HitFlashOverlay.M_HitFlashOverlay");
	const TCHAR* const BladeImpactPath = TEXT("/Game/Data/Combat/DA_Impact_Blade.DA_Impact_Blade");

	template <typename T>
	T* LoadAsset(const TCHAR* Path)
	{
		return Cast<T>(FSoftObjectPath(Path).TryLoad());
	}

	TSubclassOf<UCameraShakeBase> LoadShake(const TCHAR* Path)
	{
		return Cast<UClass>(FSoftObjectPath(Path).TryLoad());
	}
}

UMDCombatFeedbackComponent::UMDCombatFeedbackComponent()
{
	DamageNumberClass = UMDDamageNumberWidget::StaticClass();
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
	SetIsReplicatedByDefault(true); // the Client_ConfirmHit RPC needs a replicated component
}

APlayerController* UMDCombatFeedbackComponent::GetLocalController() const
{
	AActor* Owner = GetOwner();
	APlayerController* PC = Cast<APlayerController>(Owner);
	if (!PC)
	{
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			PC = Cast<APlayerController>(Pawn->GetController());
		}
	}
	return (PC && PC->IsLocalController()) ? PC : nullptr;
}

bool UMDCombatFeedbackComponent::IsLocalHere() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer && GetLocalController() != nullptr;
}

double UMDCombatFeedbackComponent::RealNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() : 0.0;
}

UMDCombatFeedbackComponent* UMDCombatFeedbackComponent::FindLocal(const UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (UMDCombatFeedbackComponent* OnController = PC->FindComponentByClass<UMDCombatFeedbackComponent>())
		{
			return OnController;
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			if (UMDCombatFeedbackComponent* OnPawn = Pawn->FindComponentByClass<UMDCombatFeedbackComponent>())
			{
				return OnPawn;
			}
		}
	}
	return nullptr;
}

void UMDCombatFeedbackComponent::StartShake(TSubclassOf<UCameraShakeBase> Shake, float Scale)
{
	APlayerController* PC = GetLocalController();
	if (!PC || !PC->PlayerCameraManager || !Shake)
	{
		return;
	}
	const UMDLocalSettings* Settings = GetDefault<UMDLocalSettings>();
	const float Final = Scale * (Settings->ShakeScalePercent / 100.0f) * MDCombatDebug::ShakeScale();
	if (Final > KINDA_SMALL_NUMBER)
	{
		PC->PlayerCameraManager->StartCameraShake(Shake, Final);
	}
}

void UMDCombatFeedbackComponent::PlayRumble(UForceFeedbackEffect* Effect)
{
	APlayerController* PC = GetLocalController();
	if (PC && Effect && GetDefault<UMDLocalSettings>()->bRumbleEnabled)
	{
		PC->ClientPlayForceFeedback(Effect, FForceFeedbackParameters());
	}
}

void UMDCombatFeedbackComponent::Client_ConfirmHit_Implementation(const FMDHitConfirm& Confirm)
{
	if (!IsLocalHere())
	{
		return;
	}
	APlayerController* PC = GetLocalController();
	const UMDLocalSettings* Settings = GetDefault<UMDLocalSettings>();

	if (Settings->DamageNumberMode != EMDDamageNumberMode::Off && Confirm.Damage > 0.0f)
	{
		ShowDamageNumber(Confirm.Damage, Confirm.bCrit, Confirm.Location, Confirm.DamageType);
		OnHitNumber(Confirm.Damage, Confirm.bCrit, Confirm.Location);
	}
	if (Confirm.bNumberOnly)
	{
		return;
	}

	const int32 Frames = UMDCombatRules::EffectiveHitStopFrames(Confirm.HitStopFrames, Confirm.Kind, LightHitStopMinFrames, HeavyHitStopMinFrames);
	if (Frames > 0)
	{
		ApplyHitStop(PC->GetPawn(), Confirm.Victim, UMDCombatRules::HitStopSeconds(Frames, 1.0f));
	}

	const bool bHeavy = Confirm.Kind == EMDAttackKind::Heavy || Confirm.Kind == EMDAttackKind::Charged;
	StartShake(bHeavy && HeavyShake ? HeavyShake : LightShake, Confirm.ShakeScale);
	PlayRumble(bHeavy && HeavyRumble ? HeavyRumble : HitRumble);
}

void UMDCombatFeedbackComponent::LoadDefaultAssets()
{
	using namespace MDFeedbackLocal;
	if (!LightShake) { LightShake = LoadShake(LightShakePath); }
	if (!HeavyShake) { HeavyShake = LoadShake(HeavyShakePath); }
	if (!VictimShake) { VictimShake = LightShake; }
	if (!HitRumble) { HitRumble = LoadAsset<UForceFeedbackEffect>(LightRumblePath); }
	if (!HeavyRumble) { HeavyRumble = LoadAsset<UForceFeedbackEffect>(HeavyRumblePath); }
	if (!StaggerRumble) { StaggerRumble = HeavyRumble; }
	if (!HitFlashOverlayMaterial) { HitFlashOverlayMaterial = LoadAsset<UMaterialInterface>(FlashOverlayPath); }
	if (!DefaultImpactSet) { DefaultImpactSet = LoadAsset<UMDImpactSet>(BladeImpactPath); }
}

void UMDCombatFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_DedicatedServer)
	{
		LoadDefaultAssets();
	}
}

void UMDCombatFeedbackComponent::ShowDamageNumber(float Damage, bool bCrit, const FVector& Location, const FGameplayTag& DamageType)
{
	APlayerController* PC = GetLocalController();
	if (!bNativeDamageNumbers || !PC || !DamageNumberClass)
	{
		return;
	}
	UMDDamageNumberWidget* Widget = nullptr;
	for (UMDDamageNumberWidget* Pooled : DamageNumberPool)
	{
		if (Pooled && !Pooled->IsActive())
		{
			Widget = Pooled;
			break;
		}
	}
	if (!Widget && DamageNumberPool.Num() < DamageNumberPoolSize)
	{
		Widget = CreateWidget<UMDDamageNumberWidget>(PC, DamageNumberClass);
		if (Widget)
		{
			DamageNumberPool.Add(Widget);
		}
	}
	if (!Widget)
	{
		// Pool exhausted: recycle the oldest.
		for (UMDDamageNumberWidget* Pooled : DamageNumberPool)
		{
			if (Pooled && (!Widget || Pooled->GetStartTime() < Widget->GetStartTime()))
			{
				Widget = Pooled;
			}
		}
	}
	if (Widget)
	{
		Widget->Start(PC, Location + FVector(0.0f, 0.0f, 40.0f), Damage, bCrit, DamageType);
	}
}

void UMDCombatFeedbackComponent::NotifyReactionObserved(AActor* Victim, const FMDHitResultRep& Hit)
{
	if (!IsLocalHere() || !Victim)
	{
		return;
	}
	const APlayerController* PC = GetLocalController();
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	const UMDLocalSettings* Settings = GetDefault<UMDLocalSettings>();

	PlayHitFlash(Victim);
	if (bNativeImpact)
	{
		PlayNativeImpact(Victim, Hit);
		PlayHurtSound(Victim, Hit);
	}
	OnImpact(Hit, DefaultImpactSet);

	if (Victim == LocalPawn)
	{
		StartShake(VictimShake, VictimShakeScale);
		if (static_cast<int32>(Hit.Reaction) >= static_cast<int32>(EMDHitReaction::Stagger))
		{
			PlayRumble(StaggerRumble);
		}
	}

	// "All" mode also shows other people's numbers; the attacker's own come from Client_ConfirmHit.
	if (Settings->DamageNumberMode == EMDDamageNumberMode::All && Hit.Instigator != LocalPawn && Hit.Damage > 0.0f)
	{
		ShowDamageNumber(Hit.Damage, Hit.bCrit, FVector(Hit.ImpactPoint), Hit.DamageType);
		OnHitNumber(Hit.Damage, Hit.bCrit, FVector(Hit.ImpactPoint));
	}
}

void UMDCombatFeedbackComponent::PlayNativeImpact(AActor* Victim, const FMDHitResultRep& Hit)
{
	UWorld* World = GetWorld();
	if (!World || !Victim)
	{
		return;
	}
	const UMDImpactSet* Set = DefaultImpactSet;
	if (!Set)
	{
		return;
	}
	const FMDImpactEntry Entry = Set->FindEntry(Hit.Surface);
	const EMDVFXLevel Level = GetDefault<UMDLocalSettings>()->VFXLevel;

	FVector Point = FVector(Hit.ImpactPoint);
	if (Point.IsNearlyZero())
	{
		Point = Victim->GetActorLocation();
	}
	const AActor* Attacker = Hit.Instigator;
	// The replicated hit carries no normal: point the effect from attacker to victim.
	const FVector Dir = MDImpactMath::ImpactDirection(FVector::ZeroVector, Attacker ? Attacker->GetActorLocation() : Point, Victim->GetActorLocation());
	const FRotator Rot = Dir.Rotation();

	if (Entry.VFX)
	{
		const FVector Scale(MDImpactMath::VFXScaleForLevel(Entry.VFXScale, Level));
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Entry.VFX))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Niagara, Point, Rot, Scale, true, true, ENCPoolMethod::AutoRelease);
		}
		else if (UParticleSystem* Cascade = Cast<UParticleSystem>(Entry.VFX))
		{
			UGameplayStatics::SpawnEmitterAtLocation(World, Cascade, Point, Rot, Scale, true);
		}
	}

	if (Entry.Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Entry.Sound, Point, FMath::FRandRange(0.95f, 1.05f), FMath::FRandRange(0.95f, 1.05f));
	}

	// A creature is a pawn or any actor with an ability system (AMDTestDummy is a plain AActor, so IsA<APawn> alone skipped it).
	const bool bVictimIsCreature = Victim->IsA<APawn>() || Cast<IAbilitySystemInterface>(Victim) != nullptr;
	if (bVictimIsCreature)
	{
		PlayFloorBlood(Victim, Hit, Entry, Set);
	}

	// Decals only on world geometry (anything that is not a creature) and not on Reduced effects.
	if (Entry.Decal && MDImpactMath::AllowDecals(Level) && !bVictimIsCreature)
	{
		ActiveDecals.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& D) { return !D.IsValid(); });
		const int32 Evict = MDImpactMath::DecalsToEvict(ActiveDecals.Num(), Set->MaxDecalsPerZone);
		for (int32 i = 0; i < Evict && ActiveDecals.Num() > 0; ++i)
		{
			if (UDecalComponent* Old = ActiveDecals[0].Get())
			{
				Old->DestroyComponent();
			}
			ActiveDecals.RemoveAt(0);
		}
		if (Set->MaxDecalsPerZone > 0)
		{
			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Entry.Decal, Entry.DecalSize, Point, Rot, 10.0f))
			{
				ActiveDecals.Add(Decal);
			}
		}
	}
}

void UMDCombatFeedbackComponent::PlayFloorBlood(AActor* Victim, const FMDHitResultRep& Hit, const FMDImpactEntry& Entry, const UMDImpactSet* Set)
{
	UWorld* World = GetWorld();
	UNiagaraSystem* System = Entry.FloorVFX ? Cast<UNiagaraSystem>(Entry.FloorVFX) : nullptr;
	if (!World || !Victim)
	{
		return;
	}
	if (!System)
	{
		return;
	}
	if (!Set || Set->MaxDecalsPerZone <= 0)
	{
		return;
	}
	if (!MDImpactMath::AllowFloorBlood(GetDefault<UMDLocalSettings>()->VFXLevel))
	{
		return;
	}

	// Per-victim cooldown so rapid multi-hits do not spam.
	constexpr double FloorBloodCooldown = 0.3;
	const double Now = RealNow();
	if (const double* Last = LastFloorBloodTimes.Find(Victim))
	{
		if (!MDImpactMath::CooldownElapsed(Now, *Last, FloorBloodCooldown))
		{
			return;
		}
	}
	if (LastFloorBloodTimes.Num() > 32)
	{
		for (auto It = LastFloorBloodTimes.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || Now - It.Value() > 5.0)
			{
				It.RemoveCurrent();
			}
		}
	}

	// Trace down from slightly behind the victim (away from the attacker) onto static / dynamic world geometry, never pawns.
	const AActor* Attacker = Hit.Instigator;
	const FVector VictimLoc = Victim->GetActorLocation();
	const FVector Offset = MDImpactMath::FloorBloodOffset(Attacker ? Attacker->GetActorLocation() : VictimLoc - FVector::ForwardVector, VictimLoc, FMath::FRandRange(20.0, 60.0));
	const FVector Start = VictimLoc + Offset;
	const FVector End = Start - FVector(0.0, 0.0, 300.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDFloorBlood), false, Victim);
	Params.AddIgnoredActor(Attacker);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult FloorHit;
	if (!World->LineTraceSingleByObjectType(FloorHit, Start, End, ObjectParams, Params))
	{
		return;
	}
	if (Cast<APawn>(FloorHit.GetActor()))
	{
		return;
	}
	LastFloorBloodTimes.Add(Victim, Now);

	// FIFO cap: destroy the oldest spawned systems.
	ActiveFloorBlood.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& C) { return !C.IsValid(); });
	const int32 Evict = MDImpactMath::DecalsToEvict(ActiveFloorBlood.Num(), Set->MaxDecalsPerZone);
	for (int32 i = 0; i < Evict && ActiveFloorBlood.Num() > 0; ++i)
	{
		if (UNiagaraComponent* Old = ActiveFloorBlood[0].Get())
		{
			Old->DestroyComponent();
		}
		ActiveFloorBlood.RemoveAt(0);
	}

	const FVector Normal = FloorHit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : FloorHit.ImpactNormal;
	const FRotator Rot = FRotationMatrix::MakeFromZ(Normal).Rotator();
	const FVector Scale(MDImpactMath::VFXScaleForLevel(Entry.FloorVFXScale, GetDefault<UMDLocalSettings>()->VFXLevel));
	if (UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, FloorHit.ImpactPoint + Normal * 2.0f, Rot, Scale, true, true, ENCPoolMethod::None))
	{
		ActiveFloorBlood.Add(Spawned);
	}
}

void UMDCombatFeedbackComponent::PlayHurtSound(AActor* Victim, const FMDHitResultRep& Hit)
{
	if (!Victim || static_cast<int32>(Hit.Reaction) < static_cast<int32>(EMDHitReaction::Flinch))
	{
		return;
	}
	const UMDPoiseComponent* Poise = Victim->FindComponentByClass<UMDPoiseComponent>();
	const UMDPoiseProfile* Profile = Poise ? Poise->GetProfile() : nullptr;
	if (!Profile || !Profile->HurtSound || !MDImpactMath::CooldownElapsed(RealNow(), LastHurtSoundTime, 0.2))
	{
		return;
	}
	LastHurtSoundTime = RealNow();
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Profile->HurtSound, Victim->GetActorLocation(), FMath::FRandRange(0.95f, 1.05f), FMath::FRandRange(0.95f, 1.05f));
}

void UMDCombatFeedbackComponent::PlayHitFlash(AActor* Victim)
{
	if (!IsLocalHere() || !Victim)
	{
		return;
	}

	FMDHitFlashState* State = nullptr;
	for (FMDHitFlashState& Existing : Flashes)
	{
		if (Existing.Actor.Get() == Victim)
		{
			State = &Existing;
			break;
		}
	}
	if (!State)
	{
		State = &Flashes.AddDefaulted_GetRef();
		State->Actor = Victim;
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Victim);
		if (HitFlashOverlayMaterial)
		{
			// Overlay path: one MID per victim, laid over every skeletal mesh (body, armor, weapons). The previous overlay is kept
			// and restored when the flash ends.
			State->OverlayMID = UMaterialInstanceDynamic::Create(HitFlashOverlayMaterial, this);
			for (USkeletalMeshComponent* Mesh : Meshes)
			{
				if (Mesh && Mesh->GetSkeletalMeshAsset())
				{
					State->OverlayMeshes.Add(Mesh);
					State->PreviousOverlays.Add(Mesh->GetOverlayMaterial());
					Mesh->SetOverlayMaterial(State->OverlayMID);
				}
			}
			Meshes.Reset();
		}
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			const int32 SlotCount = Mesh->GetNumMaterials();
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				if (!Material)
				{
					continue;
				}
				UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
				if (!Dynamic)
				{
					Dynamic = Mesh->CreateAndSetMaterialInstanceDynamic(Slot);
				}
				if (Dynamic)
				{
					State->Materials.Add(Dynamic);
				}
			}
		}
	}
	State->StartTime = RealNow();
	OnHitFlash(Victim);
}

void UMDCombatFeedbackComponent::SetAnimScale(AActor* Actor, float Scale)
{
	if (!Actor)
	{
		return;
	}
	if (bUseActorTimeDilation)
	{
		Actor->CustomTimeDilation = Scale;
		return;
	}
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Actor);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		Mesh->GlobalAnimRateScale = Scale;
	}
}

void UMDCombatFeedbackComponent::ApplyHitStop(AActor* A, AActor* B, float Seconds)
{
	if (!IsLocalHere() || !GetDefault<UMDLocalSettings>()->bHitStopEnabled)
	{
		return;
	}
	const float Length = FMath::Clamp(Seconds * MDCombatDebug::HitStopScale(), 0.0f, UMDCombatRules::MaxHitStopSeconds);
	if (Length <= 0.0f)
	{
		return;
	}
	const double End = RealNow() + Length;
	AActor* Targets[2] = { A, B };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AActor* Target = Targets[Index];
		if (!Target || (Index == 1 && Target == A))
		{
			continue;
		}
		FHitStopEntry* Entry = HitStops.FindByPredicate([Target](const FHitStopEntry& E) { return E.Actor.Get() == Target; });
		if (!Entry)
		{
			Entry = &HitStops.AddDefaulted_GetRef();
			Entry->Actor = Target;
		}
		Entry->EndRealTime = FMath::Max(Entry->EndRealTime, End);
		SetAnimScale(Target, HitStopDilation);
	}
}

void UMDCombatFeedbackComponent::RestoreOverlay(FMDHitFlashState& Flash)
{
	for (int32 Index = 0; Index < Flash.OverlayMeshes.Num(); ++Index)
	{
		USkeletalMeshComponent* Mesh = Flash.OverlayMeshes[Index];
		if (IsValid(Mesh) && Mesh->GetOverlayMaterial() == Flash.OverlayMID)
		{
			Mesh->SetOverlayMaterial(Flash.PreviousOverlays.IsValidIndex(Index) ? Flash.PreviousOverlays[Index].Get() : nullptr);
		}
	}
	Flash.OverlayMeshes.Reset();
	Flash.PreviousOverlays.Reset();
}

void UMDCombatFeedbackComponent::RestoreAll()
{
	for (const FHitStopEntry& Entry : HitStops)
	{
		SetAnimScale(Entry.Actor.Get(), 1.0f);
	}
	HitStops.Reset();
	for (FMDHitFlashState& Flash : Flashes)
	{
		RestoreOverlay(Flash);
		for (UMaterialInstanceDynamic* Material : Flash.Materials)
		{
			if (Material)
			{
				Material->SetScalarParameterValue(HitFlashParameter, 0.0f);
			}
		}
	}
	Flashes.Reset();
}

void UMDCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreAll();
	Super::EndPlay(EndPlayReason);
}

void UMDCombatFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double Real = RealNow();

	for (int32 Index = HitStops.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = HitStops[Index].Actor.Get();
		if (!Actor)
		{
			HitStops.RemoveAtSwap(Index);
		}
		else if (Real >= HitStops[Index].EndRealTime)
		{
			SetAnimScale(Actor, 1.0f);
			HitStops.RemoveAtSwap(Index);
		}
	}

	if (Flashes.Num() > 0)
	{
		const float Peak = HitFlashPeak * (GetDefault<UMDLocalSettings>()->VFXLevel == EMDVFXLevel::Reduced ? 0.5f : 1.0f);
		const float Duration = FMath::Max(HitFlashDuration, 0.01f);
		for (int32 Index = Flashes.Num() - 1; Index >= 0; --Index)
		{
			FMDHitFlashState& Flash = Flashes[Index];
			const float T = static_cast<float>((Real - Flash.StartTime) / Duration);
			const bool bDone = T >= 1.0f || !Flash.Actor.IsValid();
			float Value = 0.0f;
			if (!bDone)
			{
				Value = HitFlashCurve ? HitFlashCurve->GetFloatValue(T) : UMDCombatRules::HitFlashEnvelope(T);
				Value *= Peak;
			}
			if (Flash.OverlayMID)
			{
				Flash.OverlayMID->SetScalarParameterValue(HitFlashParameter, Value);
			}
			if (bDone)
			{
				RestoreOverlay(Flash);
			}
			for (UMaterialInstanceDynamic* Material : Flash.Materials)
			{
				if (Material)
				{
					Material->SetScalarParameterValue(HitFlashParameter, Value);
				}
			}
			if (bDone)
			{
				Flashes.RemoveAtSwap(Index);
			}
		}
	}

	if (MDCombatDebug::IsDebugEnabled())
	{
		DrawDebug();
	}
}

void UMDCombatFeedbackComponent::DrawDebug()
{
	const APlayerController* PC = GetLocalController();
	if (!GEngine || !PC)
	{
		return;
	}
	const APawn* Pawn = PC->GetPawn();
	if (const UMDComboComponent* Combo = UMDComboComponent::FindFor(Pawn))
	{
		GEngine->AddOnScreenDebugMessage(917001, 0.0f, FColor::Cyan, Combo->GetDebugString());
	}
	if (const UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Pawn))
	{
		const FMDHitResultRep& Last = Poise->GetLastHit();
		GEngine->AddOnScreenDebugMessage(917002, 0.0f, FColor::Cyan,
			FString::Printf(TEXT("Self poise %.0f%% stunned %d last reaction %d (seq %d)"), Poise->GetPoiseFraction() * 100.0f,
				Poise->IsStunned() ? 1 : 0, static_cast<int32>(Last.Reaction), static_cast<int32>(Last.Seq)));
	}

	UWorld* World = GetWorld();
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDCombatFeedbackDebug), false, Pawn);
	if (World && World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.0f, ECC_Visibility, Params))
	{
		if (const UMDPoiseComponent* Target = UMDPoiseComponent::FindFor(Hit.GetActor()))
		{
			const FMDHitResultRep& Last = Target->GetLastHit();
			GEngine->AddOnScreenDebugMessage(917003, 0.0f, FColor::Orange,
				FString::Printf(TEXT("Target %s poise %.0f%% stunned %d last reaction %d"), *GetNameSafe(Hit.GetActor()),
					Target->GetPoiseFraction() * 100.0f, Target->IsStunned() ? 1 : 0, static_cast<int32>(Last.Reaction)));
		}
	}
}
