#include "MDComboSet.h"

#include "Animation/AnimMontage.h"

FMDComboTuning UMDComboSet::MakeTuning() const
{
	FMDComboTuning Tuning;
	Tuning.LightCount = LightChain.Num();
	Tuning.InputBufferTime = InputBufferTime;
	Tuning.ComboResetTime = ComboResetTime;
	Tuning.ChargeTime = ChargeTime;
	Tuning.DodgeAttackWindow = DodgeAttackWindow;
	return Tuning;
}

bool UMDComboSet::GetStep(EMDAttackKind Kind, int32 Index, FMDAttackStep& OutStep) const
{
	switch (Kind)
	{
	case EMDAttackKind::Light:
		if (LightChain.IsValidIndex(Index))
		{
			OutStep = LightChain[Index];
			return true;
		}
		return false;
	case EMDAttackKind::Heavy:
		OutStep = Heavy;
		return true;
	case EMDAttackKind::Charged:
		// A set without a charged swing falls back to the heavy.
		OutStep = Charged.Montage ? Charged : Heavy;
		return true;
	case EMDAttackKind::DodgeAttack:
		// A set without a dodge attack falls back to the first light step.
		if (DodgeAttack.Montage)
		{
			OutStep = DodgeAttack;
			return true;
		}
		if (LightChain.Num() > 0)
		{
			OutStep = LightChain[0];
			return true;
		}
		return false;
	default:
		return false;
	}
}

UAnimMontage* UMDComboSet::GetDodgeMontage(EMDDodgeDirection Direction) const
{
	const int32 Index = static_cast<int32>(Direction);
	return DodgeMontages.IsValidIndex(Index) ? DodgeMontages[Index].Get() : nullptr;
}

bool UMDComboSet::Validate(TArray<FString>& OutErrors) const
{
	const int32 StartCount = OutErrors.Num();
	const FString Name = GetName();

	auto CheckStep = [&](const FMDAttackStep& Step, const FString& Label, bool bRequireMontage)
	{
		if (bRequireMontage && !Step.Montage)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s has no montage"), *Name, *Label));
		}
		if (Step.DamageMult <= 0.0f)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s DamageMult must be > 0"), *Name, *Label));
		}
		if (Step.PoiseDamage < 0.0f)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s PoiseDamage is negative"), *Name, *Label));
		}
		if (Step.HitStopFrames < 0 || Step.HitStopFrames > 12)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s HitStopFrames %d is outside 0..12"), *Name, *Label, Step.HitStopFrames));
		}
		if (Step.Hand != EMDWeaponHand::Main && !bDualWield)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s uses the off hand or both hands but the set is not bDualWield"), *Name, *Label));
		}
		if (Step.ComboWindowStart > Step.ComboWindowEnd)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s combo window start is after its end"), *Name, *Label));
		}
	};

	if (LightChain.Num() < 1 || LightChain.Num() > 6)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: light chain has %d steps (expected 1..6)"), *Name, LightChain.Num()));
	}
	for (int32 Index = 0; Index < LightChain.Num(); ++Index)
	{
		CheckStep(LightChain[Index], FString::Printf(TEXT("light step %d"), Index), true);
	}
	CheckStep(Heavy, TEXT("heavy"), true);
	if (Charged.Montage)
	{
		CheckStep(Charged, TEXT("charged"), true);
	}
	if (DodgeAttack.Montage)
	{
		CheckStep(DodgeAttack, TEXT("dodge attack"), true);
	}
	if (ChargeTime <= 0.0f)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: ChargeTime must be > 0"), *Name));
	}
	if (InputBufferTime <= 0.0f || InputBufferTime > 1.0f)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: InputBufferTime %.2f is outside (0, 1]"), *Name, InputBufferTime));
	}
	if (ComboResetTime <= 0.0f)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: ComboResetTime must be > 0"), *Name));
	}
	if (DodgeMontages.Num() != 0 && DodgeMontages.Num() != 4)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: DodgeMontages must be empty or have 4 entries (Forward, Backward, Left, Right)"), *Name));
	}
	if (DodgeIFrames < 0.0f || DodgeIFrames > 0.6f)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: DodgeIFrames %.2f is outside 0..0.6"), *Name, DodgeIFrames));
	}
	return OutErrors.Num() == StartCount;
}
