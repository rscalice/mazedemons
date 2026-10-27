#include "AI/MDEnemyBrainComponent.h"

#include "AI/MDAISubsystem.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameplayStatics.h"
#include "MDPoiseComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

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
	Tier = UMDAISubsystem::ResolveTierParams();

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
	bBound = true;

	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		EnemyId = Sub->RegisterBrain(this);
	}
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
		}
		if (UMDEnemyAttackComponent* A = Attack.Get())
		{
			A->OnAttackEnded.RemoveAll(this);
		}
		bBound = false;
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

void UMDEnemyBrainComponent::ReportStimulus(AActor* Source, bool bSensed, const FVector& Location)
{
	if (!Source || bDead || !Enemy.IsValid())
	{
		return;
	}
	FMDStimulus S;
	S.Source = Source;
	S.bSensed = bSensed;
	S.Location = Location;
	S.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Stimuli.Push(S);
}

void UMDEnemyBrainComponent::HandleDamageApplied(AActor* Instigator, float Applied)
{
	// Being hit is a stimulus too (2.5 Damage sense): the attacker's position, through the same reaction delay.
	if (Instigator && Applied > 0.0f)
	{
		ReportStimulus(Instigator, true, Instigator->GetActorLocation());
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
	// Sticky: keep the current target while it is still known (WP2's focus target replaces this rule).
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
	C.RunSeed = Sub ? Sub->GetRunSeed() : 0;
	C.EnemyId = EnemyId;
	C.DecisionIndex = DecisionIndex;
	return C;
}

void UMDEnemyBrainComponent::RunDecision(double Now)
{
	AMDEnemyCharacter* E = Enemy.Get();
	if (bDead || !E)
	{
		return;
	}

	UpdateKnownActors(Now);
	UpdateTarget(Now);

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

	FMDBrainContext Context = BuildContext(Now);

	// Ask for a token only when it is about to matter: in reach (with margin for the approach), ready, target in sight.
	UMDAISubsystem* Sub = GetAISubsystem();
	if (Sub && Context.bHasTarget && Context.bTargetSensed && Context.bAttackReady && !Context.bHasToken
		&& Context.AttackRange > 0.0f && Context.DistanceToTarget <= Context.AttackRange * 1.5f)
	{
		Context.bHasToken = Sub->RequestAttackToken(E, Target.Get());
	}

	++DecisionIndex;
	Context.DecisionIndex = DecisionIndex;
	LastScores = FMDUtilityScorer::Score(Context, GetAIParams(), Tier);
	EMDIntent Chosen = FMDUtilityScorer::Select(LastScores, CurrentIntent, bIntentFinished, SwitchMargin);

	if (Chosen == EMDIntent::Attack)
	{
		ChosenAttackIndex = E->Archetype ? E->Archetype->PickAttack(Context.DistanceToTarget) : INDEX_NONE;
		if (ChosenAttackIndex == INDEX_NONE)
		{
			Chosen = Context.bHasToken ? EMDIntent::Chase : EMDIntent::HoldThreat; // nothing reaches from here
		}
	}
	else
	{
		ChosenAttackIndex = INDEX_NONE;
	}

	const bool bRestart = bIntentFinished || Chosen != CurrentIntent;
	SetIntent(Chosen, bRestart);

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
	}
	OnIntentSelected.Broadcast(Intent, bRestart);
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

void UMDEnemyBrainComponent::HandleAttackEnded(bool bInterrupted)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ReleaseToken(false); // held for the attack's duration + 0.5 s (1.3)
	const AMDEnemyCharacter* E = Enemy.Get();
	if (E && E->Archetype && E->Archetype->Attacks.IsValidIndex(ChosenAttackIndex))
	{
		NextAttackReadyTime = Now + E->Archetype->Attacks[ChosenAttackIndex].Cooldown;
	}
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
	if (UMDEnemyAttackComponent* A = Attack.Get())
	{
		A->CancelAttack();
	}
	SetIntent(EMDIntent::Dead, true);
	if (UMDAISubsystem* Sub = GetAISubsystem())
	{
		Sub->UnregisterBrain(this);
	}
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
