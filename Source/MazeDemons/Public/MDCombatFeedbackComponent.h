#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDCombatTypes.h"
#include "MDCombatFeedbackComponent.generated.h"

class APlayerController;
class UCameraShakeBase;
class UCurveFloat;
class UForceFeedbackEffect;
class UMDDamageNumberWidget;
class UMDImpactSet;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UWorld;

// What the server tells the attacker about a hit it landed (Client_ConfirmHit).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDHitConfirm
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	TObjectPtr<AActor> Victim = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	bool bCrit = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	int32 HitStopFrames = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	float ShakeScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	EMDAttackKind Kind = EMDAttackKind::Light;

	UPROPERTY(BlueprintReadWrite, Category = "Hit")
	EMDHitReaction Reaction = EMDHitReaction::None;
};

// Per-flash bookkeeping (local).
USTRUCT()
struct FMDHitFlashState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Per-victim overlay material instance (drives the HitFlash scalar on the overlay).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverlayMID;

	// Meshes the overlay was put on and what overlay they had before.
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> OverlayMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PreviousOverlays;

	TWeakObjectPtr<AActor> Actor;
	double StartTime = 0.0;
};

// Cosmetic combat feedback for the LOCAL player (put it on the player controller, or on the pawn). Everything here is local and
// obeys UMDLocalSettings: hit-stop, camera shake, rumble, damage numbers, hit flash. The server never runs any of it.
//
// Hit-stop: by default the mesh animation of attacker and victim is slowed through GlobalAnimRateScale (the pose freezes, movement is
// untouched, so prediction cannot desync). Enable bUseActorTimeDilation to use AActor::CustomTimeDilation instead (also slows
// movement - only safe for non-predicted actors). Length is always clamped to 120 ms and never applied on a dedicated server.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDCombatFeedbackComponent();

	// ---- Hit-stop ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Stop", meta = (ClampMin = "0.001", ClampMax = "1"))
	float HitStopDilation = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Stop")
	bool bUseActorTimeDilation = false;

	// Floors applied to the combo step's HitStopFrames (60 fps frames) so hits are always felt. Still capped at 120 ms.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Stop", meta = (ClampMin = "0", ClampMax = "7"))
	int32 LightHitStopMinFrames = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Stop", meta = (ClampMin = "0", ClampMax = "7"))
	int32 HeavyHitStopMinFrames = 7;

	// ---- Camera shake and rumble ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	TSubclassOf<UCameraShakeBase> LightShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	TSubclassOf<UCameraShakeBase> HeavyShake;

	// The player was hit (scaled by VictimShakeScale).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	TSubclassOf<UCameraShakeBase> VictimShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "0"))
	float VictimShakeScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble")
	TObjectPtr<UForceFeedbackEffect> HitRumble;

	// Attacker rumble for heavy / charged hits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble")
	TObjectPtr<UForceFeedbackEffect> HeavyRumble;

	// Played on the victim for a stagger or worse.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble")
	TObjectPtr<UForceFeedbackEffect> StaggerRumble;

	// ---- Hit flash ----
	// Scalar material parameter set on every dynamic material of the hit mesh: 0 = normal, up to FlashPeak at the moment of the hit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Flash")
	FName HitFlashParameter = TEXT("HitFlash");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Flash", meta = (ClampMin = "0.01"))
	float HitFlashDuration = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Flash", meta = (ClampMin = "0"))
	float HitFlashPeak = 1.0f;

	// Overlay material put on every skeletal mesh of the victim for HitFlashDuration (needs a scalar parameter "HitFlash").
	// When set it replaces the per-slot material parameter approach (which needs every character material to have the parameter).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Flash")
	TObjectPtr<UMaterialInterface> HitFlashOverlayMaterial;

	// ---- Damage numbers (native, pooled, screen projected) ----
	// Disable if a Blueprint implements OnHitNumber instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Numbers")
	bool bNativeDamageNumbers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Numbers")
	TSubclassOf<UMDDamageNumberWidget> DamageNumberClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Numbers", meta = (ClampMin = "1", ClampMax = "64"))
	int32 DamageNumberPoolSize = 16;

	// Optional shape over normalised time (0..1 -> 0..1). Empty = quadratic falloff.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Flash")
	TObjectPtr<UCurveFloat> HitFlashCurve;

	// Impact effects for hits seen on this machine (the weapon's set is on the combo set; this is the fallback).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UMDImpactSet> DefaultImpactSet;

	// Play impact VFX / sound / decal natively from the impact set (OnImpact is still called afterwards for Blueprint extras).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	bool bNativeImpact = true;

	// ---- Server -> attacker ----
	// Unreliable; the attacker's hit-stop, shake, rumble and damage number.
	UFUNCTION(Client, Unreliable)
	void Client_ConfirmHit(const FMDHitConfirm& Confirm);

	// ---- Local entry points ----
	// Every machine, from the victim's replicated reaction: flash, impact hook, victim shake / rumble, other players' numbers.
	void NotifyReactionObserved(AActor* Victim, const FMDHitResultRep& Hit);

	// Runs the hit flash on Victim's skeletal meshes (also callable from Blueprint, e.g. when the player takes DoT).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void PlayHitFlash(AActor* Victim);

	// Slows the animation of the given actors for Seconds (clamped to 120 ms, scaled by options).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Combat")
	void ApplyHitStop(AActor* A, AActor* B, float Seconds);

	// ---- Blueprint hooks ----
	// A floating damage number should be shown (pool W_MDDamageNumber). Already filtered by the damage number option.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnHitNumber(float Damage, bool bCrit, FVector Location);

	// A hit flash started on Victim. Use it for a custom overlay instead of / besides the material parameter.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnHitFlash(AActor* Victim);

	// Spawn impact VFX / SFX / decal here (every machine that observes the hit). ImpactSet is the fallback set.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnImpact(const FMDHitResultRep& Hit, UMDImpactSet* ImpactSet);

	// Controller or pawn feedback component of the first local player on this machine, null on a dedicated server.
	static UMDCombatFeedbackComponent* FindLocal(const UWorld* World);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FHitStopEntry
	{
		TWeakObjectPtr<AActor> Actor;
		double EndRealTime = 0.0;
	};

	APlayerController* GetLocalController() const;
	bool IsLocalHere() const;
	double RealNow() const;
	void SetAnimScale(AActor* Actor, float Scale);
	void RestoreAll();
	void StartShake(TSubclassOf<UCameraShakeBase> Shake, float Scale);
	void PlayRumble(UForceFeedbackEffect* Effect);
	void DrawDebug();
	void LoadDefaultAssets();
	void RestoreOverlay(FMDHitFlashState& Flash);
	void ShowDamageNumber(float Damage, bool bCrit, const FVector& Location);
	void PlayNativeImpact(AActor* Victim, const FMDHitResultRep& Hit);
	void PlayHurtSound(AActor* Victim, const FMDHitResultRep& Hit);

	TArray<TWeakObjectPtr<class UDecalComponent>> ActiveDecals;
	double LastHurtSoundTime = -1000.0;

	UPROPERTY(Transient)
	TArray<FMDHitFlashState> Flashes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMDDamageNumberWidget>> DamageNumberPool;

	TArray<FHitStopEntry> HitStops;
};
