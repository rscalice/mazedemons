#include "AI/MDEnemyBehaviourComponent.h"

#include "AI/MDAIQueries.h"
#include "AI/MDAIVoice.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AIController.h"
#include "MDBlinkComponent.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Ranged/MDRangedStatics.h"
#include "Sound/SoundBase.h"

namespace MDBehaviourComponentLocal
{
	struct FCueInfo
	{
		const TCHAR* Sound = nullptr; // /Game path of a Fab MonsterRoarsAndGrowls cue
		float Volume = 0.6f;
		float Flash = 0.0f;           // 0 = no flash
	};

	FCueInfo Info(EMDBehaviourCue Cue)
	{
		switch (Cue)
		{
		case EMDBehaviourCue::SwarmHowl: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/17_Bloodthirsty_Growl_Cue.17_Bloodthirsty_Growl_Cue"), 0.9f, 0.0f };
		case EMDBehaviourCue::Taunt: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/25_Warning_Growl_Cue.25_Warning_Growl_Cue"), 0.7f, 0.0f };
		case EMDBehaviourCue::FallBack: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/04_Signal_Roar_Cue.04_Signal_Roar_Cue"), 0.9f, 0.0f };
		case EMDBehaviourCue::AmbushGrowl: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/12_Ambush_Growl_Cue.12_Ambush_Growl_Cue"), 1.0f, 0.0f };
		case EMDBehaviourCue::FeintJerk: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/13_Tentative_Growl_Cue.13_Tentative_Growl_Cue"), 0.8f, 0.9f };
		case EMDBehaviourCue::FakeFlinch: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/22_Searching_Growl_Cue.22_Searching_Growl_Cue"), 0.6f, 0.7f };
		case EMDBehaviourCue::PreBlink: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/15_Haunting_Growl_Cue.15_Haunting_Growl_Cue"), 0.8f, 0.8f };
		case EMDBehaviourCue::BlinkArrive: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/16_Feral_Growl_Cue.16_Feral_Growl_Cue"), 0.7f, 0.0f };
		case EMDBehaviourCue::Brace: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/18_Rumbling_Growl_Cue.18_Rumbling_Growl_Cue"), 0.8f, 0.8f };
		case EMDBehaviourCue::ChargeScrape: return { TEXT("/Game/Fab/MonsterRoarsAndGrowls/cues/14_Guttural_Growl_Cue.14_Guttural_Growl_Cue"), 0.8f, 0.0f };
		case EMDBehaviourCue::RangedDraw: return { nullptr, 0.0f, 0.7f };
		default: return {};
		}
	}

	FVector Flat(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0);
	}
}

UMDEnemyBehaviourComponent::UMDEnemyBehaviourComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // ticks only while an action or a kick runs
	SetIsReplicatedByDefault(true); // for the cue multicast only; no replicated state
}

UMDEnemyBehaviourComponent* UMDEnemyBehaviourComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDEnemyBehaviourComponent>() : nullptr;
}

AMDEnemyCharacter* UMDEnemyBehaviourComponent::GetEnemy() const
{
	return Cast<AMDEnemyCharacter>(GetOwner());
}

double UMDEnemyBehaviourComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

EMDIntent UMDEnemyBehaviourComponent::IntentOfAction() const
{
	switch (Action)
	{
	case EMDBehaviourAction::Feint: return EMDIntent::Feint;
	case EMDBehaviourAction::PreBlink: return EMDIntent::Blink;
	case EMDBehaviourAction::ChargeLineUp:
	case EMDBehaviourAction::Charging:
	case EMDBehaviourAction::ChargeRecover: return EMDIntent::Charge;
	case EMDBehaviourAction::Draw: return EMDIntent::Kite;
	default: return EMDIntent::Idle;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Cues
// ---------------------------------------------------------------------------------------------------------------------

FSoftObjectPath UMDEnemyBehaviourComponent::GetCueSoundPath(EMDBehaviourCue Cue)
{
	const MDBehaviourComponentLocal::FCueInfo Info = MDBehaviourComponentLocal::Info(Cue);
	return Info.Sound ? FSoftObjectPath(Info.Sound) : FSoftObjectPath();
}

void UMDEnemyBehaviourComponent::PlayCue(EMDBehaviourCue Cue)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_PlayCue(Cue); // runs here too (listen server / standalone)
	}
}

void UMDEnemyBehaviourComponent::Multicast_PlayCue_Implementation(EMDBehaviourCue Cue)
{
	PlayCueLocal(Cue);
}

void UMDEnemyBehaviourComponent::PlayCueLocal(EMDBehaviourCue Cue)
{
	const UWorld* World = GetWorld();
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!World || !Enemy || World->GetNetMode() == NM_DedicatedServer || Enemy->IsEnemyDead())
	{
		return;
	}
	const MDBehaviourComponentLocal::FCueInfo Info = MDBehaviourComponentLocal::Info(Cue);
	if (Info.Sound)
	{
		TObjectPtr<USoundBase>* Cached = LoadedSounds.Find(static_cast<uint8>(Cue));
		USoundBase* Sound = Cached ? Cached->Get() : nullptr;
		if (!Sound)
		{
			Sound = LoadObject<USoundBase>(nullptr, Info.Sound, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Sound)
			{
				LoadedSounds.Add(static_cast<uint8>(Cue), Sound);
			}
		}
		if (Sound)
		{
			// Wind-up cues (feint jerk, brace, pre-blink) are tells; the rest are pack barks. Both go through the voice gate.
			const bool bTell = Cue == EMDBehaviourCue::FeintJerk || Cue == EMDBehaviourCue::Brace || Cue == EMDBehaviourCue::PreBlink;
			if (UMDAIVoiceSubsystem* Voice = UMDAIVoiceSubsystem::Get(World))
			{
				Voice->Play(bTell ? EMDVoiceKind::Tell : EMDVoiceKind::Bark, static_cast<int32>(Enemy->GetUniqueID()), Sound, Enemy->GetActorLocation(), Info.Volume);
			}
		}
	}
	if (Cue == EMDBehaviourCue::RangedDraw)
	{
		Enemy->PlayTellCue(true); // the archetype's own tell growl + flash
	}
	else if (Info.Flash > 0.0f)
	{
		Enemy->PlayTellCue(false);
	}
	if (Cue == EMDBehaviourCue::FeintJerk)
	{
		StartKick(1.0f, 0.2f);
	}
	else if (Cue == EMDBehaviourCue::FakeFlinch)
	{
		StartKick(1.4f, 0.3f);
	}
}

void UMDEnemyBehaviourComponent::StartKick(float Strength, float DurationSeconds)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	USkeletalMeshComponent* Mesh = Enemy ? Enemy->GetMesh() : nullptr;
	if (!Mesh || DurationSeconds <= 0.0f)
	{
		return;
	}
	if (!bKickBaseCaptured)
	{
		KickBaseLocation = Mesh->GetRelativeLocation();
		bKickBaseCaptured = true;
	}
	KickStrength = Strength;
	KickDuration = DurationSeconds;
	KickRemaining = DurationSeconds;
	UpdateTickEnabled();
}

void UMDEnemyBehaviourComponent::TickKick(float DeltaTime)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	USkeletalMeshComponent* Mesh = Enemy ? Enemy->GetMesh() : nullptr;
	if (!Mesh || KickRemaining <= 0.0f)
	{
		return;
	}
	KickRemaining = FMath::Max(KickRemaining - DeltaTime, 0.0f);
	const float Progress = 1.0f - KickRemaining / FMath::Max(KickDuration, 0.01f);
	const float Profile = FMath::Sin(Progress * PI) * KickStrength; // out and back
	// Mesh-relative (the capsule's space): back along the facing and a small dip.
	Mesh->SetRelativeLocation(KickBaseLocation + FVector(-14.0f * Profile, 0.0f, -4.0f * Profile));
	if (KickRemaining <= 0.0f)
	{
		Mesh->SetRelativeLocation(KickBaseLocation);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Grace
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBehaviourComponent::StartGrace(double Seconds, bool bInAmbushArrival)
{
	GraceEnd = Now() + FMath::Max(Seconds, 0.0);
	bAmbushArrival = bInAmbushArrival;
}

bool UMDEnemyBehaviourComponent::IsInGrace() const
{
	return MDBehaviour::IsInArrivalGrace(Now(), GraceEnd);
}

bool UMDEnemyBehaviourComponent::IsInAmbushArrival() const
{
	return bAmbushArrival && IsInGrace();
}

// ---------------------------------------------------------------------------------------------------------------------
// Tick plumbing
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBehaviourComponent::UpdateTickEnabled()
{
	SetComponentTickEnabled(Action != EMDBehaviourAction::None || KickRemaining > 0.0f);
}

void UMDEnemyBehaviourComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickKick(DeltaTime);
	if (GetOwner() && GetOwner()->HasAuthority() && Action != EMDBehaviourAction::None)
	{
		const AMDEnemyCharacter* Enemy = GetEnemy();
		if (!Enemy || Enemy->IsEnemyDead())
		{
			CancelAction();
		}
		else
		{
			const double InNow = Now();
			switch (Action)
			{
			case EMDBehaviourAction::Feint: TickFeint(InNow); break;
			case EMDBehaviourAction::PreBlink: TickPreBlink(InNow); break;
			case EMDBehaviourAction::ChargeLineUp:
			case EMDBehaviourAction::Charging:
			case EMDBehaviourAction::ChargeRecover: TickCharge(InNow, DeltaTime); break;
			case EMDBehaviourAction::Draw: TickDraw(InNow); break;
			default: break;
			}
		}
	}
	UpdateTickEnabled();
}

void UMDEnemyBehaviourComponent::EnterAction(EMDBehaviourAction NewAction)
{
	Action = NewAction;
	ActionStart = Now();
	UpdateTickEnabled();
}

void UMDEnemyBehaviourComponent::Finish(bool bCompleted)
{
	const EMDIntent Intent = IntentOfAction();
	Action = EMDBehaviourAction::None;
	ActionTarget.Reset();
	UpdateTickEnabled();
	OnActionEnded.Broadcast(Intent, bCompleted);
}

void UMDEnemyBehaviourComponent::CancelAction()
{
	if (Action == EMDBehaviourAction::None)
	{
		return;
	}
	if (Action == EMDBehaviourAction::Charging || Action == EMDBehaviourAction::ChargeLineUp || Action == EMDBehaviourAction::ChargeRecover)
	{
		RestoreChargeSpeed();
		LastChargeEnd = Now();
	}
	if (Action == EMDBehaviourAction::Feint)
	{
		if (AMDEnemyCharacter* Enemy = GetEnemy(); Enemy && Enemy->AttackComponent)
		{
			Enemy->AttackComponent->CancelAttack();
		}
	}
	Action = EMDBehaviourAction::None;
	ActionTarget.Reset();
	UpdateTickEnabled();
}

void UMDEnemyBehaviourComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreChargeSpeed();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Feint
// ---------------------------------------------------------------------------------------------------------------------

bool UMDEnemyBehaviourComponent::BeginFeint(int32 AttackIndex, AActor* Target, float TellTime, float DelayRoll)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority() || IsBusy() || !Enemy->AttackComponent || !Target)
	{
		return false;
	}
	if (!Enemy->AttackComponent->StartAttack(AttackIndex, Target, TellTime))
	{
		return false;
	}
	ActionTarget = Target;
	bFeintCancelled = false;
	// Cancel inside the wind-up, before the first trace window: the feint can never hit.
	FeintCancelAt = TellTime > 0.0f ? FMath::Min(MDBehaviour::FeintCancelAfter, TellTime * 0.6f) : MDBehaviour::FeintCancelAfter;
	FeintRecover = MDBehaviour::FeintDelay(DelayRoll);
	EnterAction(EMDBehaviourAction::Feint);
	return true;
}

void UMDEnemyBehaviourComponent::TickFeint(double InNow)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!bFeintCancelled)
	{
		if (InNow - ActionStart >= FeintCancelAt)
		{
			bFeintCancelled = true;
			if (Enemy && Enemy->AttackComponent)
			{
				Enemy->AttackComponent->CancelAttack();
			}
			PlayCue(EMDBehaviourCue::FeintJerk);
			ActionStart = InNow; // the real attack comes FeintRecover seconds after the jerk
		}
		return;
	}
	if (InNow - ActionStart >= FeintRecover)
	{
		Finish(true);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Blink
// ---------------------------------------------------------------------------------------------------------------------

bool UMDEnemyBehaviourComponent::CanBlinkNow(double InNow) const
{
	const AMDEnemyCharacter* Enemy = GetEnemy();
	return Enemy && Enemy->Blink && MDBehaviour::BlinkOffCooldown(InNow, LastBlinkTime) && Enemy->Blink->CanBlink();
}

void UMDEnemyBehaviourComponent::RefreshBlinkArrival(AActor* Target, double InNow)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Target || InNow < NextBlinkRefresh)
	{
		return;
	}
	NextBlinkRefresh = InNow + 0.6;
	MDBehaviour::FMDBlinkParams Params;
	Params.TargetLocation = Target->GetActorLocation();
	Params.TargetFacing = Target->GetActorForwardVector();
	Params.FromLocation = Enemy->GetActorLocation();
	bHasBlinkArrival = MDBehaviour::FindBlinkArrival(Params, MDAIQueries::MakeWorldEnvironment(Enemy->GetWorld(), Enemy, Target), BlinkArrival);
}

bool UMDEnemyBehaviourComponent::BeginBlink(AActor* Target)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority() || IsBusy() || !Enemy->Blink || !Target || !CanBlinkNow(Now()))
	{
		return false;
	}
	NextBlinkRefresh = 0.0;
	RefreshBlinkArrival(Target, Now());
	if (!bHasBlinkArrival)
	{
		return false;
	}
	ActionTarget = Target;
	if (AAIController* AI = Cast<AAIController>(Enemy->GetController()))
	{
		AI->StopMovement();
	}
	Enemy->FaceTarget(Target);
	PlayCue(EMDBehaviourCue::PreBlink); // 0.4 s of warning at the origin
	EnterAction(EMDBehaviourAction::PreBlink);
	return true;
}

void UMDEnemyBehaviourComponent::TickPreBlink(double InNow)
{
	if (InNow - ActionStart < MDBehaviour::BlinkPreDelay)
	{
		return;
	}
	AMDEnemyCharacter* Enemy = GetEnemy();
	AActor* Target = ActionTarget.Get();
	if (!Enemy || !Target || UMDGameplayStatics::IsPawnDead(Cast<APawn>(Target)))
	{
		Finish(false);
		return;
	}
	// The world moved for 0.4 s: validate again (nav-valid, >= 2 m from the target, in a revealed area) before leaving.
	MDBehaviour::FMDBlinkParams Params;
	Params.TargetLocation = Target->GetActorLocation();
	Params.TargetFacing = Target->GetActorForwardVector();
	Params.FromLocation = Enemy->GetActorLocation();
	FVector Arrival;
	LastBlinkTime = InNow; // the 6 s cooldown starts at the attempt, so a failed blink cannot be spammed
	if (!MDBehaviour::FindBlinkArrival(Params, MDAIQueries::MakeWorldEnvironment(Enemy->GetWorld(), Enemy, Target), Arrival))
	{
		Finish(false);
		return;
	}
	if (const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
	{
		Arrival.Z += Capsule->GetScaledCapsuleHalfHeight();
	}
	if (Enemy->Blink && Enemy->Blink->TeleportTo(Arrival))
	{
		const FVector Face = Target->GetActorLocation() - Enemy->GetActorLocation();
		Enemy->SetActorRotation(FRotator(0.0, Face.Rotation().Yaw, 0.0));
		StartGrace(MDBehaviour::BlinkArrivalGrace);
		PlayCue(EMDBehaviourCue::BlinkArrive);
		Finish(true);
	}
	else
	{
		Finish(false);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Flank cache
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBehaviourComponent::RefreshFlankPoint(AActor* Target, double InNow)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Target || InNow < NextFlankRefresh)
	{
		return;
	}
	NextFlankRefresh = InNow + 0.6;
	bHasFlankPoint = MDAIQueries::FindFlankPointFor(Enemy, Target, 350.0f, FlankPoint);
}

// ---------------------------------------------------------------------------------------------------------------------
// Charge
// ---------------------------------------------------------------------------------------------------------------------

bool UMDEnemyBehaviourComponent::CanChargeNow(double InNow) const
{
	return InNow - LastChargeEnd >= MDBehaviour::ChargeCooldown;
}

bool UMDEnemyBehaviourComponent::BeginCharge(AActor* Target)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority() || IsBusy() || !Target)
	{
		return false;
	}
	ActionTarget = Target;
	const FVector Self = Enemy->GetActorLocation();
	const FVector Goal = Target->GetActorLocation();
	AAIController* AI = Cast<AAIController>(Enemy->GetController());
	if (FVector::Dist2D(Self, Goal) >= MDBehaviour::ChargeLineUpDistance - 100.0f || !AI)
	{
		// Already far enough for a run-up.
		EnterAction(EMDBehaviourAction::ChargeLineUp);
		ActionStart = Now() - 10.0; // the line-up tick starts the charge at once
		PlayCue(EMDBehaviourCue::ChargeScrape);
		return true;
	}
	// Back up to a point on the far side of the target, facing it (the backstep + scrape tell).
	Enemy->FaceTarget(Target);
	AI->MoveToLocation(MDBehaviour::ChargeLineUpPoint(Goal, Self), 80.0f, true, true, true, false);
	PlayCue(EMDBehaviourCue::ChargeScrape);
	EnterAction(EMDBehaviourAction::ChargeLineUp);
	return true;
}

void UMDEnemyBehaviourComponent::RestoreChargeSpeed()
{
	if (!bChargeSpeedApplied)
	{
		return;
	}
	bChargeSpeedApplied = false;
	if (AMDEnemyCharacter* Enemy = GetEnemy())
	{
		if (UCharacterMovementComponent* Move = Enemy->GetCharacterMovement())
		{
			Move->MaxWalkSpeed = ChargeSavedSpeed;
			Move->StopMovementImmediately();
		}
	}
}

void UMDEnemyBehaviourComponent::EndCharge(bool bCancelledBySidestep)
{
	RestoreChargeSpeed();
	ChargeRecoverTime = bCancelledBySidestep ? MDBehaviour::ChargeCancelRecovery : MDBehaviour::ChargeRecovery;
	if (bCancelledBySidestep)
	{
		PlayCue(EMDBehaviourCue::ChargeScrape); // skids to a stop
	}
	EnterAction(EMDBehaviourAction::ChargeRecover);
}

void UMDEnemyBehaviourComponent::TickCharge(double InNow, float DeltaTime)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	AActor* Target = ActionTarget.Get();
	if (!Enemy || !Target)
	{
		Finish(false);
		return;
	}
	const FVector Self = Enemy->GetActorLocation();
	switch (Action)
	{
	case EMDBehaviourAction::ChargeLineUp:
	{
		AAIController* AI = Cast<AAIController>(Enemy->GetController());
		const bool bArrived = !AI || AI->GetMoveStatus() != EPathFollowingStatus::Moving;
		if (bArrived || InNow - ActionStart > 2.0)
		{
			if (AI)
			{
				AI->StopMovement();
			}
			// Commit: a straight line at where the target stands now.
			ChargeOrigin = Self;
			const FVector To = MDBehaviourComponentLocal::Flat(Target->GetActorLocation() - Self);
			ChargeDir = To.IsNearlyZero() ? Enemy->GetActorForwardVector() : To.GetSafeNormal();
			ChargeLength = FMath::Clamp(static_cast<float>(To.Size()) + 250.0f, 300.0f, MDBehaviour::ChargeMaxDistance + 250.0f);
			if (UCharacterMovementComponent* Move = Enemy->GetCharacterMovement())
			{
				ChargeSavedSpeed = Move->MaxWalkSpeed;
				Move->MaxWalkSpeed = ChargeSavedSpeed * MDBehaviour::ChargeSpeedMultiplier;
				bChargeSpeedApplied = true;
			}
			Enemy->FaceMovement();
			EnterAction(EMDBehaviourAction::Charging);
		}
		break;
	}
	case EMDBehaviourAction::Charging:
	{
		Enemy->AddMovementInput(ChargeDir, 1.0f);
		const float Travelled = static_cast<float>(FVector::Dist2D(Self, ChargeOrigin));
		const FVector TargetLoc = Target->GetActorLocation();
		const float TargetAlong = static_cast<float>(FVector::DotProduct(MDBehaviourComponentLocal::Flat(TargetLoc - ChargeOrigin), ChargeDir));
		const bool bTargetAhead = TargetAlong > Travelled;
		if (bTargetAhead && MDBehaviour::IsChargeCancelledBySidestep(ChargeOrigin, ChargeDir, TargetLoc))
		{
			EndCharge(true); // the player slid out of the line: the brute skids on and is open longer
		}
		else if (FVector::Dist2D(Self, TargetLoc) <= 150.0f || Travelled >= ChargeLength
			|| (InNow - ActionStart > 0.4 && Enemy->GetVelocity().SizeSquared2D() < 50.0 * 50.0))
		{
			EndCharge(false);
		}
		break;
	}
	case EMDBehaviourAction::ChargeRecover:
		if (InNow - ActionStart >= ChargeRecoverTime)
		{
			LastChargeEnd = InNow;
			Finish(true);
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Ranged draw
// ---------------------------------------------------------------------------------------------------------------------

bool UMDEnemyBehaviourComponent::BeginRangedShot(AActor* Target, float InDrawTime)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority() || IsBusy() || !Target || !Enemy->Archetype || !Enemy->Archetype->RangedData || !CanShootNow(Now()))
	{
		return false;
	}
	ActionTarget = Target;
	DrawTime = FMath::Max(InDrawTime, 0.0f);
	if (AAIController* AI = Cast<AAIController>(Enemy->GetController()))
	{
		AI->StopMovement();
	}
	Enemy->FaceTarget(Target);
	PlayCue(EMDBehaviourCue::RangedDraw);
	EnterAction(EMDBehaviourAction::Draw);
	return true;
}

void UMDEnemyBehaviourComponent::TickDraw(double InNow)
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	AActor* Target = ActionTarget.Get();
	if (!Enemy || !Target || UMDGameplayStatics::IsPawnDead(Cast<APawn>(Target)))
	{
		Finish(false);
		return;
	}
	if (InNow - ActionStart < DrawTime)
	{
		return;
	}
	const FVector Origin = Enemy->GetActorLocation() + FVector(0.0, 0.0, 60.0) + Enemy->GetActorForwardVector() * 50.0;
	const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(Enemy->GetUniqueID()), GetTypeHash(static_cast<int32>(InNow * 100.0))));
	UMDRangedStatics::FireFromData(Enemy, Enemy->Archetype->RangedData, Origin, Target->GetActorLocation(), 1.5f, 0.0f, Seed);
	const float Cooldown = Enemy->Archetype->Attacks.Num() > 0 ? Enemy->Archetype->Attacks[0].Cooldown : 2.0f;
	NextShotTime = InNow + Cooldown;
	Finish(true);
}

// ---------------------------------------------------------------------------------------------------------------------
// Brace
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBehaviourComponent::BeginBrace()
{
	AMDEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !Enemy->HasAuthority())
	{
		return;
	}
	BraceEnd = Now() + MDBehaviour::BraceDuration;
	if (Enemy->Poise)
	{
		Enemy->Poise->SetHyperArmor(static_cast<float>(MDBehaviour::BraceDuration));
	}
	PlayCue(EMDBehaviourCue::Brace);
}

bool UMDEnemyBehaviourComponent::IsBraced() const
{
	return Now() < BraceEnd;
}
