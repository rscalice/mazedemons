#include "MDCombatRules.h"

namespace MDCombatRulesLocal
{
	int32 Rank(EMDHitReaction Reaction)
	{
		return static_cast<int32>(Reaction);
	}

	EMDHitReaction Stronger(EMDHitReaction A, EMDHitReaction B)
	{
		return Rank(A) >= Rank(B) ? A : B;
	}

	EMDHitReaction Weaker(EMDHitReaction A, EMDHitReaction B)
	{
		return Rank(A) <= Rank(B) ? A : B;
	}
}

void UMDCombatRules::BufferInput(FMDComboState& State, EMDComboInput Input, double Now, float HoldSeconds)
{
	if (Input == EMDComboInput::None)
	{
		return;
	}
	State.Buffered = Input;
	State.BufferedTime = Now;
	State.BufferedHold = FMath::Max(HoldSeconds, 0.0f);
}

EMDComboAction UMDCombatRules::Advance(FMDComboState& State, const FMDComboTuning& Tuning, EMDComboEvent Event, double Now)
{
	switch (Event)
	{
	case EMDComboEvent::AttackEnded:
		State.bAttacking = false;
		State.bInComboWindow = false;
		State.bInCancelWindow = false;
		State.LastAttackEndTime = Now;
		return EMDComboAction::None;

	case EMDComboEvent::Interrupted:
		State.Reset();
		return EMDComboAction::Reset;

	case EMDComboEvent::Poll:
	default:
		break;
	}

	if (State.Buffered == EMDComboInput::None)
	{
		return EMDComboAction::None;
	}
	if (Now - State.BufferedTime > Tuning.InputBufferTime)
	{
		State.Buffered = EMDComboInput::None; // expired
		return EMDComboAction::None;
	}

	const bool bCanChain = !State.bAttacking || State.bInComboWindow;
	switch (State.Buffered)
	{
	case EMDComboInput::Dodge:
	{
		if (State.bAttacking && !State.bInCancelWindow)
		{
			return EMDComboAction::None; // wait for the cancel window
		}
		State.Buffered = EMDComboInput::None;
		ApplyDodge(State, Now);
		return EMDComboAction::Dodge;
	}

	case EMDComboInput::Light:
	{
		if (!bCanChain)
		{
			return EMDComboAction::None;
		}
		State.Buffered = EMDComboInput::None;
		State.bInComboWindow = false;
		State.bInCancelWindow = false;
		if (!State.bAttacking && (Now - State.LastDodgeTime) <= Tuning.DodgeAttackWindow)
		{
			State.ActiveKind = EMDAttackKind::DodgeAttack;
			State.bAttacking = true;
			State.LightStep = -1;
			State.LastDodgeTime = -1000.0;
			return EMDComboAction::PlayDodgeAttack;
		}
		const int32 Count = FMath::Max(Tuning.LightCount, 1);
		const bool bExpired = !State.bAttacking && (Now - State.LastAttackEndTime) > Tuning.ComboResetTime;
		const int32 Next = (State.LightStep < 0 || bExpired) ? 0 : (State.LightStep + 1) % Count;
		State.LightStep = Next;
		State.ActiveKind = EMDAttackKind::Light;
		State.bAttacking = true;
		return EMDComboAction::PlayLight;
	}

	case EMDComboInput::Heavy:
	{
		if (!bCanChain)
		{
			return EMDComboAction::None;
		}
		const bool bCharged = State.BufferedHold >= Tuning.ChargeTime;
		State.Buffered = EMDComboInput::None;
		State.bInComboWindow = false;
		State.bInCancelWindow = false;
		State.LightStep = -1; // a heavy ends the light chain
		State.ActiveKind = bCharged ? EMDAttackKind::Charged : EMDAttackKind::Heavy;
		State.bAttacking = true;
		return bCharged ? EMDComboAction::PlayCharged : EMDComboAction::PlayHeavy;
	}

	default:
		break;
	}
	return EMDComboAction::None;
}

void UMDCombatRules::ApplyDodge(FMDComboState& State, double Now)
{
	State.bAttacking = false;
	State.bInComboWindow = false;
	State.bInCancelWindow = false;
	State.LightStep = -1;
	State.LastDodgeTime = Now;
}

FMDReactionResult UMDCombatRules::ResolveReaction(const FMDReactionInput& In)
{
	using namespace MDCombatRulesLocal;

	FMDReactionResult Out;
	Out.PoiseAfter = In.Poise;
	if (In.bDead)
	{
		return Out; // no reaction, nothing drains
	}

	const float Damage = FMath::Max(In.PoiseDamage, 0.0f);
	Out.PoiseAfter = FMath::Max(In.Poise - Damage, 0.0f); // poise drains even under hyper-armor
	Out.Reaction = In.StepReaction;

	const bool bBroke = Damage > 0.0f && Out.PoiseAfter <= 0.0f;
	if (In.bHyperArmor || In.bStunned || In.bImmune)
	{
		Out.Reaction = Weaker(Out.Reaction, EMDHitReaction::Flinch);
		return Out;
	}

	if (In.bFlinchOnly)
	{
		if (bBroke)
		{
			Out.Reaction = EMDHitReaction::Stagger;
			Out.bStartStun = true;
		}
		else
		{
			Out.Reaction = Weaker(Out.Reaction, EMDHitReaction::Flinch);
		}
	}
	else if (bBroke || Rank(Out.Reaction) >= Rank(EMDHitReaction::Stagger))
	{
		Out.Reaction = Stronger(Out.Reaction, EMDHitReaction::Stagger);
		Out.bStartStun = true;
	}

	if (Out.bStartStun)
	{
		Out.PoiseAfter = In.MaxPoise; // poise resets when the stun starts
	}
	return Out;
}

float UMDCombatRules::TickPoise(const FMDPoiseTickInput& In)
{
	const float Max = FMath::Max(In.MaxPoise, 0.0f);
	const float End = In.TimeSinceHit + In.DeltaTime;
	const float RegenStart = FMath::Max(In.RegenDelay, In.TimeSinceHit);
	const float RegenTime = FMath::Clamp(End - RegenStart, 0.0f, FMath::Max(In.DeltaTime, 0.0f));
	return FMath::Clamp(In.Poise + RegenTime * FMath::Max(In.RegenPerSec, 0.0f), 0.0f, Max);
}

EMDHitDirection UMDCombatRules::HitDirection(const FVector& VictimForward, const FVector& VictimLocation, const FVector& AttackerLocation)
{
	FVector Forward = VictimForward;
	Forward.Z = 0.0f;
	FVector To = AttackerLocation - VictimLocation;
	To.Z = 0.0f;
	if (!Forward.Normalize() || !To.Normalize())
	{
		return EMDHitDirection::Front;
	}
	const float Dot = FVector::DotProduct(Forward, To);
	const float Cross = Forward.X * To.Y - Forward.Y * To.X; // > 0: attacker on the victim's right
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	const float Abs = FMath::Abs(AngleDeg);
	if (Abs <= 45.0f)
	{
		return EMDHitDirection::Front;
	}
	if (Abs > 135.0f)
	{
		return EMDHitDirection::Back;
	}
	return AngleDeg > 0.0f ? EMDHitDirection::Right : EMDHitDirection::Left;
}

FVector UMDCombatRules::KnockbackVelocity(float Strength, const FVector& AttackerLocation, const FVector& VictimLocation, bool bPlayer, float Resist, float LaunchUpRatio)
{
	const float Scaled = FMath::Max(Strength, 0.0f) * (1.0f - FMath::Clamp(Resist, 0.0f, 1.0f));
	if (Scaled <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	FVector Away = VictimLocation - AttackerLocation;
	Away.Z = 0.0f;
	if (!Away.Normalize())
	{
		Away = FVector::ForwardVector;
	}
	FVector Velocity = Away * Scaled;
	Velocity.Z = bPlayer ? 0.0f : Scaled * FMath::Max(LaunchUpRatio, 0.0f);
	return Velocity;
}

float UMDCombatRules::CritPoiseBonus(float PoiseDamage, bool bCrit)
{
	return bCrit ? PoiseDamage * CritPoiseMultiplier : PoiseDamage;
}

float UMDCombatRules::HitStopSeconds(int32 Frames, float Scale, float MaxSeconds)
{
	const float Seconds = static_cast<float>(FMath::Max(Frames, 0)) / 60.0f * FMath::Max(Scale, 0.0f);
	return FMath::Clamp(Seconds, 0.0f, FMath::Max(MaxSeconds, 0.0f));
}

EMDDodgeDirection UMDCombatRules::DodgeDirectionFromInput(const FVector& WorldInput, const FVector& Forward)
{
	FVector Input = WorldInput;
	Input.Z = 0.0f;
	FVector Facing = Forward;
	Facing.Z = 0.0f;
	if (!Input.Normalize() || !Facing.Normalize())
	{
		return EMDDodgeDirection::Backward;
	}
	const float Dot = FVector::DotProduct(Facing, Input);
	const float Cross = Facing.X * Input.Y - Facing.Y * Input.X;
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	const float Abs = FMath::Abs(AngleDeg);
	if (Abs <= 45.0f)
	{
		return EMDDodgeDirection::Forward;
	}
	if (Abs > 135.0f)
	{
		return EMDDodgeDirection::Backward;
	}
	return AngleDeg > 0.0f ? EMDDodgeDirection::Right : EMDDodgeDirection::Left;
}
