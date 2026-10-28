#include "AI/StateTree/MDSTBehaviourTasks.h"

#include "AI/MDAIQueries.h"
#include "AI/MDAIRules.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyBehaviourComponent.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyController.h"
#include "StateTreeExecutionContext.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"

#define LOCTEXT_NAMESPACE "MDSTBehaviourTasks"

namespace MDSTBehaviourLocal
{
	double Now(const FStateTreeExecutionContext& Context)
	{
		const UWorld* World = Context.GetWorld();
		return World ? World->GetTimeSeconds() : 0.0;
	}

	bool TryPath(const AMDEnemyController& Controller)
	{
		UMDAISubsystem* AI = UMDAISubsystem::Get(Controller.GetWorld());
		return !AI || AI->TryConsumePathRequest();
	}

	bool IsMoving(const AMDEnemyController& Controller)
	{
		return Controller.GetMoveStatus() == EPathFollowingStatus::Moving;
	}

	// Move to Goal within the frame's path budget and the 1.5 m repath rule. False = over budget (retry next tick).
	bool MoveTo(AMDEnemyController& Controller, FVector& LastGoal, const FVector& Goal, float Acceptance)
	{
		if (!MDNav::ShouldRepath(IsMoving(Controller), LastGoal, Goal))
		{
			return true;
		}
		if (!TryPath(Controller))
		{
			return false;
		}
		Controller.MoveToLocation(Goal, Acceptance, true, true, true, true);
		LastGoal = Goal;
		return true;
	}

	void FaceTarget(AMDEnemyController& Controller, AActor* Target)
	{
		if (AMDEnemyCharacter* Enemy = Controller.GetEnemy())
		{
			if (Controller.GetFocusActor() != Target || !Enemy->IsFacingTarget())
			{
				Enemy->FaceTarget(Target);
			}
		}
	}

	void StopFacing(AMDEnemyController& Controller, AActor* Target)
	{
		Controller.StopMovement();
		if (AMDEnemyCharacter* Enemy = Controller.GetEnemy())
		{
			Enemy->FaceTarget(Target);
		}
	}

	struct FCtx
	{
		AMDEnemyController* Controller = nullptr;
		UMDEnemyBrainComponent* Brain = nullptr;
		AMDEnemyCharacter* Enemy = nullptr;
		UMDEnemyBehaviourComponent* Behaviour = nullptr;
		AActor* Target = nullptr;
		bool IsValid() const { return Controller && Brain && Enemy; }
	};
}

#define MD_BEHAVIOUR_CTX(Name)                                                                         \
	MDSTBehaviourLocal::FCtx Name;                                                                     \
	Name.Controller = ResolveController(Context, Context.GetInstanceData(*this));                      \
	Name.Brain = Name.Controller ? Name.Controller->GetBrain() : nullptr;                              \
	Name.Enemy = Name.Controller ? Name.Controller->GetEnemy() : nullptr;                              \
	Name.Behaviour = Name.Enemy ? Name.Enemy->Behaviour.Get() : nullptr;                               \
	Name.Target = Name.Brain ? Name.Brain->GetTarget() : nullptr;

// ---- Swarm ----

FMDST_EnemySwarm::FMDST_EnemySwarm()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FMDST_EnemySwarm::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
		Context.GetInstanceData(*this).NextUpdateTime = 0.0;
		Context.GetInstanceData(*this).LastGoal = FVector::ZeroVector;
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemySwarm::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTBehaviourLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Target)
	{
		return EStateTreeRunStatus::Running;
	}
	MDSTBehaviourLocal::FaceTarget(*C.Controller, C.Target);
	FVector Point;
	// A token holder takes its evenly spaced melee slot; the others hold the threat ring on their own angle.
	const bool bFound = C.Brain->HoldsAttackToken() ? C.Brain->GetSlotPoint(Point) : C.Brain->GetThreatPoint(Point);
	if (bFound && FVector::Dist2D(Point, C.Enemy->GetActorLocation()) > 80.0)
	{
		if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, Point, 40.0f))
		{
			Data.NextUpdateTime = 0.0;
		}
	}
	else if (bFound && MDSTBehaviourLocal::IsMoving(*C.Controller))
	{
		C.Controller->StopMovement();
	}
	else if (!bFound)
	{
		if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, C.Brain->GetTargetLocation(), FMath::Max(C.Brain->GetThreatRange(), 200.0f)))
		{
			Data.NextUpdateTime = 0.0;
		}
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemySwarm::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Controller)
	{
		C.Controller->StopMovement();
	}
}

// ---- Bait ----

FMDST_EnemyBait::FMDST_EnemyBait()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FMDST_EnemyBait::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
		Context.GetInstanceData(*this).NextUpdateTime = 0.0;
		Context.GetInstanceData(*this).LastGoal = FVector::ZeroVector;
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemyBait::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTBehaviourLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Target)
	{
		return EStateTreeRunStatus::Running;
	}
	MDSTBehaviourLocal::FaceTarget(*C.Controller, C.Target); // always faces the player
	const FVector Self = C.Enemy->GetActorLocation();
	const FVector Player = C.Target->GetActorLocation();
	FVector Centre = Self;
	C.Brain->GetPackCentre(Centre);
	const double PlayerDist = FVector::Dist2D(Self, Player);

	FVector Step;
	if (!MDBehaviour::IsWithinBaitLeash(Self, Centre))
	{
		// Strayed past 8 m from the pack: walk back to it.
		if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, Centre, MDBehaviour::BaitLeash * 0.5f))
		{
			Data.NextUpdateTime = 0.0;
		}
	}
	else if (MDBehaviour::ComputeBaitStep(Self, Player, Centre, Step))
	{
		if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, Step, 30.0f)) // the half-step back
		{
			Data.NextUpdateTime = 0.0;
		}
	}
	else if (MDSTBehaviourLocal::IsMoving(*C.Controller))
	{
		C.Controller->StopMovement();
	}

	if (C.Behaviour && PlayerDist < MDBehaviour::BaitApproachDistance * 1.6)
	{
		const EMDIntelligenceTier TierValue = C.Brain->GetTier().Tier;
		if (MDBehaviour::FakeFlinchUnlocked(TierValue) && Now >= C.Behaviour->NextFlinchTime && PlayerDist < MDBehaviour::BaitApproachDistance)
		{
			C.Behaviour->NextFlinchTime = Now + MDBehaviour::FakeFlinchInterval;
			C.Behaviour->NextTauntTime = Now + 2.0; // the flinch takes the place of the next taunt
			C.Behaviour->PlayCue(EMDBehaviourCue::FakeFlinch);
		}
		else if (Now >= C.Behaviour->NextTauntTime)
		{
			C.Behaviour->NextTauntTime = Now + MDBehaviour::BaitTauntInterval;
			C.Behaviour->PlayCue(EMDBehaviourCue::Taunt);
		}
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyBait::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Controller)
	{
		C.Controller->StopMovement();
	}
}

// ---- Kite ----

FMDST_EnemyKite::FMDST_EnemyKite()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FMDST_EnemyKite::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
		Context.GetInstanceData(*this).NextUpdateTime = 0.0;
		Context.GetInstanceData(*this).LastGoal = FVector::ZeroVector;
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemyKite::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTBehaviourLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Target || !C.Behaviour || C.Behaviour->IsBusy())
	{
		return EStateTreeRunStatus::Running; // drawing: stand and aim
	}
	MDSTBehaviourLocal::FaceTarget(*C.Controller, C.Target);
	const float Distance = static_cast<float>(FVector::Dist2D(C.Enemy->GetActorLocation(), C.Target->GetActorLocation()));
	const float Preferred = C.Brain->GetAIParams().PreferredRange;
	const bool bLOS = C.Controller->LineOfSightTo(C.Target);
	const bool bCanFire = C.Brain->HoldsAttackToken() && C.Behaviour->CanShootNow(Now) && C.Brain->IsTargetSensed();
	switch (MDBehaviour::DecideKite(Distance, Preferred, bLOS, MDBehaviour::KiteRepositionReady(Now, C.Behaviour->LastRepositionTime), bCanFire))
	{
	case MDBehaviour::EKiteAction::Fire:
		C.Controller->StopMovement();
		C.Behaviour->BeginRangedShot(C.Target, MDBehaviour::KiteDrawTimeFor(C.Brain->GetTier()));
		break;
	case MDBehaviour::EKiteAction::Reposition:
	{
		FVector Point;
		if (MDAIQueries::FindKitePointFor(C.Enemy, C.Target, Preferred, Point) && MDSTBehaviourLocal::TryPath(*C.Controller))
		{
			C.Controller->MoveToLocation(Point, 60.0f, true, true, true, true);
			Data.LastGoal = Point;
			C.Behaviour->NoteReposition(Now); // one reposition per second
		}
		break;
	}
	default:
		break;
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyKite::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Controller)
	{
		C.Controller->StopMovement();
	}
}

// ---- Flank ----

FMDST_EnemyFlank::FMDST_EnemyFlank()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FMDST_EnemyFlank::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		if (C.Enemy)
		{
			C.Enemy->FaceMovement(); // circle round, do not back-pedal
		}
		Context.GetInstanceData(*this).NextUpdateTime = 0.0;
		Context.GetInstanceData(*this).LastGoal = FVector::ZeroVector;
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemyFlank::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTBehaviourLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	MD_BEHAVIOUR_CTX(C);
	FVector Point;
	if (!C.IsValid() || !C.Brain->GetFlankPoint(Point))
	{
		return EStateTreeRunStatus::Running;
	}
	if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, Point, 60.0f))
	{
		Data.NextUpdateTime = 0.0;
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyFlank::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Controller)
	{
		C.Controller->StopMovement();
	}
}

// ---- Guard ----

FMDST_EnemyGuard::FMDST_EnemyGuard()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FMDST_EnemyGuard::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
		Context.GetInstanceData(*this).NextUpdateTime = 0.0;
		Context.GetInstanceData(*this).LastGoal = FVector::ZeroVector;
	}
	return Tick(Context, 0.0f);
}

EStateTreeRunStatus FMDST_EnemyGuard::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const double Now = MDSTBehaviourLocal::Now(Context);
	if (Now < Data.NextUpdateTime)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.NextUpdateTime = Now + UpdateInterval;
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid())
	{
		return EStateTreeRunStatus::Running;
	}
	if (C.Target)
	{
		MDSTBehaviourLocal::FaceTarget(*C.Controller, C.Target);
	}
	FVector Point;
	if (C.Brain->GetGuardPoint(Point))
	{
		if (FVector::Dist2D(Point, C.Enemy->GetActorLocation()) > 80.0)
		{
			if (!MDSTBehaviourLocal::MoveTo(*C.Controller, Data.LastGoal, Point, 40.0f))
			{
				Data.NextUpdateTime = 0.0;
			}
		}
		else if (MDSTBehaviourLocal::IsMoving(*C.Controller))
		{
			C.Controller->StopMovement();
		}
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyGuard::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Controller)
	{
		C.Controller->StopMovement();
	}
}

// ---- Feint ----

FMDST_EnemyFeint::FMDST_EnemyFeint()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyFeint::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Behaviour)
	{
		return EStateTreeRunStatus::Running;
	}
	MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
	if (!C.Behaviour->BeginFeint(C.Brain->GetChosenAttackIndex(), C.Target, C.Brain->GetChosenTellTime(), C.Brain->GetFeintRoll()))
	{
		C.Brain->NotifyIntentFinished(); // cannot feint: the brain attacks for real
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyFeint::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Behaviour && C.Behaviour->GetAction() == EMDBehaviourAction::Feint)
	{
		C.Behaviour->CancelAction();
	}
}

// ---- Blink ----

FMDST_EnemyBlink::FMDST_EnemyBlink()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyBlink::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Behaviour)
	{
		return EStateTreeRunStatus::Running;
	}
	if (!C.Target || !C.Behaviour->BeginBlink(C.Target))
	{
		C.Brain->NotifyIntentFinished();
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyBlink::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Behaviour && C.Behaviour->GetAction() == EMDBehaviourAction::PreBlink)
	{
		C.Behaviour->CancelAction();
	}
}

// ---- Charge ----

FMDST_EnemyCharge::FMDST_EnemyCharge()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyCharge::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (!C.IsValid() || !C.Behaviour)
	{
		return EStateTreeRunStatus::Running;
	}
	if (!C.Target || !C.Behaviour->BeginCharge(C.Target))
	{
		C.Brain->NotifyIntentFinished();
	}
	return EStateTreeRunStatus::Running;
}

void FMDST_EnemyCharge::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.Behaviour && C.Behaviour->IsBusy() && C.Behaviour->GetAction() != EMDBehaviourAction::None)
	{
		C.Behaviour->CancelAction();
	}
}

// ---- Ambush ----

FMDST_EnemyAmbush::FMDST_EnemyAmbush()
{
	bShouldCallTick = false;
}

EStateTreeRunStatus FMDST_EnemyAmbush::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	MD_BEHAVIOUR_CTX(C);
	if (C.IsValid())
	{
		MDSTBehaviourLocal::StopFacing(*C.Controller, C.Target);
	}
	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FMDST_EnemySwarm::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("SwarmDesc", "Swarm: evenly spaced ring slots, hold the threat ring without a token");
}
FText FMDST_EnemyBait::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("BaitDesc", "Bait: face the player, half-step back, taunt, stay within 8 m of the pack");
}
FText FMDST_EnemyKite::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("KiteDesc", "Kite: keep range, reposition once a second, fire with a token");
}
FText FMDST_EnemyFlank::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("FlankDesc", "Flank: walk to the point behind the target");
}
FText FMDST_EnemyGuard::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("GuardDesc", "Guard: stand on the line between the caster and the player");
}
FText FMDST_EnemyFeint::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("FeintDesc", "Feint: wind up, cancel with a visible jerk, then the brain attacks for real");
}
FText FMDST_EnemyBlink::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("BlinkDesc", "Blink: pre-blink cue, then a validated teleport");
}
FText FMDST_EnemyCharge::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("ChargeDesc", "Charge: line up and run straight; a sideways dodge cancels it");
}
FText FMDST_EnemyAmbush::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("AmbushDesc", "Ambush: stand still through the arrival grace");
}
#endif

#undef LOCTEXT_NAMESPACE
