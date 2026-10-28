#include "AI/MDEnemyBrainComponent.h"

#include "AI/MDAISubsystem.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyBehaviourComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "HAL/IConsoleManager.h"
#include "AI/MDEnemyController.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "AI/MDPackRules.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

namespace MDBrainBehaviourLocal
{
	// md.AI.ForceIntent <Name>: every brain picks this intent (Swarm, Bait, Kite, Flank, Feint, Guard, Blink, Charge, ...)
	// whatever the scorer says, for PIE testing. "" / none / off clears it. Stagger and death still interrupt.
	TAutoConsoleVariable<FString> CVarForceIntent(TEXT("md.AI.ForceIntent"), TEXT(""),
		TEXT("Forces every enemy brain to this intent for testing (Swarm Bait Kite Flank Feint Guard Blink Charge Attack Chase HoldThreat Retreat). Empty or none clears it."),
		ECVF_Cheat);
}

EMDIntent UMDEnemyBrainComponent::GetForcedIntent()
{
	EMDIntent Out = EMDIntent::Idle;
	return MDBehaviour::ParseForcedIntent(MDBrainBehaviourLocal::CVarForceIntent.GetValueOnGameThread(), Out) ? Out : EMDIntent::Idle;
}

UMDEnemyBehaviourComponent* UMDEnemyBrainComponent::GetBehaviour() const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	return E ? E->Behaviour : nullptr;
}

bool UMDEnemyBrainComponent::GetFlankPoint(FVector& OutPoint) const
{
	const UMDEnemyBehaviourComponent* B = GetBehaviour();
	return B && B->GetFlankPoint(OutPoint);
}

bool UMDEnemyBrainComponent::GetGuardPoint(FVector& OutPoint) const
{
	if (!Assignment.bHasGuardPoint)
	{
		return false;
	}
	OutPoint = Assignment.GuardPoint;
	return true;
}

bool UMDEnemyBrainComponent::GetPackCentre(FVector& OutPoint) const
{
	if (!Assignment.bHasPackCentre)
	{
		return false;
	}
	OutPoint = Assignment.PackCentre;
	return true;
}

UMDEnemyBrainComponent::UMDEnemyBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the AI subsystem's scheduler drives RunDecision
	SetIsReplicatedByDefault(false);
}

UMDEnemyBrainComponent* UMDEnemyBrainComponent::FindFor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDEnemyBrainComponent* Brain = Actor->FindComponentByClass<UMDEnemyBrainComponent>())
	{
		return Brain;
	}
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		return Pawn->GetController() ? Pawn->GetController()->FindComponentByClass<UMDEnemyBrainComponent>() : nullptr;
	}
	return nullptr;
}

UMDAISubsystem* UMDEnemyBrainComponent::GetAISubsystem() const
{
	return UMDAISubsystem::Get(GetWorld());
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBrainComponent::OnPossessed(APawn* InPawn)
{
	OnUnpossessed();

	AMDEnemyCharacter* NewEnemy = Cast<AMDEnemyCharacter>(InPawn);
	if (!NewEnemy || !NewEnemy->HasAuthority())
	{
		return;
	}
	Enemy = NewEnemy;
	Attack = NewEnemy->AttackComponent;
	Poise = NewEnemy->Poise;
	Tier = UMDAISubsystem::ResolveTierParams(); // until the enemy joins its pack below, which gives it the pack's tier

	bDormant = false;
	LastDamageTime = -1.0;
	LastHeardTime = -1000.0;
	LastHeardTag = NAME_None;
	DefaultNetUpdateFrequency = NewEnemy->GetNetUpdateFrequency();
	AppliedNetFrequency = DefaultNetUpdateFrequency;

	bDead = NewEnemy->IsEnemyDead();
	CurrentIntent = bDead ? EMDIntent::Dead : EMDIntent::Idle;
	bIntentFinished = false;
	DecisionIndex = 0;
	ChosenAttackIndex = INDEX_NONE;
	NextAttackReadyTime = 0.0;
	Target.Reset();
	Known.Reset();
	Stimuli.Reset();
	LastScores.Reset();
	NewEnemy->SetReplicatedIntent(CurrentIntent);

	if (UMDPoiseComponent* P = Poise.Get())
	{
		P->OnReaction.AddUniqueDynamic(this, &UMDEnemyBrainComponent::HandleReaction);
		P->OnStunChanged.AddUniqueDynamic(this, &UMDEnemyBrainComponent::HandleStunChanged);
	}
	if (UMDAbilitySystemComponent* ASC = NewEnemy->AbilitySystem)
	{
		ASC->OnDeath.AddUniqueDynamic(this, &UMDEnemyBrainComponent::HandleDeath);
		ASC->OnDamageApplied.AddUObject(this, &UMDEnemyBrainComponent::HandleDamageApplied);
	}
	if (UMDEnemyAttackComponent* A = Attack.Get())
	{
		A->OnAttackEnded.AddUObject(this, &UMDEnemyBrainComponent::HandleAttackEnded);
	}
	if (UMDEnemyBehaviourComponent* B = NewEnemy->Behaviour)
	{
		B->OnActionEnded.RemoveAll(this);
		B->OnActionEnded.AddUObject(this, &UMDEnemyBrainComponent::HandleBehaviourActionEnded);
	}
	FeintOpportunity.Reset();
	Greed.Reset();
	bBound = true;

	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		EnemyId = Sub->RegisterBrain(this);
	}

	// Join the pack: level-placed enemies of one SpawnGroupId share a pack, the rest are packs of one.
	Assignment = FMDEnemyAssignment();
	RecentDamage.Reset();
	OverrideTarget.Reset();
	if (UMDPackDirectorSubsystem* Director = GetDirector())
	{
		const int32 PackId = Director->GetOrCreateGroupPack(NewEnemy->SpawnGroupId);
		Director->RegisterEnemy(NewEnemy, PackId);
	}
	Tier = ResolveTierParams(); // the pack's tier, computed when its first member spawned (V1 WP4)
}

FMDTierParams UMDEnemyBrainComponent::ResolveTierParams() const
{
	// md.AI.ForceTier can change at any time; otherwise the tier is the pack's, fixed at its spawn (V1 WP4).
	if (const UMDPackDirectorSubsystem* Director = GetDirector())
	{
		return FMDTierParams::ForTier(Director->GetTierOf(Enemy.Get()));
	}
	return UMDAISubsystem::ResolveTierParams();
}

UMDPackDirectorSubsystem* UMDEnemyBrainComponent::GetDirector() const
{
	return UMDPackDirectorSubsystem::Get(GetWorld());
}

void UMDEnemyBrainComponent::OnUnpossessed()
{
	if (bBound)
	{
		if (UMDPoiseComponent* P = Poise.Get())
		{
			P->OnReaction.RemoveDynamic(this, &UMDEnemyBrainComponent::HandleReaction);
			P->OnStunChanged.RemoveDynamic(this, &UMDEnemyBrainComponent::HandleStunChanged);
		}
		if (AMDEnemyCharacter* E = Enemy.Get())
		{
			if (UMDAbilitySystemComponent* ASC = E->AbilitySystem)
			{
				ASC->OnDeath.RemoveDynamic(this, &UMDEnemyBrainComponent::HandleDeath);
				ASC->OnDamageApplied.RemoveAll(this);
			}
			if (E->Behaviour)
			{
				E->Behaviour->OnActionEnded.RemoveAll(this);
			}
		}
		if (UMDEnemyAttackComponent* A = Attack.Get())
		{
			A->OnAttackEnded.RemoveAll(this);
		}
		bBound = false;
	}
	if (UMDPackDirectorSubsystem* Director = GetDirector())
	{
		Director->UnregisterEnemy(Enemy.Get()); // leaves the pack and returns the token
	}
	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		Sub->UnregisterBrain(this); // also returns the token
	}
	Enemy.Reset();
	Attack.Reset();
	Poise.Reset();
	Target.Reset();
	Known.Reset();
	Stimuli.Reset();
}

void UMDEnemyBrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnUnpossessed();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------------------------------

const FMDArchetypeAIParams& UMDEnemyBrainComponent::GetAIParams() const
{
	static const FMDArchetypeAIParams Defaults;
	const AMDEnemyCharacter* E = Enemy.Get();
	return (E && E->Archetype) ? E->Archetype->AIParams : Defaults;
}

float UMDEnemyBrainComponent::GetAttackRange() const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	return (E && E->Archetype) ? E->Archetype->GetAttackRange() : 0.0f;
}

float UMDEnemyBrainComponent::GetChosenTellTime() const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	if (!E || !E->Archetype || !E->Archetype->Attacks.IsValidIndex(ChosenAttackIndex))
	{
		return 0.0f;
	}
	const float Base = E->Archetype->Attacks[ChosenAttackIndex].TellTime;
	return Base > 0.0f ? Tier.ResolveTellTime(Base) : 0.0f;
}

bool UMDEnemyBrainComponent::HoldsAttackToken() const
{
	const UMDAISubsystem* Sub = GetAISubsystem();
	return Sub && Sub->HoldsAttackToken(Enemy.Get());
}

FVector UMDEnemyBrainComponent::GetTargetLocation() const
{
	const AActor* T = Target.Get();
	if (!T)
	{
		return FVector::ZeroVector;
	}
	if (const FKnownActor* K = Known.Find(Target))
	{
		return K->bSensedNow ? T->GetActorLocation() : K->LastLocation;
	}
	return T->GetActorLocation();
}

bool UMDEnemyBrainComponent::IsTargetSensed() const
{
	const FKnownActor* K = Target.IsValid() ? Known.Find(Target) : nullptr;
	return K && K->bSensedNow;
}

bool UMDEnemyBrainComponent::IsValidTargetActor(const AActor* Actor) const
{
	const APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn || !Pawn->GetPlayerState() || !Enemy.IsValid())
	{
		return false; // WP1 targets players only (packs and focus targets come with WP2)
	}
	if (UMDGameplayStatics::IsPawnDead(Pawn))
	{
		return false;
	}
	return UMDGameplayStatics::CanDamage(Enemy.Get(), Actor);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sense
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBrainComponent::ReportStimulus(AActor* Source, bool bSensed, const FVector& Location, EMDStimulusKind Kind, FName Tag)
{
	if (!Source || bDead || !Enemy.IsValid())
	{
		return;
	}
	if (bDormant && Kind != EMDStimulusKind::Damage)
	{
		return; // a dormant enemy senses nothing (2.9); being hurt wakes it first
	}
	FMDStimulus S;
	S.Source = Source;
	S.bSensed = bSensed;
	S.Location = Location;
	S.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	S.Kind = Kind;
	S.Tag = Tag;
	Stimuli.Push(S);

	if (Kind == EMDStimulusKind::Hearing && bSensed)
	{
		LastHeardLocation = Location;
		LastHeardTag = Tag;
		LastHeardTime = S.Time;
	}
	if (bSensed && MDPerception::ShouldShare(Kind, Tag))
	{
		ShareToPack(Source, Location, S.Time);
	}
}

void UMDEnemyBrainComponent::ReportHeard(AActor* Source, const FVector& Location, FName Tag)
{
	ReportStimulus(Source, true, Location, EMDStimulusKind::Hearing, Tag);
}

void UMDEnemyBrainComponent::ShareToPack(AActor* Source, const FVector& Location, double SightTime)
{
	// T0 never shares (ShareDelay < 0). The director fans the sighting out to the pack mates' brains.
	const FMDTierParams Current = ResolveTierParams();
	if (Current.ShareDelay < 0.0f)
	{
		return;
	}
	if (UMDPackDirectorSubsystem* Director = GetDirector())
	{
		Director->ShareSighting(Enemy.Get(), Source, Location, SightTime, Current.ShareDelay);
	}
}

void UMDEnemyBrainComponent::ReceiveSharedSighting(AActor* Source, const FVector& Location, double SightTime, float ShareDelay)
{
	if (!Source || bDead || bDormant || !Enemy.IsValid())
	{
		return;
	}
	const double QueueTime = MDPerception::SharedQueueTime(SightTime, ShareDelay, ResolveTierParams().ReactionTime);
	if (QueueTime < 0.0)
	{
		return;
	}
	FMDStimulus S;
	S.Source = Source;
	S.bSensed = true;
	S.Location = Location;
	S.Time = QueueTime;
	S.Kind = EMDStimulusKind::Shared;
	Stimuli.PushCoalesced(S);
}

bool UMDEnemyBrainComponent::GetKnownLocation(const AActor* Actor, FVector& OutLocation, bool& bOutSensedNow) const
{
	for (const TPair<TWeakObjectPtr<AActor>, FKnownActor>& Pair : Known)
	{
		if (Pair.Key.Get() == Actor)
		{
			bOutSensedNow = Pair.Value.bSensedNow;
			OutLocation = Pair.Value.bSensedNow && Actor ? Actor->GetActorLocation() : Pair.Value.LastLocation;
			return true;
		}
	}
	return false;
}

void UMDEnemyBrainComponent::SetSensing(bool bActive)
{
	if (AMDEnemyController* Controller = Cast<AMDEnemyController>(GetOwner()))
	{
		Controller->SetSensesActive(bActive);
	}
}

void UMDEnemyBrainComponent::SetDormant(bool bNewDormant, double Now)
{
	if (bDormant == bNewDormant || bDead)
	{
		return;
	}
	bDormant = bNewDormant;
	if (bDormant)
	{
		// Forget the fight and stand down; the scheduler skips this brain from now on.
		Stimuli.Reset();
		Known.Reset();
		Target.Reset();
		ReleaseToken(true);
		if (CurrentIntent != EMDIntent::Stagger && CurrentIntent != EMDIntent::Dead && CurrentIntent != EMDIntent::Attack && CurrentIntent != EMDIntent::Idle)
		{
			SetIntent(EMDIntent::Idle, true);
		}
		SyncThreat();
	}
	SetSensing(!bDormant);
}

void UMDEnemyBrainComponent::ApplyNetFrequency(float Hz)
{
	AMDEnemyCharacter* E = Enemy.Get();
	if (!E || Hz <= 0.0f || FMath::IsNearlyEqual(Hz, AppliedNetFrequency))
	{
		return;
	}
	AppliedNetFrequency = Hz;
	E->SetNetUpdateFrequency(Hz);
}

void UMDEnemyBrainComponent::HandleDamageApplied(AActor* Instigator, float Applied)
{
	// Being hit is a stimulus too (2.5 Damage sense): the attacker's position, through the same reaction delay. It is a
	// last-known position, not a live one: an enemy hit from behind turns toward where the blow came from.
	if (Instigator && Applied > 0.0f)
	{
		LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (!Greed.bBraced)
		{
			Greed.RegisterAbsorbedHit(LastDamageTime); // reset by a stagger (HandleReaction); N absorbed hits earn a braced counter
		}
		if (bDormant)
		{
			SetDormant(false, LastDamageTime); // hurt: wake at once
		}
		ReportStimulus(Instigator, true, Instigator->GetActorLocation(), EMDStimulusKind::Damage);

		// The pack scores this player as a threat, and a player who hurts this enemy badly becomes its target whatever the
		// pack's focus is (damage-taken override, 2.4).
		if (UMDPackDirectorSubsystem* Director = GetDirector())
		{
			Director->ReportDamage(Enemy.Get(), Instigator, Applied);
		}
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		RecentDamage.RemoveAll([Now](const FDamageRecord& R) { return Now - R.Time > MDPack::DamageOverrideWindow || !R.Instigator.IsValid(); });
		FDamageRecord& Record = RecentDamage.AddDefaulted_GetRef();
		Record.Instigator = Instigator;
		Record.Amount = Applied;
		Record.Time = Now;
		float FromInstigator = 0.0f;
		for (const FDamageRecord& R : RecentDamage)
		{
			FromInstigator += R.Instigator.Get() == Instigator ? R.Amount : 0.0f;
		}
		const AMDEnemyCharacter* E = Enemy.Get();
		const float MaxHealth = (E && E->AbilitySystem) ? E->AbilitySystem->GetMaxHealth() : 0.0f;
		if (MaxHealth > 0.0f && FromInstigator >= MaxHealth * MDPack::DamageOverrideFraction)
		{
			OverrideTarget = Instigator;
			OverrideUntil = Now + MDPack::DamageOverrideHold;
		}
	}
}

void UMDEnemyBrainComponent::UpdateKnownActors(double Now)
{
	Stimuli.Pop(Now, Tier.ReactionTime, [this](const FMDStimulus& S)
	{
		if (!S.Source.IsValid())
		{
			return;
		}
		FKnownActor& K = Known.FindOrAdd(S.Source);
		if (S.bSensed && !MDPerception::IsLive(S.Kind))
		{
			// Heard, hurt by, or told about: a last-known position only. It never makes the source "sensed now" (the brain
			// would then know its live position), never overwrites what the enemy sees with its own eyes, and an older
			// report never overwrites a newer one.
			if (!K.bSensedNow && S.Time >= K.LastSensedTime)
			{
				K.LastLocation = S.Location;
				K.LastSensedTime = S.Time;
			}
			else if (K.LastSensedTime < S.Time)
			{
				K.LastSensedTime = S.Time;
			}
			return;
		}
		K.LastLocation = S.Location;
		if (S.bSensed)
		{
			K.LastSensedTime = S.Time;
			K.bSensedNow = true;
		}
		else
		{
			K.bSensedNow = false;
		}
	});

	for (auto It = Known.CreateIterator(); It; ++It)
	{
		const bool bGone = !It.Key().IsValid() || !IsValidTargetActor(It.Key().Get());
		const bool bStale = !It.Value().bSensedNow && (Now - It.Value().LastSensedTime) > static_cast<double>(LoseTargetTime);
		if (bGone || bStale)
		{
			It.RemoveCurrent();
		}
	}
}

void UMDEnemyBrainComponent::UpdateTarget(double Now)
{
	// 1. Damage-taken override: the player who just hurt this enemy badly.
	if (OverrideTarget.IsValid() && Now < OverrideUntil && Known.Contains(OverrideTarget))
	{
		Target = OverrideTarget;
		return;
	}
	// 2. The pack's focus target, when this enemy knows it. Enemies that do not know it (T0 shares nothing) act alone.
	if (Assignment.FocusTarget.IsValid() && Known.Contains(Assignment.FocusTarget))
	{
		Target = Assignment.FocusTarget;
		return;
	}
	// 3. Sticky: keep the current target while it is still known.
	if (Target.IsValid() && Known.Contains(Target))
	{
		return;
	}
	Target.Reset();
	const AMDEnemyCharacter* E = Enemy.Get();
	if (!E)
	{
		return;
	}
	const FVector Origin = E->GetActorLocation();
	double BestDist = TNumericLimits<double>::Max();
	bool bBestSensed = false;
	for (const TPair<TWeakObjectPtr<AActor>, FKnownActor>& Pair : Known)
	{
		const AActor* Actor = Pair.Key.Get();
		if (!Actor)
		{
			continue;
		}
		const FVector Loc = Pair.Value.bSensedNow ? Actor->GetActorLocation() : Pair.Value.LastLocation;
		const double Dist = FVector::Dist2D(Origin, Loc);
		// A sensed target always beats a remembered one; among equals the nearest wins.
		if ((Pair.Value.bSensedNow && !bBestSensed) || (Pair.Value.bSensedNow == bBestSensed && Dist < BestDist))
		{
			BestDist = Dist;
			bBestSensed = Pair.Value.bSensedNow;
			Target = Pair.Key;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Decide
// ---------------------------------------------------------------------------------------------------------------------

FMDBrainContext UMDEnemyBrainComponent::BuildContext(double Now) const
{
	FMDBrainContext C;
	const AMDEnemyCharacter* E = Enemy.Get();
	const AActor* T = Target.Get();
	C.bHasTarget = E && T;
	if (C.bHasTarget)
	{
		C.bTargetSensed = IsTargetSensed();
		C.DistanceToTarget = static_cast<float>(FVector::Dist2D(E->GetActorLocation(), GetTargetLocation()));
	}
	const UMDAISubsystem* Sub = GetAISubsystem();
	C.bHasToken = Sub && Sub->HoldsAttackToken(E);
	C.bAttackInProgress = Attack.IsValid() && Attack->IsAttacking();
	C.bAttackReady = Now >= NextAttackReadyTime;
	C.bStunned = Poise.IsValid() && Poise->IsStunned();
	C.bDead = bDead;
	if (E && E->AbilitySystem && E->AbilitySystem->GetMaxHealth() > 0.0f)
	{
		C.HealthFraction = FMath::Clamp(E->AbilitySystem->GetHealth() / E->AbilitySystem->GetMaxHealth(), 0.0f, 1.0f);
	}
	C.AttackRange = GetAttackRange();
	C.ThreatRange = GetThreatRange();
	C.Role = Assignment.Role;
	C.PackState = Assignment.PackState;
	C.bHasSlot = Assignment.bHasSlotPoint && T && Assignment.FocusTarget.Get() == T;
	C.bHasRally = Assignment.bHasRally;
	C.bAtRally = Assignment.bHasRally && E && FVector::Dist2D(E->GetActorLocation(), Assignment.RallyPoint) <= MDPack::RallyAcceptance;
	C.RunSeed = Sub ? Sub->GetRunSeed() : 0;
	C.EnemyId = EnemyId;
	C.DecisionIndex = DecisionIndex;
	return C;
}

void UMDEnemyBrainComponent::RunDecision(double Now)
{
	AMDEnemyCharacter* E = Enemy.Get();
	if (bDead || !E || bDormant)
	{
		return; // dormant (2.9): no sensing, no thinking until a player is within 40 m or the enemy is hurt
	}

	// A forced tier (md.AI.ForceTier) can change at any time; the tier gates what the scorer may pick.
	Tier = ResolveTierParams();

	ResolveAssignment(Assignment);
	UpdateKnownActors(Now);
	UpdateTarget(Now);
	ReportToDirector();

	// Interrupt hold: staggered until the stun ends (HandleStunChanged normally ends it; this is the safety net).
	if (CurrentIntent == EMDIntent::Stagger && !bIntentFinished)
	{
		if (Poise.IsValid() && Poise->IsStunned())
		{
			return;
		}
		bIntentFinished = true;
	}
	// Commitment: an attack in progress is never re-scored (1.3 stagger honesty is the only way out).
	if (CurrentIntent == EMDIntent::Attack && !bIntentFinished)
	{
		if (Attack.IsValid() && Attack->IsAttacking())
		{
			return;
		}
		bIntentFinished = true; // it never started or ended without telling us
	}

	// V1 WP5 commitment: a feint, a blink, a charge or an arrow draw in progress is never re-scored.
	if (UMDEnemyBehaviourComponent* Behaviour = E->Behaviour)
	{
		if (Behaviour->IsBusy() && (CurrentIntent == EMDIntent::Feint || CurrentIntent == EMDIntent::Blink
			|| CurrentIntent == EMDIntent::Charge || CurrentIntent == EMDIntent::Kite) && !bIntentFinished)
		{
			return;
		}
	}

	FMDBrainContext Context = BuildContext(Now);

	// Ask for a token only when it is about to matter: in reach (with margin for the approach), ready, target in sight.
	// Charge-capable archetypes ask from charge range too (the run-up needs the token).
	UMDAISubsystem* Sub = GetAISubsystem();
	const float TokenReach = GetAIParams().bCanCharge ? FMath::Max(Context.AttackRange * 1.5f, MDBehaviour::ChargeMaxDistance) : Context.AttackRange * 1.5f;
	const bool bKiterNow = GetAIParams().bKiter || (E->Archetype && E->Archetype->RangedData);
	const bool bInShotReach = bKiterNow && Context.DistanceToTarget <= GetAIParams().PreferredRange + MDBehaviour::KiteMaxRangeSlack;
	if (Sub && Context.bHasTarget && Context.bTargetSensed && Context.bAttackReady && !Context.bHasToken
		&& ((Context.AttackRange > 0.0f && Context.DistanceToTarget <= TokenReach) || bInShotReach)
		&& MDBehaviour::CanHoldAttackToken(Assignment.Role))
	{
		Context.bHasToken = Sub->RequestAttackToken(E, Target.Get());
	}
	FillBehaviourContext(Context, Now);

	++DecisionIndex;
	Context.DecisionIndex = DecisionIndex;
	LastScores = FMDUtilityScorer::Score(Context, GetAIParams(), Tier);
	EMDIntent Chosen = FMDUtilityScorer::Select(LastScores, CurrentIntent, bIntentFinished, SwitchMargin);

	// md.AI.ForceIntent: PIE testing override (a stagger or a stun still holds the enemy).
	if (const EMDIntent Forced = GetForcedIntent(); Forced != EMDIntent::Idle && !Context.bStunned && Context.bHasTarget)
	{
		Chosen = Forced;
	}

	if (Chosen == EMDIntent::Attack || Chosen == EMDIntent::Feint)
	{
		ChosenAttackIndex = E->Archetype ? E->Archetype->PickAttack(Context.DistanceToTarget) : INDEX_NONE;
		if (Chosen == EMDIntent::Attack && Greed.bBraced)
		{
			const int32 Counter = PickCounterAttack(Context.DistanceToTarget);
			ChosenAttackIndex = Counter != INDEX_NONE ? Counter : ChosenAttackIndex;
		}
		if (ChosenAttackIndex == INDEX_NONE)
		{
			Chosen = Context.bHasToken ? EMDIntent::Chase : EMDIntent::HoldThreat; // nothing reaches from here
		}
		else if (Chosen == EMDIntent::Feint)
		{
			FeintOpportunity.MarkFeinted(); // one feint per attack opportunity
			FRandomStream Stream(static_cast<int32>(HashCombine(HashCombine(static_cast<uint32>(Context.RunSeed), static_cast<uint32>(EnemyId)), static_cast<uint32>(DecisionIndex * 31 + 7))));
			FeintRoll = Stream.FRand();
		}
	}
	else
	{
		ChosenAttackIndex = INDEX_NONE;
	}

	// Pack barks (the tell-cue path): a swarm howl when the pack starts to close in, "fall back" when it retreats.
	if (Chosen != CurrentIntent && E->Behaviour)
	{
		const bool bSwarmStart = Chosen == EMDIntent::Swarm;
		const bool bFallBack = Chosen == EMDIntent::Retreat;
		if (bSwarmStart || bFallBack)
		{
			const EMDBehaviourCue Cue = bSwarmStart ? EMDBehaviourCue::SwarmHowl : EMDBehaviourCue::FallBack;
			if (UMDPackDirectorSubsystem* Director = GetDirector(); !Director || Director->TryPackBark(E, static_cast<uint8>(Cue)))
			{
				E->Behaviour->PlayCue(Cue);
			}
		}
	}

	const bool bRestart = bIntentFinished || Chosen != CurrentIntent;
	SetIntent(Chosen, bRestart);
	SyncThreat(); // SetIntent does nothing when the intent is unchanged, but the token may have been granted or lost

	if (UMDAISubsystem::IsDebugEnabled())
	{
		DrawDebug();
	}
}

void UMDEnemyBrainComponent::SetIntent(EMDIntent Intent, bool bRestart)
{
	if (Intent == CurrentIntent && !bRestart)
	{
		return;
	}
	CurrentIntent = Intent;
	bIntentFinished = false;
	if (AMDEnemyCharacter* E = Enemy.Get())
	{
		E->SetReplicatedIntent(Intent);
		if (Intent == EMDIntent::Attack || Intent == EMDIntent::Feint || Intent == EMDIntent::Charge)
		{
			if (UMDAISubsystem* Sub = GetAISubsystem())
			{
				Sub->CommitAttackToken(E); // the swing is starting: a higher priority enemy can no longer take the token
			}
		}
	}
	SyncThreat();
	OnIntentSelected.Broadcast(Intent, bRestart);
}

void UMDEnemyBrainComponent::SyncThreat()
{
	AMDEnemyCharacter* E = Enemy.Get();
	if (!E)
	{
		return;
	}
	// Threatening = holds a token on its target and is about to swing or swinging: the token is only asked for in reach, so
	// holding one means "this enemy is about to attack that player". After the swing the cooldown clears it, although the
	// token itself is held 0.5 s longer.
	AActor* Wanted = nullptr;
	if (!bDead && !bDormant && CurrentIntent != EMDIntent::Stagger && CurrentIntent != EMDIntent::Dead && HoldsAttackToken())
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bSwinging = Attack.IsValid() && Attack->IsAttacking();
		if (bSwinging || Now >= NextAttackReadyTime)
		{
			Wanted = Target.Get();
		}
	}
	E->SetThreat(Wanted);
}

void UMDEnemyBrainComponent::NotifyIntentFinished()
{
	if (bDead)
	{
		return;
	}
	bIntentFinished = true;
	RunDecision(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void UMDEnemyBrainComponent::ReleaseToken(bool bImmediate)
{
	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		if (bImmediate)
		{
			Sub->ReleaseAttackToken(Enemy.Get());
		}
		else
		{
			Sub->ReleaseAttackTokenAfter(Enemy.Get(), MDAI::TokenHoldAfterAttack);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Interrupts and executor feedback
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBrainComponent::FillBehaviourContext(FMDBrainContext& C, double Now)
{
	AMDEnemyCharacter* E = Enemy.Get();
	AActor* T = Target.Get();
	const FMDArchetypeAIParams& P = GetAIParams();
	C.bKiter = P.bKiter || (E && E->Archetype && E->Archetype->RangedData);
	UMDEnemyBehaviourComponent* B = E ? E->Behaviour : nullptr;
	if (!B)
	{
		return;
	}
	C.bInGrace = B->IsInGrace();
	C.bInAmbushArrival = B->IsInAmbushArrival();
	C.bSwarmActive = MDBehaviour::ShouldSwarm(Tier.Tier, Assignment.PackAlive, C.bHasSlot, C.DistanceToTarget, C.ThreatRange);

	if (C.bHasTarget && T)
	{
		const FVector Self = E->GetActorLocation();
		const FVector TargetLoc = T->GetActorLocation();

		// Bait: never baits across rooms the player has not seen (both ends must be in revealed areas).
		if (Assignment.Role == EMDRole::Bait)
		{
			C.bBaitAllowed = MDBehaviour::CanBaitAcross(MDAIQueries::MakeWorldEnvironment(E->GetWorld(), E, T), Self, TargetLoc);
		}
		// Flank: the cached point behind the target, until the flanker stands on it.
		if (Assignment.Role == EMDRole::Flanker && C.bTargetSensed)
		{
			B->RefreshFlankPoint(T, Now);
			FVector Point;
			C.bFlankReady = B->GetFlankPoint(Point) && FVector::Dist2D(Self, Point) > 200.0;
		}
		// Blink and charge only close a gap, only while sensed and off cooldown.
		if (P.bCanBlink && C.bTargetSensed && Tier.IsIntentUnlocked(EMDIntent::Blink)
			&& C.DistanceToTarget >= MDBehaviour::BlinkMinRange && C.DistanceToTarget <= MDBehaviour::BlinkMaxRange && B->CanBlinkNow(Now))
		{
			B->RefreshBlinkArrival(T, Now);
			FVector Arrival;
			C.bBlinkReady = B->GetBlinkArrival(Arrival);
		}
		C.bChargeReady = P.bCanCharge && C.bTargetSensed && Tier.IsIntentUnlocked(EMDIntent::Charge)
			&& MDBehaviour::IsChargeDistanceOk(C.DistanceToTarget) && B->CanChargeNow(Now);
	}
	C.bGuardActive = Assignment.bHasGuardPoint;

	// Feint (T3+): one roll per attack opportunity; the opportunity ends when the token is lost or the real attack ends.
	if (!C.bHasToken)
	{
		FeintOpportunity.Reset();
	}
	else if (C.bHasTarget && C.bTargetSensed && C.bAttackReady && !C.bInGrace && !C.bKiter && C.AttackRange > 0.0f && C.DistanceToTarget <= C.AttackRange)
	{
		FRandomStream Stream(static_cast<int32>(HashCombine(HashCombine(static_cast<uint32>(C.RunSeed), static_cast<uint32>(EnemyId)), static_cast<uint32>(DecisionIndex + 1))));
		C.bFeintReady = FeintOpportunity.ShouldFeintNow(Tier.Tier, C.bHasToken, Stream.FRand());
	}

	// Punish greed (T3+): N absorbed hits earn a braced counter.
	if (Greed.BraceExpired(Now))
	{
		Greed.Reset();
	}
	if (!Greed.bBraced && P.PunishGreedHits > 0 && Greed.ShouldBrace(Tier.Tier, P.PunishGreedHits))
	{
		B->BeginBrace();
		Greed.StartBrace(Now);
	}
	C.bPunishCounter = Greed.bBraced;
}

int32 UMDEnemyBrainComponent::PickCounterAttack(float Distance) const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	if (!E || !E->Archetype)
	{
		return INDEX_NONE;
	}
	int32 Best = INDEX_NONE;
	float BestPoise = -1.0f;
	for (int32 i = 0; i < E->Archetype->Attacks.Num(); ++i)
	{
		const FMDEnemyAttack& A = E->Archetype->Attacks[i];
		if (A.Range >= Distance && A.Step.PoiseDamage > BestPoise)
		{
			BestPoise = A.Step.PoiseDamage;
			Best = i;
		}
	}
	return Best;
}

void UMDEnemyBrainComponent::HandleBehaviourActionEnded(EMDIntent Intent, bool bCompleted)
{
	if (bDead)
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const AMDEnemyCharacter* E = Enemy.Get();
	if (Intent == EMDIntent::Kite || Intent == EMDIntent::Charge)
	{
		ReleaseToken(false);
		const float Cooldown = (Intent == EMDIntent::Kite && E && E->Archetype && E->Archetype->Attacks.Num() > 0) ? E->Archetype->Attacks[0].Cooldown : 1.5f;
		NextAttackReadyTime = Now + Cooldown;
		SyncThreat();
	}
	if (CurrentIntent == Intent)
	{
		bIntentFinished = true;
		if (bCompleted)
		{
			RunDecision(Now); // a finished feint goes straight into the real attack
		}
	}
}

void UMDEnemyBrainComponent::HandleAttackEnded(bool bInterrupted)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CurrentIntent == EMDIntent::Feint)
	{
		return; // the feint's own cancel: the token and the attack stay ready for the real swing 0.3 - 0.6 s later
	}
	if (CurrentIntent == EMDIntent::Attack)
	{
		FeintOpportunity.Reset(); // the opportunity is spent
		if (Greed.bBraced)
		{
			Greed.Reset(); // the braced counter is thrown
		}
	}
	ReleaseToken(false); // held for the attack's duration + 0.5 s (1.3)
	const AMDEnemyCharacter* E = Enemy.Get();
	if (E && E->Archetype && E->Archetype->Attacks.IsValidIndex(ChosenAttackIndex))
	{
		NextAttackReadyTime = Now + E->Archetype->Attacks[ChosenAttackIndex].Cooldown;
	}
	SyncThreat();
	if (CurrentIntent == EMDIntent::Attack && !bDead)
	{
		bIntentFinished = true;
		if (!bInterrupted)
		{
			RunDecision(Now); // straight into the next decision: no idle frame between swings
		}
	}
}

void UMDEnemyBrainComponent::HandleReaction(const FMDHitResultRep& Hit)
{
	if (bDead || static_cast<int32>(Hit.Reaction) < static_cast<int32>(EMDHitReaction::Stagger))
	{
		return; // a flinch never interrupts (1.3: stagger honesty is about staggers)
	}
	ReleaseToken(true);
	Greed.Reset();
	FeintOpportunity.Reset();
	if (AMDEnemyCharacter* E = Enemy.Get(); E && E->Behaviour)
	{
		E->Behaviour->CancelAction(); // a feint, a blink wind-up, a charge or a draw ends silently
	}
	if (UMDEnemyAttackComponent* A = Attack.Get())
	{
		A->CancelAttack();
	}
	SetIntent(EMDIntent::Stagger, true);
}

void UMDEnemyBrainComponent::HandleStunChanged(bool bStunned)
{
	if (!bStunned && CurrentIntent == EMDIntent::Stagger && !bDead)
	{
		NotifyIntentFinished();
	}
}

void UMDEnemyBrainComponent::HandleDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	ReleaseToken(true);
	if (AMDEnemyCharacter* E = Enemy.Get(); E && E->Behaviour)
	{
		E->Behaviour->CancelAction();
	}
	if (UMDEnemyAttackComponent* A = Attack.Get())
	{
		A->CancelAttack();
	}
	SetIntent(EMDIntent::Dead, true);
	if (UMDPackDirectorSubsystem* Director = GetDirector())
	{
		Director->UnregisterEnemy(Enemy.Get()); // out of the pack at once: the survivors' retreat threshold counts the living
	}
	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		Sub->UnregisterBrain(this);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Pack
// ---------------------------------------------------------------------------------------------------------------------

bool UMDEnemyBrainComponent::ResolveAssignment(FMDEnemyAssignment& Out) const
{
	const UMDPackDirectorSubsystem* Director = GetDirector();
	if (Director && Enemy.IsValid() && Director->GetAssignment(Enemy.Get(), Out))
	{
		return true;
	}
	Out = FMDEnemyAssignment();
	return false;
}

void UMDEnemyBrainComponent::ReportToDirector()
{
	UMDPackDirectorSubsystem* Director = GetDirector();
	AMDEnemyCharacter* E = Enemy.Get();
	if (!Director || !E)
	{
		return;
	}
	FMDMemberReport Report;
	Report.Target = Target;
	for (const TPair<TWeakObjectPtr<AActor>, FKnownActor>& Pair : Known)
	{
		Report.Known.Add(Pair.Key);
	}
	const FMDArchetypeAIParams& Params = GetAIParams();
	for (const EMDRole Role : Params.AllowedRoles)
	{
		Report.AllowedMask |= MDPack::RoleBit(Role);
	}
	Report.bCaster = Params.bIsCaster;
	Report.PreferredRange = Params.PreferredRange;
	Report.ThreatRange = Params.ThreatRange;
	Report.AttackRange = GetAttackRange();
	if (E->AbilitySystem && E->AbilitySystem->GetMaxHealth() > 0.0f)
	{
		Report.HealthFraction = FMath::Clamp(E->AbilitySystem->GetHealth() / E->AbilitySystem->GetMaxHealth(), 0.0f, 1.0f);
	}
	Report.bRecovering = Poise.IsValid() && Poise->IsStunned();
	Director->ReportMember(E, Report);
}

bool UMDEnemyBrainComponent::GetSlotPoint(FVector& OutPoint) const
{
	FMDEnemyAssignment A;
	if (!ResolveAssignment(A) || !A.bHasSlotPoint || A.FocusTarget.Get() != Target.Get() || !IsTargetSensed())
	{
		return false;
	}
	OutPoint = A.SlotPoint;
	return true;
}

bool UMDEnemyBrainComponent::GetThreatPoint(FVector& OutPoint) const
{
	FMDEnemyAssignment A;
	if (!ResolveAssignment(A) || !A.bHasSlotPoint || A.FocusTarget.Get() != Target.Get() || !IsTargetSensed())
	{
		return false;
	}
	OutPoint = A.ThreatPoint;
	return true;
}

bool UMDEnemyBrainComponent::GetRallyPoint(FVector& OutPoint) const
{
	FMDEnemyAssignment A;
	if (!ResolveAssignment(A) || !A.bHasRally)
	{
		return false;
	}
	OutPoint = A.RallyPoint;
	return true;
}

bool UMDEnemyBrainComponent::IsAtRally() const
{
	FVector Rally;
	const AMDEnemyCharacter* E = Enemy.Get();
	return E && GetRallyPoint(Rally) && FVector::Dist2D(E->GetActorLocation(), Rally) <= MDPack::RallyAcceptance;
}

// ---------------------------------------------------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnemyBrainComponent::DrawDebug() const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	UWorld* World = GetWorld();
	if (!E || !World)
	{
		return;
	}
	MDAI::FScores Sorted = LastScores;
	FMDUtilityScorer::SortDescending(Sorted);
	FString Text = FString::Printf(TEXT("#%d %s%s  tok:%s  T%d"), EnemyId, MDIntentToString(CurrentIntent),
		CurrentIntent == EMDIntent::Attack && ChosenAttackIndex != INDEX_NONE && E->Archetype && E->Archetype->Attacks.IsValidIndex(ChosenAttackIndex)
			? *FString::Printf(TEXT("(%s)"), *E->Archetype->Attacks[ChosenAttackIndex].Name.ToString()) : TEXT(""),
		HoldsAttackToken() ? TEXT("YES") : TEXT("no"), static_cast<int32>(Tier.Tier));
	if (Assignment.PackId != 0)
	{
		Text += FString::Printf(TEXT("  (pack IT %.2f)"), Assignment.ITValue); // V1 WP4: the ITValue behind the tier
	}
	{
		// Pack line (V1 WP2): pack id, role, ring slot, pack state, and the pack's focus target.
		const AActor* Focus = Assignment.FocusTarget.Get();
		Text += FString::Printf(TEXT("\npack %d  %s  slot %s  %s  focus %s"), Assignment.PackId, *UEnum::GetValueAsString(Assignment.Role),
			Assignment.Slot >= 0 ? *FString::FromInt(Assignment.Slot) : TEXT("-"), *UEnum::GetValueAsString(Assignment.PackState),
			Focus ? *Focus->GetName() : TEXT("-"));
	}
	for (int32 i = 0; i < Sorted.Num() && i < 3; ++i)
	{
		Text += FString::Printf(TEXT("\n%s %.2f"), MDIntentToString(Sorted[i].Intent), Sorted[i].Score);
	}
	if (const AActor* T = Target.Get())
	{
		Text += FString::Printf(TEXT("\n-> %s %.0f cm%s"), *T->GetName(), FVector::Dist2D(E->GetActorLocation(), GetTargetLocation()), IsTargetSensed() ? TEXT("") : TEXT(" (lost)"));
	}
	const FColor Color = CurrentIntent == EMDIntent::Attack ? FColor::Red : (HoldsAttackToken() ? FColor::Orange : FColor::White);
	DrawDebugString(World, E->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), Text, nullptr, Color, 1.0f / UMDAISubsystem::GetBrainHz() + 0.05f, true);
}

// md.AI.DebugPerception 1 (server): green cone = awake, grey = dormant, red = a player is in sight. Yellow sphere + line = the
// last-known position of a player the enemy remembers (not in sight); red line = a player in sight; cyan sphere + tag = the
// last noise this enemy heard (for 2 s); the faint circle is its hearing range.
void UMDEnemyBrainComponent::DrawPerceptionDebug(float Life) const
{
	const AMDEnemyCharacter* E = Enemy.Get();
	UWorld* World = GetWorld();
	if (!E || !World)
	{
		return;
	}
	const FVector Eye = E->GetActorLocation() + FVector(0.0, 0.0, 60.0);
	const double Now = World->GetTimeSeconds();
	const float HalfFov = FMath::DegreesToRadians(Tier.SightFOV * 0.5f);
	bool bSeeing = false;
	for (const TPair<TWeakObjectPtr<AActor>, FKnownActor>& Pair : Known)
	{
		bSeeing |= Pair.Value.bSensedNow;
	}
	const FColor ConeColor = bDormant ? FColor(90, 90, 90) : (bSeeing ? FColor::Red : FColor::Green);
	DrawDebugCone(World, Eye, E->GetActorForwardVector(), Tier.SightRadius, HalfFov, HalfFov, 12, ConeColor, false, Life, SDPG_World, 1.0f);
	DrawDebugCircle(World, E->GetActorLocation() + FVector(0.0, 0.0, 20.0), MDPerception::HearingRange(Tier.Tier), 32, FColor(0, 110, 140), false, Life, SDPG_World, 0.0f, FVector::ForwardVector, FVector::RightVector, false);

	for (const TPair<TWeakObjectPtr<AActor>, FKnownActor>& Pair : Known)
	{
		const AActor* Actor = Pair.Key.Get();
		if (!Actor)
		{
			continue;
		}
		if (Pair.Value.bSensedNow)
		{
			DrawDebugLine(World, Eye, Actor->GetActorLocation(), FColor::Red, false, Life, SDPG_World, 1.0f);
		}
		else
		{
			DrawDebugSphere(World, Pair.Value.LastLocation, 30.0f, 8, FColor::Yellow, false, Life, SDPG_World, 1.0f);
			DrawDebugLine(World, Eye, Pair.Value.LastLocation, FColor::Yellow, false, Life, SDPG_World, 0.5f);
			DrawDebugString(World, Pair.Value.LastLocation + FVector(0.0, 0.0, 50.0),
				FString::Printf(TEXT("last known %.1fs"), static_cast<float>(Now - Pair.Value.LastSensedTime)), nullptr, FColor::Yellow, Life, true);
		}
	}
	if (Now - LastHeardTime < 2.0)
	{
		DrawDebugSphere(World, LastHeardLocation, 40.0f, 8, FColor::Cyan, false, Life, SDPG_World, 1.0f);
		DrawDebugLine(World, Eye, LastHeardLocation, FColor::Cyan, false, Life, SDPG_World, 0.5f);
		DrawDebugString(World, LastHeardLocation + FVector(0.0, 0.0, 60.0), FString::Printf(TEXT("heard %s"), *LastHeardTag.ToString()), nullptr, FColor::Cyan, Life, true);
	}
}
