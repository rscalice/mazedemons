#include "LockOn/MDLockOnComponent.h"

#include "AI/MDEnemyCharacter.h"
#include "AI/MDTargetableComponent.h"
#include "Blueprint/UserWidget.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "LockOn/MDLockOnReticleWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDPlayerController.h"
#include "MDPoiseComponent.h"
#include "MDUISettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDLockOn, Log, All);

namespace MDLockOnLocal
{
	// Subtle lock highlight: the same overlay material as the hit / tell flash (M_HitFlashOverlay, parameter HitFlash) at a low value.
	// The hit-flash overlay asset currently sits under Content/Fab/Staticshes (the path the combat code loads, /Game/Materials/Combat, does
	// not exist), so try both.
	const TCHAR* const HighlightOverlayPaths[] = {
		TEXT("/Game/Materials/Combat/M_HitFlashOverlay.M_HitFlashOverlay"),
		TEXT("/Game/Fab/Staticshes/Materials/Combat/M_HitFlashOverlay.M_HitFlashOverlay") };
	const FName HighlightParameter(TEXT("HitFlash"));
	constexpr float HighlightIntensity = 0.14f;
	TAutoConsoleVariable<int32> CVarDebug(TEXT("md.LockOn.Debug"), 0,
		TEXT("1 = draw every lock-on candidate with its score and log lock changes / breaks."), ECVF_Default);
	TAutoConsoleVariable<float> CVarAssist(TEXT("md.LockOn.Assist"), -1.0f,
		TEXT("Camera assist strength 0..1 overriding the option (-1 = use the option)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarRange(TEXT("md.LockOn.Range"), 0.0f,
		TEXT("Lock-on candidate range in cm (0 = the 30 m default)."), ECVF_Default);

	TAutoConsoleVariable<float> CVarSelfTest(TEXT("md.LockOn.SelfTest"), 0.0f,
		TEXT("Seconds into the level at which the local lock-on locks the best target once and dumps its state (headless checks)."), ECVF_Default);

	// Autonomous soft lock (distances in cm, like md.LockOn.Range).
	TAutoConsoleVariable<int32> CVarSoft(TEXT("md.LockOn.Soft"), -1,
		TEXT("1 / 0 = force the autonomous soft lock on / off (-1 = use the option)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSoftRange(TEXT("md.LockOn.SoftRange"), 1200.0f,
		TEXT("Soft lock acquire range in cm (default 1200 = 12 m)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSoftBreakRange(TEXT("md.LockOn.SoftBreakRange"), 1500.0f,
		TEXT("Soft lock retargets at attack start beyond this distance in cm (default 1500 = 15 m). The server validates against it."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSoftTimeout(TEXT("md.LockOn.SoftTimeout"), 4.0f,
		TEXT("Seconds without an attack before the soft lock drops."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSoftViewMargin(TEXT("md.LockOn.SoftViewMargin"), 0.10f,
		TEXT("Fraction of the viewport the soft lock's in-view test is widened by on every side."), ECVF_Default);

	constexpr float ScanInterval = 0.1f;
	constexpr float ReassertInterval = 1.5f;
	constexpr float ChestLift = 20.0f; // actor origin of a character is the capsule centre

	bool IsDebug() { return CVarDebug.GetValueOnGameThread() != 0; }

	const TCHAR* BreakName(EMDLockBreak Reason)
	{
		switch (Reason)
		{
		case EMDLockBreak::TargetDied: return TEXT("TargetDied");
		case EMDLockBreak::TargetInvalid: return TEXT("TargetInvalid");
		case EMDLockBreak::TooFar: return TEXT("TooFar");
		case EMDLockBreak::NoLineOfSight: return TEXT("NoLineOfSight");
		case EMDLockBreak::PlayerDied: return TEXT("PlayerDied");
		case EMDLockBreak::MenuOpened: return TEXT("MenuOpened");
		case EMDLockBreak::Manual: return TEXT("Manual");
		default: return TEXT("None");
		}
	}

	bool IsActorDead(const AActor* Actor)
	{
		if (const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Actor))
		{
			if (Enemy->IsEnemyDead())
			{
				return true;
			}
		}
		const APawn* Pawn = Cast<APawn>(Actor);
		return Pawn && UMDGameplayStatics::IsPawnDead(Pawn);
	}
}

UMDLockOnComponent::UMDLockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after the controller applied this frame's look input
	SetIsReplicatedByDefault(true);                  // a component needs this to send RPCs (Server_SetLockTarget)
}

UMDLockOnComponent* UMDLockOnComponent::FindFor(const AActor* ControllerOrPawn)
{
	if (!ControllerOrPawn)
	{
		return nullptr;
	}
	if (const APawn* Pawn = Cast<APawn>(ControllerOrPawn))
	{
		ControllerOrPawn = Pawn->GetController();
	}
	return ControllerOrPawn ? ControllerOrPawn->FindComponentByClass<UMDLockOnComponent>() : nullptr;
}

UMDLockOnComponent* UMDLockOnComponent::GetLocal(const UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC && PC->IsLocalController() ? PC->FindComponentByClass<UMDLockOnComponent>() : nullptr;
}

APlayerController* UMDLockOnComponent::GetPC() const { return Cast<APlayerController>(GetOwner()); }
bool UMDLockOnComponent::IsLocal() const { const APlayerController* PC = GetPC(); return PC && PC->IsLocalController(); }
APawn* UMDLockOnComponent::GetPawn() const { const APlayerController* PC = GetPC(); return PC ? PC->GetPawn() : nullptr; }

bool UMDLockOnComponent::IsMenuOpen() const
{
	const AMDPlayerController* PC = Cast<AMDPlayerController>(GetOwner());
	return PC && PC->IsAnyMenuOpen();
}

bool UMDLockOnComponent::IsPlayerDead() const
{
	const APawn* Pawn = GetPawn();
	return !Pawn || UMDGameplayStatics::IsPawnDead(Pawn);
}

FVector UMDLockOnComponent::ChestOf(const AActor* Actor)
{
	return Actor ? Actor->GetActorLocation() + FVector(0.0, 0.0, MDLockOnLocal::ChestLift) : FVector::ZeroVector;
}

AActor* UMDLockOnComponent::GetLockTarget() const
{
	if (AActor* Local = LockTarget.Get())
	{
		return Local;
	}
	return ServerTarget.Get();
}

bool UMDLockOnComponent::GetLockAimPoint(FVector& OutPoint) const
{
	const AActor* Target = GetLockTarget();
	if (!Target)
	{
		return false;
	}
	OutPoint = ChestOf(Target);
	return true;
}

bool UMDLockOnComponent::IsHardLock() const
{
	if (LockTarget.Get())
	{
		return bLockIsHard;
	}
	return ServerTarget.Get() && !bServerSoft;
}

bool UMDLockOnComponent::IsSoftEnabled() const
{
	const int32 Override = MDLockOnLocal::CVarSoft.GetValueOnGameThread();
	return Override >= 0 ? Override != 0 : GetDefault<UMDLocalSettings>()->bSoftLockOn;
}

void UMDLockOnComponent::RefreshTuning()
{
	const float RangeOverride = MDLockOnLocal::CVarRange.GetValueOnGameThread();
	Tuning.MaxRange = RangeOverride > 0.0f ? RangeOverride : FMDLockOnTuning().MaxRange;
	Tuning.BreakRange = RangeOverride > 0.0f ? RangeOverride * 1.2f : FMDLockOnTuning().BreakRange; // the override moves the break distance with it
	Tuning.SoftRange = FMath::Max(MDLockOnLocal::CVarSoftRange.GetValueOnGameThread(), 1.0f);
	Tuning.SoftBreakRange = FMath::Max(MDLockOnLocal::CVarSoftBreakRange.GetValueOnGameThread(), Tuning.SoftRange);
	Tuning.SoftTimeout = FMath::Max(MDLockOnLocal::CVarSoftTimeout.GetValueOnGameThread(), 0.0f);
	Tuning.SoftViewMargin = FMath::Clamp(MDLockOnLocal::CVarSoftViewMargin.GetValueOnGameThread(), 0.0f, 1.0f);
}

bool UMDLockOnComponent::IsLockable(const AActor* Actor) const
{
	const APawn* Player = GetPawn();
	const APawn* Pawn = Cast<APawn>(Actor);
	if (!Player || !Pawn || Pawn == Player || !UMDTargetableComponent::IsActorTargetable(Pawn) || MDLockOnLocal::IsActorDead(Pawn))
	{
		return false;
	}
	return UMDGameplayStatics::GetTeamId(Pawn) != UMDGameplayStatics::GetTeamId(Player) && UMDGameplayStatics::CanDamage(Player, Pawn);
}

bool UMDLockOnComponent::HasLineOfSight(const AActor* Target, const FVector& From) const
{
	const UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return false;
	}
	// Only level geometry occludes: other pawns, and actors attached to the player or the target (weapons, armor, shields), do not.
	// A plain test trace used to be blocked by the target's own weapon actor or by the player's, so no enemy ever passed.
	const APawn* Player = GetPawn();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDLockOnLOS), false, Player);
	Params.AddIgnoredActor(Target);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, From, ChestOf(Target), ECC_Visibility, Params);
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}
		const AActor* HitActor = Hit.GetActor();
		bool bAttachedToEndpoint = false;
		for (const AActor* Parent = HitActor ? HitActor->GetAttachParentActor() : nullptr; Parent && !bAttachedToEndpoint; Parent = Parent->GetAttachParentActor())
		{
			bAttachedToEndpoint = Parent == Target || Parent == Player;
		}
		if (!FMDLockOnRules::IsLineOfSightOccluder(Cast<APawn>(HitActor) != nullptr, bAttachedToEndpoint))
		{
			continue;
		}
		return false;
	}
	return true;
}

void UMDLockOnComponent::BeginPlay()
{
	Super::BeginPlay();
	Tuning.MaxRange = 3000.0f;
}

void UMDLockOnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearHighlight();	if (Reticle)
	{
		Reticle->RemoveFromParent(); // only when the controller itself goes away
		Reticle = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Candidates and lock state
// ---------------------------------------------------------------------------------------------------------------------

bool UMDLockOnComponent::CanProjectTarget(const AActor* Target, FVector2D& OutPixel) const
{
	const APlayerController* PC = GetPC();
	return PC && Target && PC->ProjectWorldLocationToScreen(ChestOf(Target), OutPixel, false);
}

void UMDLockOnComponent::ApplyHighlight(AActor* Target)
{
	if (!Target)
	{
		return;
	}
	if (!HighlightMID)
	{
		for (const TCHAR* Path : MDLockOnLocal::HighlightOverlayPaths)
		{
			if (UMaterialInterface* Overlay = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				HighlightMID = UMaterialInstanceDynamic::Create(Overlay, this);
				break;
			}
		}
	}
	if (!HighlightMID)
	{
		UE_LOG(LogMDLockOn, Warning, TEXT("LockOn: highlight overlay material M_HitFlashOverlay not found"));
		return;
	}
	const float Intensity = MDLockOnLocal::HighlightIntensity * (GetDefault<UMDLocalSettings>()->VFXLevel == EMDVFXLevel::Reduced ? 0.5f : 1.0f);
	HighlightMID->SetScalarParameterValue(MDLockOnLocal::HighlightParameter, Intensity);
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Target);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		// Local only: overlay materials are not replicated. A hit or tell flash saves this as its "previous" overlay and restores it after.
		if (Mesh && Mesh->GetSkeletalMeshAsset() && !Mesh->GetOverlayMaterial())
		{
			Mesh->SetOverlayMaterial(HighlightMID);
			HighlightMeshes.Add(Mesh);
		}
	}
}

void UMDLockOnComponent::ClearHighlight()
{
	if (HighlightMID)
	{
		HighlightMID->SetScalarParameterValue(MDLockOnLocal::HighlightParameter, 0.0f); // covers a flash that still holds this MID as its previous overlay
	}
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Weak : HighlightMeshes)
	{
		if (USkeletalMeshComponent* Mesh = Weak.Get())
		{
			if (Mesh->GetOverlayMaterial() == HighlightMID)
			{
				Mesh->SetOverlayMaterial(nullptr);
			}
		}
	}
	HighlightMeshes.Reset();
}

void UMDLockOnComponent::ScanCandidates()
{
	Candidates.Reset();
	CandidateActors.Reset();
	const APlayerController* PC = GetPC();
	const APawn* Pawn = GetPawn();
	UWorld* World = GetWorld();
	if (!PC || !Pawn || !World)
	{
		return;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Forward = CamRot.Vector();
	const float Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.0f;
	const UMDAbilitySystemComponent* MyASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Pawn);
	FMDScanStats Stats;

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (Other == Pawn)
		{
			continue;
		}
		++Stats.Pawns;
		if (!IsLockable(Other))
		{
			++Stats.NotLockable;
			continue;
		}
		const float Dist = static_cast<float>(FVector::Dist(Other->GetActorLocation(), Pawn->GetActorLocation()));
		if (Dist > Tuning.MaxRange)
		{
			++Stats.OutOfRange;
			continue;
		}
		const FVector Chest = ChestOf(Other);
		if (!HasLineOfSight(Other, CamLoc))
		{
			++Stats.NoLOS;
			continue;
		}
		const FVector To = (Chest - CamLoc).GetSafeNormal();
		const float Dot = static_cast<float>(FVector::DotProduct(Forward, To));
		FMDLockOnCandidate C;
		C.Id = Candidates.Num();
		C.DistanceCm = Dist;
		C.AngleFromCentreDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
		FVector2D Pixel(0, 0);
		const bool bProjected = PC->ProjectWorldLocationToScreen(Chest, Pixel, false);
		C.ScreenX = static_cast<float>(Pixel.X);
		C.bOnScreen = bProjected && Pixel.X >= 0.0 && Pixel.Y >= 0.0 && Pixel.X <= SizeX && Pixel.Y <= SizeY;
		C.bWithinEdgeSlack = Dot > 0.0f && C.AngleFromCentreDeg <= Fov * 0.5f + Tuning.EdgeSlackDeg;
		if (const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Other))
		{
			C.bThreatening = Enemy->bThreatening && Enemy->ThreatTarget == Pawn;
		}
		// The instigator is server state: only the host knows it, a client's threat flag already covers the attack that is coming.
		C.bRecentlyDamagedMe = MyASC && MyASC->GetLastDamageInstigator() == Other && MyASC->GetTimeSinceLastDamage() < 3.0;
		Candidates.Add(C);
		CandidateActors.Add(Other);
	}
	LastScanSummary = FString::Printf(TEXT("pawns=%d notLockable=%d outOfRange=%d noLOS=%d candidates=%d"), Stats.Pawns, Stats.NotLockable, Stats.OutOfRange, Stats.NoLOS, Candidates.Num());

	if (MDLockOnLocal::IsDebug())
	{
#if ENABLE_DRAW_DEBUG
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (const AActor* Actor = CandidateActors[i].Get())
			{
				DrawDebugString(World, ChestOf(Actor) + FVector(0, 0, 60), FString::Printf(TEXT("%.2f%s"), FMDLockOnRules::Score(Candidates[i], Tuning),
					Candidates[i].bOnScreen ? TEXT("") : TEXT(" off")), nullptr, FColor::White, MDLockOnLocal::ScanInterval, false, 1.0f);
			}
		}
#endif
	}
}

void UMDLockOnComponent::SetLockTarget(AActor* NewTarget, bool bSendToServer, bool bHard)
{
	AActor* Old = LockTarget.Get();
	if (Old == NewTarget)
	{
		if (NewTarget && bLockIsHard != bHard)
		{
			// Same enemy, new mode (hard key on a soft target, or the reverse): no new target, but the server must learn the mode.
			bLockIsHard = bHard;
			UE_LOG(LogMDLockOn, Display, TEXT("LockOn: %s lock on %s"), bHard ? TEXT("hard") : TEXT("soft"), *NewTarget->GetName());
			NoLOSSeconds = 0.0f;
			if (bSendToServer)
			{
				Server_SetLockTarget(NewTarget, !bHard);
			}
		}
		return;
	}
	LockTarget = NewTarget;
	bLockIsHard = NewTarget && bHard;
	NoLOSSeconds = 0.0f;
	bTargetHasLOS = true;
	LastReassertTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!NewTarget && Reticle)
	{
		Reticle->BeginSnapOff();
	}
	UE_LOG(LogMDLockOn, Display, TEXT("LockOn: %s lock %s -> %s"), NewTarget && !bHard ? TEXT("soft") : TEXT("hard"),
		Old ? *Old->GetName() : TEXT("none"), NewTarget ? *NewTarget->GetName() : TEXT("none"));
	ClearHighlight();
	if (NewTarget && IsLocal())
	{
		ApplyHighlight(NewTarget);
	}
	if (bSendToServer)
	{
		Server_SetLockTarget(NewTarget, NewTarget && !bHard); // only on a target / mode change; runs locally on a listen server host
	}
	OnLockTargetChanged.Broadcast(NewTarget, Old);
}

void UMDLockOnComponent::BreakLock(EMDLockBreak Reason)
{
	if (!LockTarget.Get() && Reason != EMDLockBreak::TargetDied)
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bWasHard = bLockIsHard;
	UE_LOG(LogMDLockOn, Display, TEXT("LockOn: unlock (%s, was %s)"), MDLockOnLocal::BreakName(Reason), bWasHard ? TEXT("hard") : TEXT("soft"));
	SetLockTarget(nullptr, true);
	// Unlocking a hard lock returns to soft mode: the next attack soft-locks again. A dead hard target also keeps the chain going.
	if (Reason == EMDLockBreak::TargetDied && !bWasHard)
	{
		return; // soft kills are chained by UpdateSoft
	}
	if (Reason == EMDLockBreak::TargetDied)
	{
		ScanCandidates();
		float BestScore = 0.0f;
		const int32 Best = FMDLockOnRules::PickBest(Candidates, Tuning, false, &BestScore);
		const float BestDist = Best != INDEX_NONE ? Candidates[Best].DistanceCm : -1.0f;
		if (FMDLockOnRules::ShouldAutoRetarget(Reason, static_cast<float>(Now - LastAttackTime), BestDist, Tuning))
		{
			SetLockTarget(CandidateActors[Best].Get(), true);
		}
	}
}

void UMDLockOnComponent::ClearLock()
{
	if (LockTarget.Get())
	{
		BreakLock(EMDLockBreak::Manual);
	}
}

bool UMDLockOnComponent::AcquireBestTarget()
{
	if (!IsLocal() || IsPlayerDead() || IsMenuOpen())
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: acquire refused (local=%d dead=%d menu=%d)"), IsLocal() ? 1 : 0, IsPlayerDead() ? 1 : 0, IsMenuOpen() ? 1 : 0);
		return false;
	}
	ScanCandidates();
	// The key on a soft-locked enemy makes that lock hard (same target) instead of jumping to a different one.
	if (AActor* Soft = LockTarget.Get())
	{
		if (!bLockIsHard && CandidateActors.Contains(Soft))
		{
			UE_LOG(LogMDLockOn, Display, TEXT("LockOn: hard lock promotes soft target %s"), *Soft->GetName());
			SetLockTarget(Soft, true, true);
			return true;
		}
	}
	const int32 Best = FMDLockOnRules::PickBest(Candidates, Tuning, true);
	if (Best == INDEX_NONE)
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: acquire found no target (%s, range=%.0f)"), *LastScanSummary, Tuning.MaxRange);
		return false;
	}
	UE_LOG(LogMDLockOn, Display, TEXT("LockOn: acquire picked candidate %d (%s)"), Best, *LastScanSummary);
	SetLockTarget(CandidateActors[Best].Get(), true);
	return LockTarget.Get() != nullptr;
}

void UMDLockOnComponent::OnLockPressed()
{
	const bool bHardLocked = LockTarget.Get() && bLockIsHard;
	UE_LOG(LogMDLockOn, Display, TEXT("LockOn: lock key pressed (hardLocked=%d softLocked=%d)"), bHardLocked ? 1 : 0, LockTarget.Get() && !bLockIsHard ? 1 : 0);
	if (!IsLocal() || IsPlayerDead() || IsMenuOpen())
	{
		return;
	}
	// The key is the HARD lock: it overrides a soft lock, and unlocking returns to soft mode.
	const bool bHold = GetDefault<UMDLocalSettings>()->bLockOnHold;
	if (bHold)
	{
		bHoldLocked = true;
		if (!bHardLocked)
		{
			AcquireBestTarget();
		}
		return;
	}
	if (bHardLocked)
	{
		ClearLock();
	}
	else if (!AcquireBestTarget())
	{
#if !UE_BUILD_SHIPPING
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(reinterpret_cast<uintptr_t>(this)), 1.5f, FColor::Silver,
				FString::Printf(TEXT("Lock-on: no target in range (%.0f m, on screen, in view)"), Tuning.MaxRange / 100.0f));
		}
#endif
	}
}

void UMDLockOnComponent::OnLockReleased()
{
	if (bHoldLocked && GetDefault<UMDLocalSettings>()->bLockOnHold)
	{
		ClearLock();
	}
	bHoldLocked = false;
}

void UMDLockOnComponent::CycleTarget(int32 Direction)
{
	AActor* Current = LockTarget.Get();
	UE_LOG(LogMDLockOn, Display, TEXT("LockOn: cycle %d (locked=%d hard=%d)"), Direction, Current ? 1 : 0, bLockIsHard ? 1 : 0);
	if (!Current || !bLockIsHard || !IsLocal() || IsPlayerDead() || IsMenuOpen())
	{
		return;
	}
	ScanCandidates();
	int32 CurrentId = INDEX_NONE;
	for (int32 i = 0; i < CandidateActors.Num(); ++i)
	{
		if (CandidateActors[i].Get() == Current)
		{
			CurrentId = i;
			break;
		}
	}
	const int32 Next = FMDLockOnRules::PickCycle(Candidates, CurrentId, Direction);
	if (Next != INDEX_NONE && CandidateActors[Next].Get() != Current)
	{
		SetLockTarget(CandidateActors[Next].Get(), true);
	}
}

void UMDLockOnComponent::NotifyPlayerAttacked()
{
	if (!IsLocal())
	{
		return;
	}
	LastAttackTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RefreshTuning();
	if (IsPlayerDead() || IsMenuOpen() || !FMDLockOnRules::ShouldSoftAcquire(IsSoftEnabled(), LockTarget.Get() && bLockIsHard))
	{
		return; // a hard lock stays; soft lock off
	}
	AcquireSoftTarget(true);
}

void UMDLockOnComponent::BuildSoftCandidates(TArray<FMDSoftCandidate>& Out, TArray<TWeakObjectPtr<AActor>>& OutActors, bool& bOutHasInput) const
{
	Out.Reset();
	OutActors.Reset();
	bOutHasInput = false;
	const APlayerController* PC = GetPC();
	const APawn* Pawn = GetPawn();
	UWorld* World = GetWorld();
	if (!PC || !Pawn || !World)
	{
		return;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	const FVector2D View(SizeX, SizeY);
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Input = FVector(Pawn->GetLastMovementInputVector().X, Pawn->GetLastMovementInputVector().Y, 0.0).GetSafeNormal();
	bOutHasInput = !Input.IsNearlyZero();

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (Other == Pawn || !IsLockable(Other))
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - Pawn->GetActorLocation();
		const float Dist = static_cast<float>(ToOther.Size());
		if (Dist > Tuning.SoftBreakRange) // SoftRange is applied by the picker; the wider band lets the current target be judged
		{
			continue;
		}
		FVector2D Pixel(0, 0);
		const bool bProjected = PC->ProjectWorldLocationToScreen(ChestOf(Other), Pixel, false);
		const bool bInView = FMDLockOnRules::IsInView(Pixel, bProjected, View, Tuning.SoftViewMargin);
		if (!bInView || !HasLineOfSight(Other, CamLoc)) // level geometry only; other pawns and attached weapons never occlude
		{
			continue;
		}
		FMDSoftCandidate C;
		C.Id = Out.Num();
		C.DistanceCm = Dist;
		C.CentreOffset = FMDLockOnRules::CentreOffset(Pixel, View);
		C.bInView = true;
		const FVector Planar = FVector(ToOther.X, ToOther.Y, 0.0).GetSafeNormal();
		if (bOutHasInput && !Planar.IsNearlyZero())
		{
			C.InputAngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Input, Planar)), -1.0f, 1.0f)));
		}
		Out.Add(C);
		OutActors.Add(Other);
	}
}

bool UMDLockOnComponent::AcquireSoftTarget(bool bAtAttackStart)
{
	if (!IsLocal() || IsPlayerDead() || IsMenuOpen())
	{
		return false;
	}
	RefreshTuning();
	TArray<FMDSoftCandidate> Soft;
	TArray<TWeakObjectPtr<AActor>> SoftActors;
	bool bHasInput = false;
	BuildSoftCandidates(Soft, SoftActors, bHasInput);

	AActor* Current = LockTarget.Get();
	int32 CurrentId = INDEX_NONE;
	for (int32 i = 0; i < SoftActors.Num(); ++i)
	{
		if (SoftActors[i].Get() == Current)
		{
			CurrentId = i;
			break;
		}
	}
	if (Current && !bLockIsHard)
	{
		// Keep the soft target unless the retarget rules say otherwise (dead / out of view / beyond 15 m / input steering elsewhere).
		FMDSoftRetargetInput In;
		In.bTargetValid = CurrentId != INDEX_NONE && IsLockable(Current); // not in the list = out of view, no LOS or too far
		In.bInView = CurrentId != INDEX_NONE;
		In.DistanceCm = CurrentId != INDEX_NONE ? Soft[CurrentId].DistanceCm : 0.0f;
		In.bHasMoveInput = bHasInput;
		In.TargetInputAngleDeg = CurrentId != INDEX_NONE ? FMath::Max(Soft[CurrentId].InputAngleDeg, 0.0f) : 0.0f;
		In.bOtherInInputCone = FMDLockOnRules::HasCandidateInInputCone(Soft, CurrentId, Tuning);
		if (!FMDLockOnRules::ShouldSoftRetarget(In, Tuning))
		{
			return true;
		}
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: soft retarget from %s (valid=%d inView=%d dist=%.0f input=%d angle=%.0f)"), *Current->GetName(),
			In.bTargetValid ? 1 : 0, In.bInView ? 1 : 0, In.DistanceCm, In.bHasMoveInput ? 1 : 0, In.TargetInputAngleDeg);
	}
	else if (Current)
	{
		return true; // hard lock: never replaced by a soft pick
	}

	const int32 Best = FMDLockOnRules::PickSoftBest(Soft, Tuning, bHasInput);
	if (Best == INDEX_NONE)
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: soft acquire found no target (%d in view, range=%.0f, %s)"), Soft.Num(), Tuning.SoftRange,
			bAtAttackStart ? TEXT("attack") : TEXT("chain"));
		if (Current && !bLockIsHard)
		{
			DropSoft(TEXT("no candidate"));
		}
		return false;
	}
	AActor* NewTarget = SoftActors[Best].Get();
	if (NewTarget != Current)
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: soft acquire %s dist=%.0f centre=%.2f input=%.0f (%s)"), *NewTarget->GetName(), Soft[Best].DistanceCm,
			Soft[Best].CentreOffset, Soft[Best].InputAngleDeg, bAtAttackStart ? TEXT("attack") : TEXT("chain"));
		SetLockTarget(NewTarget, true, false);
	}
	return true;
}

void UMDLockOnComponent::DropSoft(const TCHAR* Why)
{
	if (LockTarget.Get() && !bLockIsHard)
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: soft drop %s (%s)"), *LockTarget.Get()->GetName(), Why);
		SetLockTarget(nullptr, true);
	}
}

// Per-frame upkeep of a soft lock: timeout, death (chain to the next best in view), invalid. Never touches the camera.
void UMDLockOnComponent::UpdateSoft()
{
	const AActor* Target = LockTarget.Get();
	if (!Target || bLockIsHard)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (IsPlayerDead() || IsMenuOpen())
	{
		DropSoft(TEXT("player dead / menu"));
	}
	else if (!IsSoftEnabled())
	{
		DropSoft(TEXT("option off"));
	}
	else if (FMDLockOnRules::IsSoftExpired(static_cast<float>(Now - LastAttackTime), Tuning))
	{
		DropSoft(TEXT("timeout"));
	}
	else if (!IsLockable(Target))
	{
		UE_LOG(LogMDLockOn, Display, TEXT("LockOn: soft target %s died / invalid: chaining"), *Target->GetName());
		SetLockTarget(nullptr, true);
		AcquireSoftTarget(false);
	}
}

AActor* UMDLockOnComponent::GetWarpOverrideTarget(bool bHasMoveInput, const FVector& InputDir, float WarpRange) const
{
	AActor* Target = GetLockTarget();
	const APawn* Pawn = GetPawn();
	if (!Target || !Pawn || !IsLockable(Target))
	{
		return nullptr;
	}
	return FMDLockOnRules::UseLockedTargetForWarp(bHasMoveInput, InputDir, Target->GetActorLocation() - Pawn->GetActorLocation(), WarpRange, Tuning) ? Target : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------------------------------------------------

bool UMDLockOnComponent::ValidateTargetOnServer(const AActor* Target, bool bSoft) const
{
	const APawn* Player = GetPawn();
	const UWorld* World = GetWorld();
	if (!Target || !Player || !World)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const bool bAlive = Cast<APawn>(Target) && UMDTargetableComponent::IsActorTargetable(Target) && !MDLockOnLocal::IsActorDead(Target);
	const bool bHostile = Cast<APawn>(Target) && UMDGameplayStatics::GetTeamId(Target) != UMDGameplayStatics::GetTeamId(Player) && UMDGameplayStatics::CanDamage(Player, Target);
	const float Dist = static_cast<float>(FVector::Dist(Target->GetActorLocation(), Player->GetActorLocation()));
	if (HasLineOfSight(Target, Player->GetPawnViewLocation()))
	{
		ServerLastLOSTime = Now;
	}
	else if (ServerTarget.Get() != Target)
	{
		ServerLastLOSTime = -1000.0; // a new target must be visible now
	}
	return FMDLockOnRules::ValidateServerTarget(bAlive, bHostile, Dist, static_cast<float>(Now - ServerLastLOSTime), Tuning, bSoft);
}

void UMDLockOnComponent::Server_SetLockTarget_Implementation(AActor* Target, bool bSoft)
{
	RefreshTuning(); // the server's soft ranges come from the same cvars
	if (!Target)
	{
		ServerTarget = nullptr;
		bServerSoft = false;
		return;
	}
	if (ValidateTargetOnServer(Target, bSoft))
	{
		ServerTarget = Target;
		bServerSoft = bSoft;
	}
	else
	{
		if (MDLockOnLocal::IsDebug())
		{
			UE_LOG(LogTemp, Display, TEXT("LockOn: server rejected lock hint %s"), *Target->GetName());
		}
		ServerTarget = nullptr; // an invalid hint never leaves a stale lock behind
		bServerSoft = false;
	}
}

void UMDLockOnComponent::ServerUpkeep(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !ServerTarget.Get())
	{
		return;
	}
	ServerUpkeepTimer += DeltaTime;
	if (ServerUpkeepTimer < 0.25f)
	{
		return;
	}
	ServerUpkeepTimer = 0.0f;
	const AActor* Target = ServerTarget.Get();
	const APawn* Player = GetPawn();
	const float BreakRange = bServerSoft ? Tuning.SoftBreakRange + Tuning.ServerRangeTolerance : Tuning.BreakRange;
	const bool bFar = Player && FVector::Dist(Target->GetActorLocation(), Player->GetActorLocation()) > BreakRange;
	if (!Player || bFar || MDLockOnLocal::IsActorDead(Target) || MDLockOnLocal::IsActorDead(Player) || !UMDTargetableComponent::IsActorTargetable(Target))
	{
		ServerTarget = nullptr;
		bServerSoft = false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Local per-frame work
// ---------------------------------------------------------------------------------------------------------------------

float UMDLockOnComponent::GetAssistStrength() const
{
	const float Override = MDLockOnLocal::CVarAssist.GetValueOnGameThread();
	return Override >= 0.0f ? FMath::Clamp(Override, 0.0f, 1.0f) : FMath::Clamp(GetDefault<UMDLocalSettings>()->LockOnAssistStrength, 0.0f, 1.0f);
}

void UMDLockOnComponent::UpdateBreakRules(float DeltaTime)
{
	const AActor* Target = LockTarget.Get();
	const APawn* Pawn = GetPawn();
	if (!Target)
	{
		return; // a target destroyed while locked is handled in TickComponent
	}
	const APlayerController* PC = GetPC();
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	FMDLockOnBreakInput In;
	In.bTargetAlive = !MDLockOnLocal::IsActorDead(Target);
	In.bTargetValid = !In.bTargetAlive || IsLockable(Target);
	In.DistanceCm = Pawn ? static_cast<float>(FVector::Dist(Target->GetActorLocation(), Pawn->GetActorLocation())) : 0.0f;
	In.bHasLOS = bTargetHasLOS;
	In.bPlayerDead = IsPlayerDead();
	In.bMenuOpen = IsMenuOpen();
	const EMDLockBreak Reason = FMDLockOnRules::EvaluateBreak(In, DeltaTime, Tuning, NoLOSSeconds);
	if (Reason != EMDLockBreak::None)
	{
		BreakLock(Reason);
	}
}

void UMDLockOnComponent::UpdateFlick(float DeltaTime)
{
	const APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	const float StickX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	const int32 Direction = Flick.Update(LockTarget.Get() ? StickX : 0.0f, DeltaTime);
	if (Direction != 0)
	{
		CycleTarget(Direction);
	}
}

void UMDLockOnComponent::UpdateCameraAssist(float DeltaTime)
{
	APlayerController* PC = GetPC();
	const AActor* Target = LockTarget.Get();
	FVector2D Pixel;
	if (!FMDLockOnRules::ShouldAssistCamera(bLockIsHard)) // a soft lock never touches the camera
	{
		bHaveAssistRotation = false;
		return;
	}
	if (!PC || !Target || IsMenuOpen() || !CanProjectTarget(Target, Pixel))
	{
		bHaveAssistRotation = false;
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FRotator Current = PC->GetControlRotation();
	if (bHaveAssistRotation && !Current.Equals(LastAssistedRotation, 0.05f))
	{
		LastInputTime = Now; // something other than this assist moved the camera: the player. Pause.
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	const float Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.0f;
	const FVector2D Error = FMDLockOnRules::ScreenErrorDegrees(Pixel, FVector2D(SizeX, SizeY), Fov, Tuning);
	const float Since = static_cast<float>(Now - LastInputTime);
	const float Strength = GetAssistStrength();
	const float DYaw = FMDLockOnRules::AssistStep(Error.X, Strength, Since, DeltaTime, Tuning);
	const float DPitch = FMDLockOnRules::AssistStep(Error.Y, Strength, Since, DeltaTime, Tuning);
	if (FMath::Abs(DYaw) + FMath::Abs(DPitch) > 1.0e-4f)
	{
		FRotator Next = Current;
		Next.Yaw += DYaw;
		const float Pitch = FRotator::NormalizeAxis(Current.Pitch);
		Next.Pitch = FMDLockOnRules::ClampAssistPitch(Pitch, Pitch + DPitch, Tuning);
		PC->SetControlRotation(Next);
	}
	LastAssistedRotation = PC->GetControlRotation();
	bHaveAssistRotation = true;
}

void UMDLockOnComponent::UpdateReticle()
{
	APlayerController* PC = GetPC();
	const AActor* Target = LockTarget.Get();
	if (!Reticle)
	{
		if (!Target || !PC)
		{
			return;
		}
		Reticle = CreateWidget<UMDLockOnReticleWidget>(PC, UMDLockOnReticleWidget::StaticClass());
		if (!Reticle)
		{
			return;
		}
		Reticle->SetVisibility(ESlateVisibility::HitTestInvisible);
		Reticle->AddToViewport(41); // above the threat arrows (40), below the game menu (90)
	}
	FVector2D Pixel;
	if (!Target || !CanProjectTarget(Target, Pixel))
	{
		if (Reticle->IsLockedState())
		{
			Reticle->SetHidden(); // behind the camera: nothing to draw, keep the lock
		}
		return;
	}
	float Poise = -1.0f;
	if (const UMDPoiseComponent* PoiseComp = UMDPoiseComponent::FindFor(Target))
	{
		Poise = PoiseComp->GetMaxPoise() > 0.0f ? PoiseComp->GetPoiseFraction() : -1.0f;
	}
	const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Target);
	const bool bThreat = Enemy && Enemy->bThreatening && Enemy->ThreatTarget == GetPawn();
	Reticle->SetLocked(Pixel, Poise, bThreat, GetDefault<UMDLocalSettings>()->LockOnReticleScale, !bLockIsHard);
	if (!bLockIsHard)
	{
		Reticle->SetNeighbours(false, FVector2D::ZeroVector, false, FVector2D::ZeroVector); // cycling hints are a hard-lock thing
		return;
	}

	int32 Left = INDEX_NONE;
	int32 Right = INDEX_NONE;
	int32 CurrentId = INDEX_NONE;
	for (int32 i = 0; i < CandidateActors.Num(); ++i)
	{
		if (CandidateActors[i].Get() == Target)
		{
			CurrentId = i;
			break;
		}
	}
	FMDLockOnRules::FindNeighbours(Candidates, CurrentId, Left, Right);
	FVector2D LeftPixel(0, 0);
	FVector2D RightPixel(0, 0);
	const bool bHasLeft = Left != INDEX_NONE && CanProjectTarget(CandidateActors[Left].Get(), LeftPixel);
	const bool bHasRight = Right != INDEX_NONE && CanProjectTarget(CandidateActors[Right].Get(), RightPixel);
	Reticle->SetNeighbours(bHasLeft, LeftPixel, bHasRight, RightPixel);
}

void UMDLockOnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	RefreshTuning();

	ServerUpkeep(DeltaTime);
	if (!World || !IsLocal())
	{
		return;
	}

	const float SelfTestAt = MDLockOnLocal::CVarSelfTest.GetValueOnGameThread();
	if (SelfTestAt > 0.0f && !bSelfTested && GetPawn() && World->GetTimeSeconds() > SelfTestAt)
	{
		bSelfTested = true;
		// Through the real input stack (Enhanced Input + the controller's key bindings), so a broken bind shows up here.
		if (APlayerController* PC = GetPC())
		{
			PC->InputKey(FInputKeyParams(EKeys::MiddleMouseButton, IE_Pressed, 1.0));
			PC->InputKey(FInputKeyParams(EKeys::MiddleMouseButton, IE_Released, 0.0));
		}
		DumpState();
	}
	if (SelfTestAt > 0.0f && bSelfTested && !bSelfShot && GEngine && World->GetTimeSeconds() > SelfTestAt + 3.0f)
	{
		bSelfShot = true; // screenshot (UI included) of the locked state, for checking the reticle and the highlight
		GEngine->Exec(GetWorld(), TEXT("Shot"));
	}

	const double Now = World->GetTimeSeconds();
	AActor* Target = LockTarget.Get();
	if (Target)
	{
		const APawn* Pawn = GetPawn();
		FVector CamLoc;
		FRotator CamRot;
		GetPC()->GetPlayerViewPoint(CamLoc, CamRot);
		ScanTimer += DeltaTime;
		if (!bLockIsHard)
		{
			UpdateSoft(); // timeout / death chain / invalid; no camera, no scan, no cycling
		}
		else
		{
			bTargetHasLOS = HasLineOfSight(Target, CamLoc);
			if (ScanTimer >= MDLockOnLocal::ScanInterval)
			{
				ScanTimer = 0.0;
				ScanCandidates();
			}
			UpdateBreakRules(DeltaTime);
			UpdateFlick(DeltaTime);
			UpdateCameraAssist(DeltaTime);
		}
		// Late join / dropped hint: tell the server again now and then (hard lock only; a soft lock sends on changes only).
		if (LockTarget.Get() && bLockIsHard && !GetOwner()->HasAuthority() && Now - LastReassertTime > MDLockOnLocal::ReassertInterval)
		{
			LastReassertTime = Now;
			Server_SetLockTarget(LockTarget.Get(), false);
		}
		(void)Pawn;
	}
	else
	{
		if (LockTarget.IsStale())
		{
			const bool bWasHard = bLockIsHard;
			LockTarget.Reset();
			if (Reticle)
			{
				Reticle->BeginSnapOff();
			}
			Server_SetLockTarget(nullptr, false);
			OnLockTargetChanged.Broadcast(nullptr, nullptr);
			if (bWasHard)
			{
				BreakLock(EMDLockBreak::TargetDied); // destroyed with the lock on it: counts as a kill for auto-retarget
				bLockIsHard = LockTarget.Get() != nullptr; // BreakLock may have hard-retargeted
			}
			else if (IsSoftEnabled() && !FMDLockOnRules::IsSoftExpired(static_cast<float>(Now - LastAttackTime), Tuning))
			{
				bLockIsHard = false;
				AcquireSoftTarget(false); // a destroyed soft target is a kill: keep the chain going
			}
		}
		bHaveAssistRotation = false;
		Flick.Update(0.0f, DeltaTime);
	}
	UpdateReticle();
}

void UMDLockOnComponent::DumpState() const
{
	const AActor* Target = GetLockTarget();
	UE_LOG(LogTemp, Display, TEXT("LockOn: local=%d locked=%s mode=%s server=%s candidates=%d noLOS=%.2f assist=%.2f range=%.0f soft=%d softRange=%.0f softBreak=%.0f softTimeout=%.1f"), IsLocal() ? 1 : 0,
		Target ? *Target->GetName() : TEXT("none"), !Target ? TEXT("-") : (IsHardLock() ? TEXT("hard") : TEXT("soft")),
		ServerTarget.Get() ? *ServerTarget.Get()->GetName() : TEXT("none"), Candidates.Num(),
		NoLOSSeconds, GetAssistStrength(), Tuning.MaxRange, IsSoftEnabled() ? 1 : 0, Tuning.SoftRange, Tuning.SoftBreakRange, Tuning.SoftTimeout);
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		UE_LOG(LogTemp, Display, TEXT("  [%d] %s score %.2f dist %.0f angle %.1f onScreen %d threat %d"), i,
			CandidateActors[i].Get() ? *CandidateActors[i].Get()->GetName() : TEXT("?"), FMDLockOnRules::Score(Candidates[i], Tuning),
			Candidates[i].DistanceCm, Candidates[i].AngleFromCentreDeg, Candidates[i].bOnScreen ? 1 : 0, Candidates[i].bThreatening ? 1 : 0);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Console: md.LockOn.* (md.LockOn.Debug / .Assist / .Range are variables above)
// ---------------------------------------------------------------------------------------------------------------------

namespace MDLockOnConsoleLocal
{
	FAutoConsoleCommandWithWorldAndArgs CmdDump(TEXT("md.LockOn.Dump"), TEXT("Log the lock-on state and the scored candidates."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::GetLocal(World))
			{
				Lock->DumpState();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdLock(TEXT("md.LockOn.Lock"), TEXT("Lock the best candidate now (same as pressing the lock key)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::GetLocal(World))
			{
				Lock->AcquireBestTarget();
				Lock->DumpState();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdSoftAcquire(TEXT("md.LockOn.SoftAcquire"), TEXT("Run the soft lock pick now, as an attack would (needs the soft lock on)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::GetLocal(World))
			{
				Lock->NotifyPlayerAttacked();
				Lock->DumpState();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdClear(TEXT("md.LockOn.Clear"), TEXT("Break the lock."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::GetLocal(World))
			{
				Lock->ClearLock();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdCycle(TEXT("md.LockOn.Cycle"), TEXT("md.LockOn.Cycle [1|-1]: cycle the lock right (default) or left."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::GetLocal(World))
			{
				Lock->CycleTarget(Args.Num() > 0 && FCString::Atoi(*Args[0]) < 0 ? -1 : 1);
			}
		}));
}
