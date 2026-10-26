#include "MDComboComponent.h"

#include "MDAfterImageComponent.h"
#include "MDComboSet.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "TimerManager.h"
#include "MDPoiseComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "MDWarpTargeting.h"
#include "MotionWarpingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"

namespace
{
	const FName MDAttackWarpTarget(TEXT("AttackTarget"));
	constexpr float MDWarpConeHalfAngle = 60.0f;
	constexpr float MDWarpServerTolerance = 150.0f;
}

UMDComboComponent::UMDComboComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

UMDComboComponent* UMDComboComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDComboComponent>() : nullptr;
}

double UMDComboComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UMDComboComponent::IsAuthority() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

bool UMDComboComponent::IsOwnerLocal() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

bool UMDComboComponent::RunsStateMachine() const
{
	return IsAuthority() || IsOwnerLocal();
}

USkeletalMeshComponent* UMDComboComponent::GetMesh() const
{
	AActor* Owner = GetOwner();
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		return Character->GetMesh();
	}
	return Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void UMDComboComponent::SetComboSet(UMDComboSet* NewSet)
{
	if (ComboSet != NewSet)
	{
		ComboSet = NewSet;
		NotifyInterrupted();
	}
}

void UMDComboComponent::BeginPlay()
{
	Super::BeginPlay();
	TryBindInventory();
}

void UMDComboComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InventoryBindTimer);
	}
	if (UMDInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnEquipmentChanged.RemoveDynamic(this, &UMDComboComponent::HandleEquipmentChanged);
	}
	BoundInventory.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMDComboComponent::TryBindInventory()
{
	UWorld* World = GetWorld();
	if (!World || BoundInventory.IsValid())
	{
		return;
	}
	if (UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetOwner()))
	{
		BoundInventory = Inventory;
		Inventory->OnEquipmentChanged.AddUniqueDynamic(this, &UMDComboComponent::HandleEquipmentChanged);
		HandleEquipmentChanged(); // pick up whatever is already equipped
		return;
	}
	// Not there yet (PlayerState / possession arrives late on clients): retry for ~30 seconds.
	if (++InventoryBindAttempts < 120)
	{
		World->GetTimerManager().SetTimer(InventoryBindTimer, this, &UMDComboComponent::TryBindInventory, 0.25f, false);
	}
}

void UMDComboComponent::HandleEquipmentChanged()
{
	if (const UMDInventoryComponent* Inventory = BoundInventory.Get())
	{
		SetComboSet(Inventory->ResolveEquippedComboSet());
	}
}

bool UMDComboComponent::IsGateOpen(EMDComboInput Input) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	// No AC_Combat requirement here: the combo path stands on its own (bHasCombat = true).
	const EMDAttackBlock Block = UMDGameplayStatics::EvaluateAttackGate(true, UMDGameplayStatics::IsPawnDead(Pawn), Pawn->IsLocallyControlled(),
		UMDGameplayStatics::IsLocalMenuBlockingPawn(Pawn), true, UMDPoiseComponent::IsActorStunned(Pawn));
	if (Block != EMDAttackBlock::None)
	{
		return false;
	}
	if ((Input == EMDComboInput::Light || Input == EMDComboInput::Heavy) && IsInputLocked())
	{
		return false;
	}
	return true;
}

void UMDComboComponent::RequestInput(EMDComboInput Input, float HeldSeconds)
{
	if (Input == EMDComboInput::None || !ComboSet)
	{
		return;
	}
	if (!IsAuthority() && IsOwnerLocal())
	{
		ClientHint = BuildWarpHint();
		Server_RequestInput(Input, HeldSeconds, ClientHint.Yaw, ClientHint.Target.Get());
	}
	ProcessInput(Input, HeldSeconds);
}

void UMDComboComponent::ProcessInput(EMDComboInput Input, float HeldSeconds)
{
	if (Input == EMDComboInput::None || !ComboSet || !RunsStateMachine() || !IsGateOpen(Input))
	{
		return;
	}
	UMDCombatRules::BufferInput(State, Input, Now(), HeldSeconds);
	Execute(UMDCombatRules::Advance(State, ComboSet->MakeTuning(), EMDComboEvent::Poll, Now()));
}

void UMDComboComponent::Server_RequestInput_Implementation(EMDComboInput Input, float HeldSeconds, float FacingYaw, AActor* WarpTarget)
{
	ClientHint.bValid = FMath::IsFinite(FacingYaw);
	ClientHint.Yaw = ClientHint.bValid ? FacingYaw : 0.0f;
	ClientHint.Target = WarpTarget;
	ClientHint.Time = Now();
	ProcessInput(Input, FMath::Clamp(HeldSeconds, 0.0f, 5.0f));
}

void UMDComboComponent::Server_UpdateWarpHint_Implementation(float FacingYaw, AActor* WarpTarget)
{
	ClientHint.bValid = FMath::IsFinite(FacingYaw);
	ClientHint.Yaw = ClientHint.bValid ? FacingYaw : 0.0f;
	ClientHint.Target = WarpTarget;
	ClientHint.Time = Now();
	// The server may already have started that step from the older hint: redo the warp for it.
	if (ClientHint.bValid && State.bAttacking && bHasActiveStep && Now() - StepStartTime < 0.35)
	{
		ApplyWarpHint(ClientHint, ActiveStep);
	}
}

void UMDComboComponent::HeavyPressed()
{
	if (!ComboSet || !RunsStateMachine() || bHeavyDown || !IsGateOpen(EMDComboInput::Heavy))
	{
		return;
	}
	bHeavyDown = true;
	HeavyPressTime = Now();
	if (!IsAuthority() && IsOwnerLocal())
	{
		Server_HeavyPressed();
	}
	else
	{
		ServerHeavyPressTime = Now();
	}
	if (ComboSet->ChargeLoopMontage && bPlayMontagesDirectly)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (Anim->Montage_Play(ComboSet->ChargeLoopMontage) > 0.0f)
			{
				ChargeMontage = ComboSet->ChargeLoopMontage;
			}
		}
	}
	OnChargeBegan.Broadcast();
}

void UMDComboComponent::Server_HeavyPressed_Implementation()
{
	ServerHeavyPressTime = Now();
}

void UMDComboComponent::HeavyReleased()
{
	if (!bHeavyDown)
	{
		return;
	}
	bHeavyDown = false;
	const float Held = static_cast<float>(FMath::Clamp(Now() - HeavyPressTime, 0.0, 5.0));
	if (UAnimMontage* Charge = ChargeMontage.Get())
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			Anim->Montage_Stop(0.1f, Charge);
		}
		ChargeMontage.Reset();
	}
	OnChargeEnded.Broadcast();
	if (!IsAuthority() && IsOwnerLocal())
	{
		ClientHint = BuildWarpHint();
		Server_HeavyReleased(Held, ClientHint.Yaw, ClientHint.Target.Get());
	}
	ProcessInput(EMDComboInput::Heavy, Held);
}

void UMDComboComponent::Server_HeavyReleased_Implementation(float HeldSeconds, float FacingYaw, AActor* WarpTarget)
{
	ClientHint.bValid = FMath::IsFinite(FacingYaw);
	ClientHint.Yaw = ClientHint.bValid ? FacingYaw : 0.0f;
	ClientHint.Target = WarpTarget;
	ClientHint.Time = Now();
	// Trust the client's hold time only within 0.1 s of what the server measured between press and release.
	float Held = FMath::Clamp(HeldSeconds, 0.0f, 5.0f);
	if (ServerHeavyPressTime >= 0.0)
	{
		const float Measured = static_cast<float>(Now() - ServerHeavyPressTime);
		Held = FMath::Clamp(Held, FMath::Max(Measured - 0.1f, 0.0f), Measured + 0.1f);
	}
	ServerHeavyPressTime = -1.0;
	ProcessInput(EMDComboInput::Heavy, Held);
}

void UMDComboComponent::SetComboWindow(bool bOpen)
{
	if (!RunsStateMachine())
	{
		return;
	}
	bNotifyWindows = true;
	State.bInComboWindow = bOpen && State.bAttacking;
}

void UMDComboComponent::SetCancelWindow(bool bOpen)
{
	if (!RunsStateMachine())
	{
		return;
	}
	bNotifyWindows = true;
	State.bInCancelWindow = bOpen && State.bAttacking;
}

void UMDComboComponent::SetInputLocked(bool bLocked, float Duration)
{
	InputLockUntil = bLocked ? Now() + FMath::Max(Duration, 0.05f) + 0.1 : 0.0;
}

bool UMDComboComponent::IsInputLocked() const
{
	return Now() < InputLockUntil;
}

void UMDComboComponent::NotifyInterrupted()
{
	const bool bWasActive = State.bAttacking || State.Buffered != EMDComboInput::None || State.LightStep >= 0;
	UMDCombatRules::Advance(State, FMDComboTuning(), EMDComboEvent::Interrupted, Now());
	bHasActiveStep = false;
	bNotifyWindows = false;
	InputLockUntil = 0.0;
	bHeavyDown = false;
	SwingHits.Reset();
	ClearWarp();
	if (bWasActive)
	{
		OnComboReset.Broadcast();
	}
}

bool UMDComboComponent::CanDodgeNow() const
{
	return !State.bAttacking || State.bInCancelWindow;
}

void UMDComboComponent::BeginDodge(EMDDodgeDirection Direction, float SlideDuration)
{
	UMDCombatRules::ApplyDodge(State, Now());
	State.Buffered = EMDComboInput::None;
	bHasActiveStep = false;
	bNotifyWindows = false;
	SwingHits.Reset();
	ClearWarp();
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (UAnimMontage* Montage = CurrentMontage.Get())
		{
			Anim->Montage_Stop(0.1f, Montage);
		}
	}
	CurrentMontage.Reset();
	PlayDodgeCosmetics(Direction, SlideDuration);
	if (IsAuthority())
	{
		Multicast_Dodge(Direction, SlideDuration);
	}
}

void UMDComboComponent::Multicast_Dodge_Implementation(EMDDodgeDirection Direction, float SlideDuration)
{
	if (IsAuthority() || IsOwnerLocal())
	{
		return; // the server and the owning client already ran BeginDodge
	}
	PlayDodgeCosmetics(Direction, SlideDuration);
}

void UMDComboComponent::PlayDodgeCosmetics(EMDDodgeDirection Direction, float SlideDuration)
{
	if (ComboSet && bPlayMontagesDirectly)
	{
		if (UAnimMontage* Montage = ComboSet->GetDodgeMontage(Direction))
		{
			if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				Anim->Montage_Play(Montage);
			}
		}
	}
	if (UMDAfterImageComponent* AfterImage = UMDAfterImageComponent::FindFor(GetOwner()))
	{
		AfterImage->StartTrail(SlideDuration);
	}
}

void UMDComboComponent::Execute(EMDComboAction Action)
{
	switch (Action)
	{
	case EMDComboAction::PlayLight:
		StartStep(EMDAttackKind::Light, State.LightStep);
		break;
	case EMDComboAction::PlayHeavy:
		StartStep(EMDAttackKind::Heavy, 0);
		break;
	case EMDComboAction::PlayCharged:
		StartStep(EMDAttackKind::Charged, 0);
		break;
	case EMDComboAction::PlayDodgeAttack:
		StartStep(EMDAttackKind::DodgeAttack, 0);
		break;
	case EMDComboAction::Dodge:
		OnDodgeRequested.Broadcast();
		break;
	case EMDComboAction::Reset:
		OnComboReset.Broadcast();
		break;
	case EMDComboAction::None:
	default:
		break;
	}
}

void UMDComboComponent::StartStep(EMDAttackKind Kind, int32 Index)
{
	FMDAttackStep Step;
	if (!ComboSet || !ComboSet->GetStep(Kind, Index, Step))
	{
		// Nothing to play: give the state machine its attack-ended event so it does not wait forever.
		UMDCombatRules::Advance(State, ComboSet ? ComboSet->MakeTuning() : FMDComboTuning(), EMDComboEvent::AttackEnded, Now());
		return;
	}

	ActiveStep = Step;
	ActiveKind = Kind;
	ActiveIndex = Index;
	bHasActiveStep = true;
	bNotifyWindows = false;
	StepStartTime = Now();
	StepFallbackEnd = StepStartTime + FallbackStepDuration;
	SwingHits.Reset();
	CurrentMontage.Reset();

	ApplyStepWarp(Step); // before the montage so its first root-motion frame is already warped

	if (bPlayMontagesDirectly && Step.Montage)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (Anim->Montage_Play(Step.Montage, Step.PlayRate) > 0.0f)
			{
				if (Step.Section != NAME_None)
				{
					Anim->Montage_JumpToSection(Step.Section, Step.Montage);
					Anim->Montage_SetNextSection(Step.Section, NAME_None, Step.Montage); // the swing ends with its section
				}
				CurrentMontage = Step.Montage;
			}
		}
	}

	if (Step.bAfterImage)
	{
		if (UMDAfterImageComponent* AfterImage = UMDAfterImageComponent::FindFor(GetOwner()))
		{
			AfterImage->StartTrail(FallbackStepDuration);
		}
	}

	if (ActiveStep.SwingSound && GetOwner())
	{
		UGameplayStatics::PlaySoundAtLocation(this, ActiveStep.SwingSound, GetOwner()->GetActorLocation(), ComboSet ? ComboSet->SwingSoundVolume : 0.5f, ComboSet ? ComboSet->SwingSoundPitch : 1.0f); // swing whoosh hook (data only)
	}

	OnStepStarted.Broadcast(ActiveStep, Kind, Index);

	if (IsAuthority())
	{
		Multicast_PlayStep(Kind, Index);
	}
}

void UMDComboComponent::Multicast_PlayStep_Implementation(EMDAttackKind Kind, int32 Index)
{
	if (IsAuthority() || IsOwnerLocal() || !ComboSet)
	{
		return; // the server and the owning client already played it
	}
	FMDAttackStep Step;
	if (!ComboSet->GetStep(Kind, Index, Step))
	{
		return;
	}
	ActiveStep = Step;
	ActiveKind = Kind;
	ActiveIndex = Index;
	if (bPlayMontagesDirectly && Step.Montage)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (Anim->Montage_Play(Step.Montage, Step.PlayRate) > 0.0f && Step.Section != NAME_None)
			{
				Anim->Montage_JumpToSection(Step.Section, Step.Montage);
				Anim->Montage_SetNextSection(Step.Section, NAME_None, Step.Montage);
			}
		}
	}
	if (Step.bAfterImage)
	{
		if (UMDAfterImageComponent* AfterImage = UMDAfterImageComponent::FindFor(GetOwner()))
		{
			AfterImage->StartTrail(FallbackStepDuration);
		}
	}
	if (Step.SwingSound && GetOwner())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Step.SwingSound, GetOwner()->GetActorLocation(), ComboSet->SwingSoundVolume, ComboSet->SwingSoundPitch);
	}
	OnStepStarted.Broadcast(Step, Kind, Index);
}

void UMDComboComponent::FinishStep()
{
	UMDCombatRules::Advance(State, ComboSet ? ComboSet->MakeTuning() : FMDComboTuning(), EMDComboEvent::AttackEnded, Now());
	StepEndedTime = Now();
	CurrentMontage.Reset();
	ClearWarp();
	OnStepEnded.Broadcast();
}

// ---- Root motion / motion warping ----

float UMDComboComponent::GetMaxWarpRange() const
{
	float Range = 0.0f;
	if (ComboSet)
	{
		for (const FMDAttackStep& Step : ComboSet->LightChain)
		{
			Range = FMath::Max(Range, Step.WarpRange);
		}
		Range = FMath::Max(Range, FMath::Max(ComboSet->Heavy.WarpRange, ComboSet->Charged.WarpRange));
		Range = FMath::Max(Range, ComboSet->DodgeAttack.WarpRange);
	}
	return Range;
}

bool UMDComboComponent::IsValidWarpTarget(const AActor* Candidate) const
{
	const APawn* Pawn = Cast<APawn>(Candidate);
	if (!Pawn || Pawn == GetOwner() || UMDGameplayStatics::IsPawnDead(Pawn))
	{
		return false;
	}
	if (!UMDGameplayStatics::GetMDAbilitySystemComponent(Pawn) && !Pawn->FindComponentByClass<UMDPoiseComponent>())
	{
		return false;
	}
	return UMDGameplayStatics::GetTeamId(Pawn) != UMDGameplayStatics::GetTeamId(GetOwner()) && UMDGameplayStatics::CanDamage(GetOwner(), Pawn);
}

AActor* UMDComboComponent::FindWarpTarget(const FVector& Facing, float Range) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || Range <= 0.0f)
	{
		return nullptr;
	}
	const FVector Origin = Owner->GetActorLocation();
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDWarpTarget), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Range), Query);

	TArray<MDWarpTargeting::FCandidate> Candidates;
	TArray<AActor*> Actors;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (Actor && !Actors.Contains(Actor) && IsValidWarpTarget(Actor))
		{
			MDWarpTargeting::FCandidate Candidate;
			Candidate.Id = Actors.Num();
			Candidate.Location = Actor->GetActorLocation();
			Candidates.Add(Candidate);
			Actors.Add(Actor);
		}
	}
	for (const int32 Id : MDWarpTargeting::RankTargets(Origin, Facing, Range, MDWarpConeHalfAngle, Candidates))
	{
		AActor* Actor = Actors[Id];
		FCollisionQueryParams Sight(SCENE_QUERY_STAT(MDWarpSight), false, Owner);
		Sight.AddIgnoredActor(Actor);
		if (!World->LineTraceTestByChannel(Origin + FVector(0, 0, 50), Actor->GetActorLocation(), ECC_Visibility, Sight))
		{
			return Actor;
		}
	}
	return nullptr;
}

FMDWarpHint UMDComboComponent::BuildWarpHint() const
{
	FMDWarpHint Hint;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return Hint;
	}
	// Snap to input: the last movement input, else the camera (controller) yaw.
	FVector Facing = MDWarpTargeting::Planar(Pawn->GetLastMovementInputVector());
	if (Facing.IsNearlyZero())
	{
		Facing = FRotator(0.0, Pawn->GetControlRotation().Yaw, 0.0).Vector();
	}
	Hint.bValid = true;
	Hint.Time = Now();
	Hint.Yaw = MDWarpTargeting::YawDegrees(Facing);
	if (AActor* Target = FindWarpTarget(Facing, GetMaxWarpRange()))
	{
		Hint.Target = Target;
		Hint.Yaw = MDWarpTargeting::YawDegrees(Target->GetActorLocation() - Pawn->GetActorLocation());
	}
	return Hint;
}

void UMDComboComponent::ApplyStepWarp(const FMDAttackStep& Step)
{
	// Simulated proxies get replicated movement; AI pawns keep their own facing.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !RunsStateMachine() || !Pawn->IsPlayerControlled())
	{
		return;
	}
	FMDWarpHint Hint;
	if (IsOwnerLocal())
	{
		if (!IsAuthority() && ClientHint.bValid && Now() - ClientHint.Time < 0.1)
		{
			Hint = ClientHint; // just built for this input and already sent
		}
		else
		{
			Hint = BuildWarpHint(); // host, or a buffered step that starts later than its input
			if (!IsAuthority())
			{
				ClientHint = Hint;
				Server_UpdateWarpHint(Hint.Yaw, Hint.Target.Get());
			}
		}
	}
	else
	{
		Hint = (ClientHint.bValid && Now() - ClientHint.Time < 1.0) ? ClientHint : BuildWarpHint();
	}
	ApplyWarpHint(Hint, Step);
}

void UMDComboComponent::ApplyWarpHint(const FMDWarpHint& Hint, const FMDAttackStep& Step)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Hint.bValid)
	{
		return;
	}
	const FVector Origin = Character->GetActorLocation();
	AActor* Target = Hint.Target.Get();
	if (Target)
	{
		const float Tolerance = IsOwnerLocal() ? 0.0f : MDWarpServerTolerance; // the server's position lags the client's
		if (!IsValidWarpTarget(Target) || !MDWarpTargeting::IsTargetWithinReach(Origin, Target->GetActorLocation(), Step.WarpRange, Tolerance))
		{
			Target = nullptr;
		}
	}
	float Yaw = Hint.Yaw;
	if (Target)
	{
		Yaw = MDWarpTargeting::YawDegrees(Target->GetActorLocation() - Origin);
	}
	FRotator Rotation = Character->GetActorRotation();
	Rotation.Yaw = Yaw;
	Character->SetActorRotation(Rotation);

	UMotionWarpingComponent* Warping = Character->FindComponentByClass<UMotionWarpingComponent>();
	if (Target && Warping)
	{
		const UCapsuleComponent* Capsule = Target->FindComponentByClass<UCapsuleComponent>();
		const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 0.0f;
		const FVector Point = MDWarpTargeting::ComputeWarpPoint(Origin, Target->GetActorLocation(), Radius, Step.WarpStopDistance);
		Warping->AddOrUpdateWarpTargetFromLocationAndRotation(MDAttackWarpTarget, Point, FRotator(0.0, Yaw, 0.0));
		Character->SetAnimRootMotionTranslationScale(1.0f);
	}
	else
	{
		if (Warping)
		{
			Warping->RemoveWarpTarget(MDAttackWarpTarget);
		}
		Character->SetAnimRootMotionTranslationScale(FMath::Max(Step.RootMotionScale, 0.0f));
	}
	bWarpApplied = true;
}

void UMDComboComponent::ClearWarp()
{
	if (!bWarpApplied)
	{
		return;
	}
	bWarpApplied = false;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->SetAnimRootMotionTranslationScale(1.0f);
		if (UMotionWarpingComponent* Warping = Character->FindComponentByClass<UMotionWarpingComponent>())
		{
			Warping->RemoveWarpTarget(MDAttackWarpTarget);
		}
	}
}

void UMDComboComponent::UpdateFallbackWindows()
{
	if (bNotifyWindows || !bHasActiveStep)
	{
		return;
	}
	UAnimMontage* Montage = CurrentMontage.Get();
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	float Fraction = 0.0f;
	if (Montage && Anim)
	{
		float Start = 0.0f;
		float End = Montage->GetPlayLength();
		if (ActiveStep.Section != NAME_None)
		{
			const int32 SectionIndex = Montage->GetSectionIndex(ActiveStep.Section);
			if (SectionIndex != INDEX_NONE)
			{
				Montage->GetSectionStartAndEndTime(SectionIndex, Start, End);
			}
		}
		Fraction = (Anim->Montage_GetPosition(Montage) - Start) / FMath::Max(End - Start, KINDA_SMALL_NUMBER);
	}
	else
	{
		Fraction = static_cast<float>((Now() - StepStartTime) / FMath::Max(static_cast<double>(FallbackStepDuration), 0.01));
	}
	State.bInComboWindow = Fraction >= ActiveStep.ComboWindowStart && Fraction <= ActiveStep.ComboWindowEnd;
	State.bInCancelWindow = Fraction >= ActiveStep.CancelWindowStart;
}

void UMDComboComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Simulated proxies only play what the server multicasts; they never run the state machine. (Possession can arrive after
	// BeginPlay on a client, so this is checked every tick instead of disabling the tick.)
	if (!ComboSet || !RunsStateMachine())
	{
		return;
	}

	if (State.bAttacking)
	{
		UpdateFallbackWindows();

		bool bPlaying = Now() < StepFallbackEnd;
		if (UAnimMontage* Montage = CurrentMontage.Get())
		{
			const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			bPlaying = Anim && Anim->Montage_IsPlaying(Montage);
		}
		else if (!bPlayMontagesDirectly)
		{
			// Blueprint plays the montage: follow whatever montage is active on the mesh.
			const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			bPlaying = Now() < StepFallbackEnd || (Anim && Anim->IsAnyMontagePlaying());
		}
		if (!bPlaying && Now() - StepStartTime > 0.05)
		{
			FinishStep();
		}
	}

	if (State.Buffered != EMDComboInput::None && IsGateOpen(State.Buffered))
	{
		Execute(UMDCombatRules::Advance(State, ComboSet->MakeTuning(), EMDComboEvent::Poll, Now()));
	}
}

bool UMDComboComponent::GetActiveStep(FMDAttackStep& OutStep) const
{
	// Grace: a trace that lands a frame after the montage ended still belongs to that swing.
	if (!bHasActiveStep)
	{
		return false;
	}
	if (!State.bAttacking && Now() - StepEndedTime > 0.25)
	{
		return false;
	}
	OutStep = ActiveStep;
	return true;
}

EMDWeaponHand UMDComboComponent::GetActiveStepHand() const
{
	FMDAttackStep Step;
	return GetActiveStep(Step) ? Step.Hand : EMDWeaponHand::Main;
}

bool UMDComboComponent::TryRegisterSwingHit(const AActor* Target)
{
	if (!Target)
	{
		return false;
	}
	int32& Count = SwingHits.FindOrAdd(Target);
	if (Count >= FMath::Max(ActiveStep.MaxHitsPerTarget, 1))
	{
		return false;
	}
	++Count;
	return true;
}

FString UMDComboComponent::GetDebugString() const
{
	return FString::Printf(TEXT("Combo: step %d kind %d attacking %d window %d cancel %d lock %d buffer %d"),
		State.LightStep, static_cast<int32>(ActiveKind), State.bAttacking ? 1 : 0, State.bInComboWindow ? 1 : 0,
		State.bInCancelWindow ? 1 : 0, IsInputLocked() ? 1 : 0, static_cast<int32>(State.Buffered));
}
