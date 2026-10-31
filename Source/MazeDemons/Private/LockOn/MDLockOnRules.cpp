#include "LockOn/MDLockOnRules.h"

namespace MDLockOnRulesLocal
{
	// Sorted (screen X, then id) ids of the on-screen candidates.
	TArray<const FMDLockOnCandidate*> OnScreenByX(const TArray<FMDLockOnCandidate>& Candidates)
	{
		TArray<const FMDLockOnCandidate*> Out;
		for (const FMDLockOnCandidate& C : Candidates)
		{
			if (C.bOnScreen)
			{
				Out.Add(&C);
			}
		}
		Out.Sort([](const FMDLockOnCandidate& A, const FMDLockOnCandidate& B)
		{
			return A.ScreenX < B.ScreenX || (A.ScreenX == B.ScreenX && A.Id < B.Id);
		});
		// TArray::Sort dereferences pointer elements for the predicate, so the lambda above takes references.
		return Out;
	}
}

float FMDLockOnRules::Score(const FMDLockOnCandidate& Candidate, const FMDLockOnTuning& Tuning)
{
	const float Centre = FMath::Clamp(1.0f - Candidate.AngleFromCentreDeg / FMath::Max(Tuning.CentreAngleDeg, 1.0f), 0.0f, 1.0f);
	const float Close = FMath::Clamp(1.0f - Candidate.DistanceCm / FMath::Max(Tuning.MaxRange, 1.0f), 0.0f, 1.0f);
	return Tuning.WeightCentre * Centre + Tuning.WeightDistance * Close
		+ (Candidate.bRecentlyDamagedMe ? Tuning.WeightDamagedMe : 0.0f)
		+ (Candidate.bThreatening ? Tuning.WeightThreat : 0.0f);
}

int32 FMDLockOnRules::PickBest(const TArray<FMDLockOnCandidate>& Candidates, const FMDLockOnTuning& Tuning, bool bFirstLock, float* OutScore)
{
	const FMDLockOnCandidate* Best = nullptr;
	float BestScore = -1.0f;
	for (const FMDLockOnCandidate& C : Candidates)
	{
		if (C.DistanceCm > Tuning.MaxRange || !(C.bOnScreen || (bFirstLock && C.bWithinEdgeSlack)))
		{
			continue;
		}
		const float S = Score(C, Tuning);
		const bool bBetter = !Best || S > BestScore + 1.0e-5f
			|| (FMath::Abs(S - BestScore) <= 1.0e-5f && (C.DistanceCm < Best->DistanceCm || (C.DistanceCm == Best->DistanceCm && C.Id < Best->Id)));
		if (bBetter)
		{
			Best = &C;
			BestScore = S;
		}
	}
	if (OutScore)
	{
		*OutScore = Best ? BestScore : 0.0f;
	}
	return Best ? Best->Id : INDEX_NONE;
}

int32 FMDLockOnRules::PickCycle(const TArray<FMDLockOnCandidate>& Candidates, int32 CurrentId, int32 Direction)
{
	const TArray<const FMDLockOnCandidate*> Sorted = MDLockOnRulesLocal::OnScreenByX(Candidates);
	if (Sorted.Num() == 0)
	{
		return INDEX_NONE;
	}
	const int32 Step = Direction < 0 ? -1 : 1;
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		if (Sorted[i]->Id == CurrentId)
		{
			Index = i;
			break;
		}
	}
	if (Index == INDEX_NONE)
	{
		return Step > 0 ? Sorted[0]->Id : Sorted.Last()->Id;
	}
	return Sorted[(Index + Step + Sorted.Num()) % Sorted.Num()]->Id;
}

void FMDLockOnRules::FindNeighbours(const TArray<FMDLockOnCandidate>& Candidates, int32 CurrentId, int32& OutLeft, int32& OutRight)
{
	OutLeft = INDEX_NONE;
	OutRight = INDEX_NONE;
	const TArray<const FMDLockOnCandidate*> Sorted = MDLockOnRulesLocal::OnScreenByX(Candidates);
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		if (Sorted[i]->Id == CurrentId)
		{
			OutLeft = i > 0 ? Sorted[i - 1]->Id : INDEX_NONE;
			OutRight = i + 1 < Sorted.Num() ? Sorted[i + 1]->Id : INDEX_NONE;
			return;
		}
	}
}

EMDLockBreak FMDLockOnRules::EvaluateBreak(const FMDLockOnBreakInput& Input, float DeltaSeconds, const FMDLockOnTuning& Tuning, float& InOutNoLOSSeconds)
{
	if (Input.bHasLOS)
	{
		InOutNoLOSSeconds = 0.0f;
	}
	else
	{
		InOutNoLOSSeconds += FMath::Max(DeltaSeconds, 0.0f);
	}
	if (Input.bPlayerDead)
	{
		return EMDLockBreak::PlayerDied;
	}
	if (Input.bMenuOpen)
	{
		return EMDLockBreak::MenuOpened;
	}
	if (!Input.bTargetValid)
	{
		return EMDLockBreak::TargetInvalid;
	}
	if (!Input.bTargetAlive)
	{
		return EMDLockBreak::TargetDied;
	}
	if (Input.DistanceCm > Tuning.BreakRange)
	{
		return EMDLockBreak::TooFar;
	}
	if (InOutNoLOSSeconds >= Tuning.NoLOSBreakSeconds)
	{
		return EMDLockBreak::NoLineOfSight;
	}
	return EMDLockBreak::None;
}

bool FMDLockOnRules::ShouldAutoRetarget(EMDLockBreak Reason, float SecondsSincePlayerAttack, float BestCandidateDistanceCm, const FMDLockOnTuning& Tuning)
{
	return Reason == EMDLockBreak::TargetDied && SecondsSincePlayerAttack >= 0.0f && SecondsSincePlayerAttack <= Tuning.AutoRetargetAttackWindow
		&& BestCandidateDistanceCm >= 0.0f && BestCandidateDistanceCm <= Tuning.AutoRetargetRange;
}

bool FMDLockOnRules::UseLockedTargetForWarp(bool bHasMoveInput, const FVector& InputDir, const FVector& ToTarget, float WarpRange, const FMDLockOnTuning& Tuning)
{
	const FVector To(ToTarget.X, ToTarget.Y, 0.0);
	const double Dist = To.Size();
	if (Dist > WarpRange * Tuning.WarpRangeScale)
	{
		return false;
	}
	if (!bHasMoveInput || Dist < 1.0)
	{
		return true;
	}
	const FVector In = FVector(InputDir.X, InputDir.Y, 0.0).GetSafeNormal();
	if (In.IsNearlyZero())
	{
		return true;
	}
	return FVector::DotProduct(In, To / Dist) >= FMath::Cos(FMath::DegreesToRadians(Tuning.WarpAngleDeg));
}

bool FMDLockOnRules::ValidateServerTarget(bool bAlive, bool bHostile, float DistanceCm, float SecondsSinceLOS, const FMDLockOnTuning& Tuning, bool bSoft)
{
	const float Range = bSoft ? Tuning.SoftBreakRange : Tuning.MaxRange;
	return bAlive && bHostile && DistanceCm <= Range + Tuning.ServerRangeTolerance && SecondsSinceLOS <= Tuning.ServerLOSWindow;
}

float FMDLockOnRules::SoftScore(const FMDSoftCandidate& C, const FMDLockOnTuning& Tuning, bool bHasMoveInput)
{
	const float Close = FMath::Clamp(1.0f - C.DistanceCm / FMath::Max(Tuning.SoftRange, 1.0f), 0.0f, 1.0f);
	const float Centre = FMath::Clamp(1.0f - C.CentreOffset, 0.0f, 1.0f);
	float Score = Tuning.SoftWeightDistance * Close + Tuning.SoftWeightCentre * Centre;
	if (bHasMoveInput && C.InputAngleDeg >= 0.0f)
	{
		Score += Tuning.SoftWeightInput * FMath::Clamp(1.0f - C.InputAngleDeg / FMath::Max(Tuning.SoftInputFullDeg, 1.0f), 0.0f, 1.0f);
	}
	return Score;
}

int32 FMDLockOnRules::PickSoftBest(const TArray<FMDSoftCandidate>& Candidates, const FMDLockOnTuning& Tuning, bool bHasMoveInput, float* OutScore)
{
	const FMDSoftCandidate* Best = nullptr;
	float BestScore = -1.0f;
	for (const FMDSoftCandidate& C : Candidates)
	{
		if (!C.bInView || C.DistanceCm > Tuning.SoftRange)
		{
			continue;
		}
		const float S = SoftScore(C, Tuning, bHasMoveInput);
		const bool bBetter = !Best || S > BestScore + 1.0e-5f
			|| (FMath::Abs(S - BestScore) <= 1.0e-5f && (C.DistanceCm < Best->DistanceCm || (C.DistanceCm == Best->DistanceCm && C.Id < Best->Id)));
		if (bBetter)
		{
			Best = &C;
			BestScore = S;
		}
	}
	if (OutScore)
	{
		*OutScore = Best ? BestScore : 0.0f;
	}
	return Best ? Best->Id : INDEX_NONE;
}

bool FMDLockOnRules::IsInView(const FVector2D& Pixel, bool bProjected, const FVector2D& ViewSize, float Margin)
{
	if (!bProjected)
	{
		return false;
	}
	const double MX = ViewSize.X * Margin;
	const double MY = ViewSize.Y * Margin;
	return Pixel.X >= -MX && Pixel.X <= ViewSize.X + MX && Pixel.Y >= -MY && Pixel.Y <= ViewSize.Y + MY;
}

float FMDLockOnRules::CentreOffset(const FVector2D& Pixel, const FVector2D& ViewSize)
{
	const double HX = FMath::Max(ViewSize.X * 0.5, 1.0);
	const double HY = FMath::Max(ViewSize.Y * 0.5, 1.0);
	return static_cast<float>(FMath::Max(FMath::Abs(Pixel.X - HX) / HX, FMath::Abs(Pixel.Y - HY) / HY));
}

bool FMDLockOnRules::HasCandidateInInputCone(const TArray<FMDSoftCandidate>& Candidates, int32 ExcludeId, const FMDLockOnTuning& Tuning)
{
	for (const FMDSoftCandidate& C : Candidates)
	{
		if (C.Id != ExcludeId && C.bInView && C.DistanceCm <= Tuning.SoftRange && C.InputAngleDeg >= 0.0f && C.InputAngleDeg <= Tuning.SoftInputConeDeg)
		{
			return true;
		}
	}
	return false;
}

bool FMDLockOnRules::ShouldSoftRetarget(const FMDSoftRetargetInput& In, const FMDLockOnTuning& Tuning)
{
	if (!In.bTargetValid || !In.bInView || In.DistanceCm > Tuning.SoftBreakRange)
	{
		return true;
	}
	return In.bHasMoveInput && In.TargetInputAngleDeg > Tuning.SoftRetargetInputDeg && In.bOtherInInputCone;
}

float FMDLockOnRules::AssistStep(float ErrorDeg, float Strength01, float SecondsSincePlayerInput, float DeltaSeconds, const FMDLockOnTuning& Tuning)
{
	const float Strength = FMath::Clamp(Strength01, 0.0f, 1.0f);
	if (Strength <= KINDA_SMALL_NUMBER || SecondsSincePlayerInput < Tuning.AssistInputPauseSeconds || DeltaSeconds <= 0.0f)
	{
		return 0.0f;
	}
	const float Alpha = 1.0f - FMath::Exp(-Strength * Tuning.AssistFollowRate * DeltaSeconds);
	return ErrorDeg * FMath::Clamp(Alpha, 0.0f, 1.0f);
}

FVector2D FMDLockOnRules::ScreenErrorDegrees(const FVector2D& TargetPixel, const FVector2D& ViewSize, float HorizontalFovDeg, const FMDLockOnTuning& Tuning)
{
	const double HalfFov = FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDeg, 10.0f, 170.0f) * 0.5f);
	const double Focal = (ViewSize.X * 0.5) / FMath::Tan(HalfFov);
	const double GoalX = ViewSize.X * Tuning.AssistTargetScreenX;
	const double GoalY = ViewSize.Y * Tuning.AssistTargetScreenY;
	const double CX = ViewSize.X * 0.5;
	const double CY = ViewSize.Y * 0.5;
	const double Yaw = FMath::Atan((TargetPixel.X - CX) / Focal) - FMath::Atan((GoalX - CX) / Focal);
	const double Pitch = FMath::Atan((CY - TargetPixel.Y) / Focal) - FMath::Atan((CY - GoalY) / Focal);
	return FVector2D(FMath::RadiansToDegrees(Yaw), FMath::RadiansToDegrees(Pitch));
}

float FMDLockOnRules::ClampAssistPitch(float CurrentPitchDeg, float NewPitchDeg, const FMDLockOnTuning& Tuning)
{
	const float C = Tuning.AssistPitchClampDeg;
	if (FMath::Abs(NewPitchDeg) <= C)
	{
		return NewPitchDeg;
	}
	if (FMath::Abs(CurrentPitchDeg) > C)
	{
		return FMath::Abs(NewPitchDeg) < FMath::Abs(CurrentPitchDeg) ? NewPitchDeg : CurrentPitchDeg;
	}
	return NewPitchDeg < 0.0f ? -C : C;
}

int32 FMDLockOnRules::FFlick::Update(float StickX, float DeltaSeconds, float Deadzone, float RearmSeconds)
{
	SinceFire += FMath::Max(DeltaSeconds, 0.0f);
	const float Abs = FMath::Abs(StickX);
	if (!bArmed && SinceFire >= RearmSeconds && Abs < Deadzone * 0.5f)
	{
		bArmed = true;
	}
	if (bArmed && Abs >= Deadzone)
	{
		bArmed = false;
		SinceFire = 0.0f;
		return StickX < 0.0f ? -1 : 1;
	}
	return 0;
}

float FMDPawnSeparation::Penetration(const FVector& A, float RA, const FVector& B, float RB)
{
	const double Dist = FVector(A.X - B.X, A.Y - B.Y, 0.0).Size();
	return static_cast<float>(RA + RB - Dist);
}

FVector FMDPawnSeparation::ComputePush(const FVector& A, float RA, const FVector& B, float RB, float Skin, float MinPenetration, float Strength, float MaxPush, float ShareA)
{
	const FVector Delta(A.X - B.X, A.Y - B.Y, 0.0);
	const double Dist = Delta.Size();
	const float Pen = static_cast<float>(RA + RB + FMath::Max(Skin, 0.0f) - Dist);
	if (Pen <= FMath::Max(MinPenetration, 0.0f))
	{
		return FVector::ZeroVector;
	}
	const FVector Dir = Dist > 1.0 ? Delta / Dist : FVector(1.0, 0.0, 0.0); // exactly stacked: a deterministic direction
	const float Mag = FMath::Min(Pen * FMath::Max(Strength, 0.0f) * FMath::Clamp(ShareA, 0.0f, 1.0f), FMath::Max(MaxPush, 0.0f));
	return Dir * Mag;
}
