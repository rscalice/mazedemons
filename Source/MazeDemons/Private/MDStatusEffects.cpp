#include "MDStatusEffects.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectTypes.h"
#include "MDAttributeSet.h"
#include "MDDamageNumbers.h"
#include "MDGameplayTags.h"

namespace MDStatusLocal
{
	const FMDStatusTuning& NoStatus()
	{
		static const FMDStatusTuning None;
		return None;
	}

	// Two ticks closer than this to the interval count as due (float drift from 0.1 s steps).
	constexpr float TickTolerance = 1.0e-3f;
}

const FMDStatusTuning& FMDStatusRules::GetTuning(EMDStatusType Type)
{
	static const FMDStatusTuning Bleed = []
	{
		FMDStatusTuning T;
		T.Duration = 5.0f;
		T.Ticks = 5;
		T.TickFraction = 0.02f;
		T.MaxStacks = 3;
		return T;
	}();
	static const FMDStatusTuning Frost = []
	{
		FMDStatusTuning T;
		T.Duration = 4.0f;
		T.MoveSpeedScale = 0.75f;
		T.StaminaDrainScale = 1.2f;
		return T;
	}();
	static const FMDStatusTuning Poison = []
	{
		FMDStatusTuning T;
		T.Duration = 8.0f;
		T.Ticks = 8;
		T.TickFraction = 0.015f;
		T.bBlocksStaminaRegen = true;
		return T;
	}();
	static const FMDStatusTuning Burn = []
	{
		FMDStatusTuning T;
		T.Duration = 3.0f;
		T.Ticks = 3;
		T.TickFraction = 0.03f;
		return T;
	}();

	switch (Type)
	{
	case EMDStatusType::Bleed:
		return Bleed;
	case EMDStatusType::Frost:
		return Frost;
	case EMDStatusType::Poison:
		return Poison;
	case EMDStatusType::Burn:
		return Burn;
	default:
		return MDStatusLocal::NoStatus();
	}
}

bool FMDStatusRules::Apply(TArray<FMDStatusRuntime>& Active, EMDStatusType Type, AActor* Instigator)
{
	if (Type == EMDStatusType::None || Type == EMDStatusType::Count)
	{
		return false;
	}
	const FMDStatusTuning& Tuning = GetTuning(Type);
	FMDStatusRuntime* Entry = Active.FindByPredicate([Type](const FMDStatusRuntime& E) { return E.Type == Type; });
	const bool bNew = Entry == nullptr;
	if (bNew)
	{
		Entry = &Active.AddDefaulted_GetRef();
		Entry->Type = Type;
		Entry->Stacks = 1;
	}
	else
	{
		Entry->Stacks = FMath::Min(Entry->Stacks + 1, FMath::Max(Tuning.MaxStacks, 1));
	}
	Entry->Duration = Tuning.Duration;
	Entry->Remaining = Tuning.Duration;
	Entry->TicksLeft = Tuning.Ticks;
	if (Instigator)
	{
		Entry->Instigator = Instigator;
	}
	return bNew;
}

void FMDStatusRules::Advance(TArray<FMDStatusRuntime>& Active, float DeltaSeconds, TArray<FMDStatusTick>& OutTicks, TArray<EMDStatusType>* OutExpired)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}
	for (int32 Index = 0; Index < Active.Num();)
	{
		FMDStatusRuntime& Entry = Active[Index];
		const FMDStatusTuning& Tuning = GetTuning(Entry.Type);
		Entry.Remaining -= DeltaSeconds;

		if (Tuning.Ticks > 0)
		{
			const float Interval = Tuning.TickInterval();
			Entry.TickAccum += DeltaSeconds;
			while (Entry.TicksLeft > 0 && Entry.TickAccum + MDStatusLocal::TickTolerance >= Interval)
			{
				FMDStatusTick& Tick = OutTicks.AddDefaulted_GetRef();
				Tick.Type = Entry.Type;
				Tick.Stacks = Entry.Stacks;
				Tick.Fraction = Tuning.TickFraction * static_cast<float>(Entry.Stacks);
				Tick.Instigator = Entry.Instigator;
				Entry.TickAccum -= Interval;
				--Entry.TicksLeft;
			}
			if (Entry.TicksLeft <= 0)
			{
				Entry.TickAccum = 0.0f;
			}
		}

		if (Entry.Remaining <= MDStatusLocal::TickTolerance)
		{
			if (OutExpired)
			{
				OutExpired->Add(Entry.Type);
			}
			Active.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
}

float FMDStatusRules::TickDamage(float MaxHealth, EMDStatusType Type, int32 Stacks)
{
	const FMDStatusTuning& Tuning = GetTuning(Type);
	return FMath::Max(MaxHealth, 0.0f) * Tuning.TickFraction * static_cast<float>(FMath::Clamp(Stacks, 0, FMath::Max(Tuning.MaxStacks, 1)));
}

float FMDStatusRules::MoveSpeedScale(const TArray<FMDStatusRep>& Active)
{
	float Scale = 1.0f;
	for (const FMDStatusRep& Entry : Active)
	{
		Scale = FMath::Min(Scale, GetTuning(Entry.Type).MoveSpeedScale);
	}
	return Scale;
}

float FMDStatusRules::StaminaDrainScale(const TArray<FMDStatusRep>& Active)
{
	float Scale = 1.0f;
	for (const FMDStatusRep& Entry : Active)
	{
		Scale *= GetTuning(Entry.Type).StaminaDrainScale;
	}
	return Scale;
}

bool FMDStatusRules::BlocksStaminaRegen(const TArray<FMDStatusRep>& Active)
{
	for (const FMDStatusRep& Entry : Active)
	{
		if (GetTuning(Entry.Type).bBlocksStaminaRegen)
		{
			return true;
		}
	}
	return false;
}

FGameplayTag FMDStatusRules::GetStatusTag(EMDStatusType Type)
{
	switch (Type)
	{
	case EMDStatusType::Bleed:
		return TAG_Status_Bleed.GetTag();
	case EMDStatusType::Frost:
		return TAG_Status_Frost.GetTag();
	case EMDStatusType::Poison:
		return TAG_Status_Poison.GetTag();
	case EMDStatusType::Burn:
		return TAG_Status_Burn.GetTag();
	default:
		return FGameplayTag();
	}
}

FGameplayTag FMDStatusRules::GetDamageType(EMDStatusType Type)
{
	switch (Type)
	{
	case EMDStatusType::Bleed:
		return TAG_Damage_Type_Bleed.GetTag();
	case EMDStatusType::Frost:
		return TAG_Damage_Type_Frost.GetTag();
	case EMDStatusType::Poison:
		return TAG_Damage_Type_Poison.GetTag();
	case EMDStatusType::Burn:
		return TAG_Damage_Type_Fire.GetTag();
	default:
		return TAG_Damage_Type_Physical.GetTag();
	}
}

FText FMDStatusRules::GetDisplayName(EMDStatusType Type)
{
	switch (Type)
	{
	case EMDStatusType::Bleed:
		return NSLOCTEXT("MDStatus", "Bleed", "Bleed");
	case EMDStatusType::Frost:
		return NSLOCTEXT("MDStatus", "Frost", "Frost");
	case EMDStatusType::Poison:
		return NSLOCTEXT("MDStatus", "Poison", "Poison");
	case EMDStatusType::Burn:
		return NSLOCTEXT("MDStatus", "Burn", "Burn");
	default:
		return FText::GetEmpty();
	}
}

FLinearColor FMDStatusRules::GetColor(EMDStatusType Type)
{
	return MDDamageNumbers::DefaultTypeColor(GetDamageType(Type));
}

// ---- Gameplay effects ----

UMDDoTEffect::UMDDoTEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UMDDoTExecCalc::StaticClass();
	Executions.Add(Execution);
}

float UMDDoTExecCalc::ComputeDamage(float IsDoTFlag, float RawDamage)
{
	return IsDoTFlag > 0.0f ? FMath::Max(RawDamage, 0.0f) : 0.0f;
}

void UMDDoTExecCalc::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const float IsDoT = Spec.GetSetByCallerMagnitude(TAG_Data_IsDoT.GetTag(), false, 0.0f);
	const float Raw = Spec.GetSetByCallerMagnitude(TAG_Data_Damage.GetTag(), false, 0.0f);
	const float Damage = ComputeDamage(IsDoT, Raw);
	if (Damage > 0.0f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UMDAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage));
	}
}

UMDStatusEffectBase::UMDStatusEffectBase()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}

void UMDStatusEffectBase::GrantTag(const FGameplayTag& Tag)
{
	// Constructor-time component creation must go through CreateDefaultSubobject (FindOrAddComponent uses NewObject and asserts here).
	UTargetTagsGameplayEffectComponent* TagComponent = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantedStatusTag"));
	FInheritedTagContainer Granted;
	Granted.Added.AddTag(Tag);
	TagComponent->SetAndApplyTargetTagChanges(Granted);
	GEComponents.Add(TagComponent);
}

UMDStatusEffect_Bleed::UMDStatusEffect_Bleed()
{
	GrantTag(TAG_Status_Bleed.GetTag());
}

UMDStatusEffect_Frost::UMDStatusEffect_Frost()
{
	GrantTag(TAG_Status_Frost.GetTag());
}

UMDStatusEffect_Poison::UMDStatusEffect_Poison()
{
	GrantTag(TAG_Status_Poison.GetTag());
}

UMDStatusEffect_Burn::UMDStatusEffect_Burn()
{
	GrantTag(TAG_Status_Burn.GetTag());
}
