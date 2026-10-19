#include "MDPerkCatalog.h"

#ifdef LOCTEXT_NAMESPACE
#undef LOCTEXT_NAMESPACE
#endif
#define LOCTEXT_NAMESPACE "MDPerks"

namespace
{
	int32 PerkPathFirstNode(EMDPerkPath Path)
	{
		return (static_cast<int32>(Path) - 1) * MD_NodesPerPerkPath;
	}

	bool PerkIsRealPath(EMDPerkPath Path)
	{
		return Path >= EMDPerkPath::Defense && Path <= EMDPerkPath::Movement;
	}

	FText PerkNum(float Value)
	{
		// Trim trailing zeros: 6 -> "6", 0.5 -> "0.5".
		FString S = FString::Printf(TEXT("%.2f"), Value);
		while (S.EndsWith(TEXT("0")) && S.Contains(TEXT(".")))
		{
			S.LeftChopInline(1);
		}
		if (S.EndsWith(TEXT(".")))
		{
			S.LeftChopInline(1);
		}
		return FText::FromString(S);
	}
}

// ---- UMDPerkTree ----

int32 UMDPerkTree::GetNodeIndex(EMDPerkPath Path, EMDPerkTier Tier)
{
	if (!PerkIsRealPath(Path) || Tier == EMDPerkTier::None)
	{
		return INDEX_NONE;
	}
	return PerkPathFirstNode(Path) + (static_cast<int32>(Tier) - 1);
}

int32 UMDPerkTree::FindNodeIndex(FName Id) const
{
	return Nodes.IndexOfByPredicate([Id](const FMDPerkNode& N) { return N.Id == Id; });
}

UMDPerkTree::UMDPerkTree()
{
	Nodes = MakeDefaultNodes();
}

TArray<FMDPerkNode> UMDPerkTree::MakeDefaultNodes()
{
	TArray<FMDPerkNode> Out;
	Out.Reserve(MD_NumPerkNodes);

	auto Add = [&Out](const TCHAR* Id, EMDPerkPath Path, EMDPerkTier Tier, EMDPerkEffect Effect, float PerRank, const FText& Name, const FText& Desc)
	{
		FMDPerkNode& N = Out.AddDefaulted_GetRef();
		N.Id = FName(Id);
		N.Path = Path;
		N.Tier = Tier;
		N.Effect = Effect;
		N.MaxRank = (Tier == EMDPerkTier::Capstone) ? 1 : 2;
		N.RequiresPathPoints = (Tier == EMDPerkTier::Tier2) ? 2 : (Tier == EMDPerkTier::Capstone ? 4 : 0);
		N.ValuePerRank = PerRank;
		N.Name = Name;
		N.Description = Desc;
	};

	using P = EMDPerkPath;
	using T = EMDPerkTier;
	using E = EMDPerkEffect;

	// Defense
	Add(TEXT("Hardened"), P::Defense, T::Tier1a, E::DamageReductionPP, 3.0f, LOCTEXT("NHardened", "Hardened"),
		LOCTEXT("DHardened", "+{Value} percentage points damage reduction (total {Total})."));
	Add(TEXT("ThickHide"), P::Defense, T::Tier1b, E::FlatDamageReduction, 2.0f, LOCTEXT("NThickHide", "Thick Hide"),
		LOCTEXT("DThickHide", "Each hit deals {Value} less damage, never below 1 (total {Total})."));
	Add(TEXT("Stalwart"), P::Defense, T::Tier2, E::StalwartDRPP, 5.0f, LOCTEXT("NStalwart", "Stalwart"),
		LOCTEXT("DStalwart", "+{Value} percentage points damage reduction below 50% health (total {Total})."));
	Add(TEXT("Bulwark"), P::Defense, T::Capstone, E::BulwarkHitCap, 0.30f, LOCTEXT("NBulwark", "Bulwark"),
		LOCTEXT("DBulwark", "No single hit takes more than 30% of your maximum health."));

	// Offense
	Add(TEXT("Sharpened"), P::Offense, T::Tier1a, E::AttackPowerPct, 0.06f, LOCTEXT("NSharpened", "Sharpened"),
		LOCTEXT("DSharpened", "+{Value}% attack power (total {Total}%)."));
	Add(TEXT("Precision"), P::Offense, T::Tier1b, E::CritChance, 0.05f, LOCTEXT("NPrecision", "Precision"),
		LOCTEXT("DPrecision", "+{Value}% critical hit chance (total {Total}%)."));
	Add(TEXT("Ferocity"), P::Offense, T::Tier2, E::CritDamage, 0.15f, LOCTEXT("NFerocity", "Ferocity"),
		LOCTEXT("DFerocity", "+{Value}% critical damage (total {Total}%)."));
	Add(TEXT("Executioner"), P::Offense, T::Capstone, E::ExecutionerPct, 0.20f, LOCTEXT("NExecutioner", "Executioner"),
		LOCTEXT("DExecutioner", "+{Value}% damage against targets below 30% health."));

	// Vitality
	Add(TEXT("Robust"), P::Vitality, T::Tier1a, E::MaxHealth, 10.0f, LOCTEXT("NRobust", "Robust"),
		LOCTEXT("DRobust", "+{Value} maximum health (total {Total})."));
	Add(TEXT("Recovery"), P::Vitality, T::Tier1b, E::HealthRegen, 0.5f, LOCTEXT("NRecovery", "Recovery"),
		LOCTEXT("DRecovery", "+{Value} health per second after 5 seconds without taking damage (total {Total})."));
	Add(TEXT("PotentDraughts"), P::Vitality, T::Tier2, E::PotionHealPct, 0.20f, LOCTEXT("NPotentDraughts", "Potent Draughts"),
		LOCTEXT("DPotentDraughts", "+{Value}% potion healing (total {Total}%)."));
	Add(TEXT("SecondWind"), P::Vitality, T::Capstone, E::SecondWind, 0.25f, LOCTEXT("NSecondWind", "Second Wind"),
		LOCTEXT("DSecondWind", "Once per life, survive a lethal hit at 1 health, then heal 25% over 3 seconds."));

	// Movement
	Add(TEXT("Endurance"), P::Movement, T::Tier1a, E::MaxStamina, 10.0f, LOCTEXT("NEndurance", "Endurance"),
		LOCTEXT("DEndurance", "+{Value} maximum stamina (total {Total})."));
	Add(TEXT("QuickBreath"), P::Movement, T::Tier1b, E::StaminaRegenPct, 0.15f, LOCTEXT("NQuickBreath", "Quick Breath"),
		LOCTEXT("DQuickBreath", "+{Value}% stamina regeneration (total {Total}%)."));
	Add(TEXT("LightStep"), P::Movement, T::Tier2, E::StaminaCostPct, 0.10f, LOCTEXT("NLightStep", "Light Step"),
		LOCTEXT("DLightStep", "-{Value}% stamina cost of sprinting, jumping and vaulting (total {Total}%)."));
	Add(TEXT("Fleet"), P::Movement, T::Capstone, E::FleetSprint, 0.05f, LOCTEXT("NFleet", "Fleet"),
		LOCTEXT("DFleet", "+5% sprint speed and stamina regeneration starts 0.5 seconds sooner."));

	return Out;
}

// ---- Points ----

int32 UMDPerkCatalog::PointsInPath(const TArray<uint8>& Ranks, EMDPerkPath Path)
{
	if (!PerkIsRealPath(Path))
	{
		return 0;
	}
	int32 Sum = 0;
	const int32 First = PerkPathFirstNode(Path);
	for (int32 i = First; i < First + MD_NodesPerPerkPath && i < Ranks.Num(); ++i)
	{
		Sum += Ranks[i];
	}
	return Sum;
}

int32 UMDPerkCatalog::TotalRanks(const TArray<uint8>& Ranks)
{
	int32 Sum = 0;
	for (const uint8 Rank : Ranks)
	{
		Sum += Rank;
	}
	return Sum;
}

int32 UMDPerkCatalog::ComputePointsEarned(int32 ZonesCompleted, int32 SoulsSaved, int32 DebugBonusPoints, const FMDPerkTuning& Tuning)
{
	int32 Points = Tuning.StartingPoints + FMath::Clamp(ZonesCompleted, 0, FMath::Max(Tuning.MaxZones, 0)) * Tuning.PointsPerZone;
	for (const int32 Milestone : Tuning.SoulsSavedMilestones)
	{
		if (SoulsSaved >= Milestone)
		{
			++Points;
		}
	}
	return Points + FMath::Max(DebugBonusPoints, 0);
}

// ---- Commit ----

EMDPerkReject UMDPerkCatalog::ValidateCommit(const TArray<uint8>& Current, const TArray<uint8>& Desired, int32 PointsEarned,
	double TimeSinceDamage, bool bDead, const UMDPerkTree& Tree, const FMDPerkTuning& Tuning)
{
	int32 Arg = 0;
	EMDPerkPath Path = EMDPerkPath::None;
	return ValidateCommitEx(Current, Desired, PointsEarned, TimeSinceDamage, bDead, Tree, Tuning, Arg, Path);
}

EMDPerkReject UMDPerkCatalog::ValidateCommitEx(const TArray<uint8>& Current, const TArray<uint8>& Desired, int32 PointsEarned,
	double TimeSinceDamage, bool bDead, const UMDPerkTree& Tree, const FMDPerkTuning& Tuning, int32& OutArg, EMDPerkPath& OutPath)
{
	OutArg = 0;
	OutPath = EMDPerkPath::None;

	// Array length and per-node rank limits.
	if (Desired.Num() != MD_NumPerkNodes || Current.Num() != MD_NumPerkNodes || Tree.Nodes.Num() != MD_NumPerkNodes)
	{
		return EMDPerkReject::InvalidRanks;
	}
	for (int32 i = 0; i < MD_NumPerkNodes; ++i)
	{
		if (Desired[i] > Tree.Nodes[i].MaxRank)
		{
			return EMDPerkReject::InvalidRanks;
		}
	}

	if (bDead)
	{
		return EMDPerkReject::Dead;
	}
	if (TimeSinceDamage < Tuning.SpendDelay)
	{
		OutArg = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Tuning.SpendDelay - TimeSinceDamage)));
		return EMDPerkReject::InCombat;
	}

	// Reducing a rank needs a station respec.
	for (int32 i = 0; i < MD_NumPerkNodes; ++i)
	{
		if (Desired[i] < Current[i])
		{
			return EMDPerkReject::WouldReduceRank;
		}
	}

	if (TotalRanks(Desired) > PointsEarned)
	{
		return EMDPerkReject::NoPoints;
	}

	const int32 PathCap = Tree.Caps.PathPointCap;
	for (int32 PathIndex = 1; PathIndex <= 4; ++PathIndex)
	{
		const EMDPerkPath Path = static_cast<EMDPerkPath>(PathIndex);
		if (PointsInPath(Desired, Path) > PathCap)
		{
			OutArg = PathCap;
			OutPath = Path;
			return EMDPerkReject::PathCap;
		}
	}

	// Prerequisite: points in the path, excluding the node's own ranks, must reach RequiresPathPoints.
	for (int32 i = 0; i < MD_NumPerkNodes; ++i)
	{
		if (Desired[i] == 0)
		{
			continue;
		}
		const FMDPerkNode& Node = Tree.Nodes[i];
		const int32 Others = PointsInPath(Desired, Node.Path) - Desired[i];
		if (Others < Node.RequiresPathPoints)
		{
			OutArg = Node.RequiresPathPoints;
			OutPath = Node.Path;
			return EMDPerkReject::PathLocked;
		}
	}
	return EMDPerkReject::None;
}

// ---- Modifiers ----

void UMDPerkCatalog::ComputeModifiers(const TArray<uint8>& Ranks, const UMDPerkTree& Tree, FMDPerkModifiers& Out, const FMDPerkTuning& Tuning)
{
	Out = FMDPerkModifiers();
	Out.CritDamage = Tuning.BaseCritDamage;

	const int32 Count = FMath::Min(Ranks.Num(), Tree.Nodes.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const FMDPerkNode& Node = Tree.Nodes[i];
		const int32 Rank = FMath::Min<int32>(Ranks[i], Node.MaxRank);
		if (Rank <= 0)
		{
			continue;
		}
		const float Total = Node.ValuePerRank * static_cast<float>(Rank);
		switch (Node.Effect)
		{
		case EMDPerkEffect::DamageReductionPP: Out.DamageReductionPP += Total; break;
		case EMDPerkEffect::FlatDamageReduction: Out.FlatDamageReduction += Total; break;
		case EMDPerkEffect::StalwartDRPP: Out.StalwartPP += Total; break;
		case EMDPerkEffect::BulwarkHitCap: Out.bBulwark = true; break;
		case EMDPerkEffect::AttackPowerPct: Out.AttackPowerPct += Total; break;
		case EMDPerkEffect::CritChance: Out.CritChance += Total; break;
		case EMDPerkEffect::CritDamage: Out.CritDamage += Total; break;
		case EMDPerkEffect::ExecutionerPct: Out.ExecutionerPct += Total; break;
		case EMDPerkEffect::MaxHealth: Out.MaxHealthBonus += Total; break;
		case EMDPerkEffect::HealthRegen: Out.HealthRegen += Total; break;
		case EMDPerkEffect::PotionHealPct: Out.PotionHealPct += Total; break;
		case EMDPerkEffect::SecondWind: Out.bSecondWind = true; break;
		case EMDPerkEffect::MaxStamina: Out.MaxStaminaBonus += Total; break;
		case EMDPerkEffect::StaminaRegenPct: Out.StaminaRegenPct += Total; break;
		case EMDPerkEffect::StaminaCostPct: Out.StaminaCostScale -= Total; break;
		case EMDPerkEffect::FleetSprint:
			Out.SprintSpeedScale += Total;
			Out.RegenDelayReduction += Tuning.FleetRegenDelayReduction * static_cast<float>(Rank);
			break;
		default: break;
		}
	}

	// BALANCE section 7 hard caps.
	const FMDPerkCaps& Caps = Tree.Caps;
	Out.MaxHealthBonus = FMath::Min(Out.MaxHealthBonus, Caps.MaxHealthFromPerks);
	Out.HealthRegen = FMath::Min(Out.HealthRegen, Caps.HealthRegenCap);
	Out.CritChance = FMath::Min(Out.CritChance, Caps.CritChanceCap);
	Out.CritDamage = FMath::Min(Out.CritDamage, Caps.CritDamageCap);
	Out.StaminaCostScale = FMath::Max(Out.StaminaCostScale, Caps.StaminaCostScaleMin);
	Out.StaminaRegenPct = FMath::Min(Out.StaminaRegenPct, Caps.StaminaRegenBonusCap);
	Out.SprintSpeedScale = FMath::Min(Out.SprintSpeedScale, 1.0f + Caps.SprintSpeedBonusCap);
}

float UMDPerkCatalog::CurrentAfterMaxChange(float OldMax, float NewMax, float Current)
{
	if (Current <= 0.0f)
	{
		return Current; // dead stays dead; nothing here revives
	}
	const float Max = FMath::Max(NewMax, 1.0f);
	if (NewMax > OldMax)
	{
		return FMath::Min(Current + (NewMax - OldMax), Max);
	}
	return FMath::Clamp(Current, 1.0f, Max);
}

// ---- Stamina ----

float UMDPerkCatalog::ScaleStaminaCost(float Cost, float Scale, float MinScale)
{
	return Cost * FMath::Clamp(Scale, MinScale, 1.0f);
}

float UMDPerkCatalog::EffectiveRegenDelay(float BaseDelay, float Reduction)
{
	return FMath::Max(0.0f, BaseDelay - Reduction);
}

// ---- Damage ----

bool UMDPerkCatalog::IsCrit(float Chance, float Roll)
{
	return Chance > 0.0f && Roll >= 0.0f && Roll < Chance;
}

float UMDPerkCatalog::ApplyCrit(float Damage, float Chance, float CritMult, float Roll)
{
	return IsCrit(Chance, Roll) ? Damage * FMath::Max(CritMult, 1.0f) : Damage;
}

float UMDPerkCatalog::ApplyExecutioner(float Damage, float TargetHealthFraction, float Pct, float Threshold)
{
	return (Pct > 0.0f && TargetHealthFraction < Threshold) ? Damage * (1.0f + Pct) : Damage;
}

float UMDPerkCatalog::ApplyIncomingDamageRules(float DamageAfterDR, float MaxHealth, float FlatReduction, bool bBulwark, const FMDPerkTuning& Tuning)
{
	float Taken = DamageAfterDR;
	if (FlatReduction > 0.0f)
	{
		// Never below the floor, and never raises a hit that was already smaller than the floor.
		Taken = FMath::Max(Taken - FlatReduction, FMath::Min(Taken, Tuning.ThickHideMinDamage));
	}
	if (bBulwark)
	{
		Taken = FMath::Min(Taken, MaxHealth * Tuning.BulwarkMaxHitFraction);
	}
	return Taken;
}

FMDIncomingDamageResult UMDPerkCatalog::ResolveIncomingDamage(float DamageAfterDR, float Health, float MaxHealth,
	const FMDPerkCombatState& State, const FMDPerkTuning& Tuning)
{
	FMDIncomingDamageResult Result;
	Result.DamageTaken = ApplyIncomingDamageRules(DamageAfterDR, MaxHealth, State.FlatDamageReduction, State.bBulwark, Tuning);
	const float After = Health - Result.DamageTaken;
	if (After <= 0.0f && State.bSecondWindAvailable && Health > 0.0f)
	{
		Result.bSecondWindTriggered = true;
		Result.NewHealth = FMath::Min(1.0f, MaxHealth);
		return Result;
	}
	Result.NewHealth = FMath::Clamp(After, 0.0f, MaxHealth);
	return Result;
}

bool UMDPerkCatalog::IsBelowHealthFraction(float Health, float MaxHealth, float Threshold)
{
	return MaxHealth > 0.0f && (Health / MaxHealth) < Threshold;
}

// ---- Text ----

FText UMDPerkCatalog::GetPathText(EMDPerkPath Path)
{
	switch (Path)
	{
	case EMDPerkPath::Defense: return LOCTEXT("PathDefense", "Defense");
	case EMDPerkPath::Offense: return LOCTEXT("PathOffense", "Offense");
	case EMDPerkPath::Vitality: return LOCTEXT("PathVitality", "Vitality");
	case EMDPerkPath::Movement: return LOCTEXT("PathMovement", "Movement");
	default: return FText::GetEmpty();
	}
}

FText UMDPerkCatalog::GetPerkRejectText(EMDPerkReject Reject, int32 Arg, EMDPerkPath Path)
{
	switch (Reject)
	{
	case EMDPerkReject::InvalidRanks:
		return LOCTEXT("RejectInvalid", "Invalid perk selection");
	case EMDPerkReject::Dead:
		return LOCTEXT("RejectPerkDead", "Cannot spend perks while dead");
	case EMDPerkReject::InCombat:
		return FText::Format(LOCTEXT("RejectCombat", "Can't spend in combat ({0}s)"), FText::AsNumber(Arg));
	case EMDPerkReject::NoPoints:
		return LOCTEXT("RejectNoPoints", "Not enough perk points");
	case EMDPerkReject::WouldReduceRank:
		return LOCTEXT("RejectReduce", "Respec at a station to remove points");
	case EMDPerkReject::PathLocked:
		return FText::Format(LOCTEXT("RejectLocked", "Requires {0} points in {1}"), FText::AsNumber(Arg), GetPathText(Path));
	case EMDPerkReject::PathCap:
		return FText::Format(LOCTEXT("RejectPathCap", "Path limit reached ({0})"), FText::AsNumber(Arg));
	case EMDPerkReject::NotAtStation:
		return LOCTEXT("RejectNotAtStation", "No station in reach");
	default:
		return FText::GetEmpty();
	}
}

FText UMDPerkCatalog::GetEffectDetailText(const FMDPerkNode& Node, int32 Rank)
{
	if (Rank <= 0)
	{
		return FText::GetEmpty();
	}
	const float Total = Node.ValuePerRank * static_cast<float>(Rank);
	switch (Node.Effect)
	{
	case EMDPerkEffect::DamageReductionPP:
		return FText::Format(LOCTEXT("EffDR", "+{0} pp damage reduction"), PerkNum(Total));
	case EMDPerkEffect::FlatDamageReduction:
		return FText::Format(LOCTEXT("EffFlat", "-{0} damage per hit (min 1)"), PerkNum(Total));
	case EMDPerkEffect::StalwartDRPP:
		return FText::Format(LOCTEXT("EffStalwart", "+{0} pp damage reduction below 50% health"), PerkNum(Total));
	case EMDPerkEffect::BulwarkHitCap:
		return LOCTEXT("EffBulwark", "No hit above 30% of max health");
	case EMDPerkEffect::AttackPowerPct:
		return FText::Format(LOCTEXT("EffAP", "+{0}% attack power"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::CritChance:
		return FText::Format(LOCTEXT("EffCritChance", "+{0}% crit chance"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::CritDamage:
		return FText::Format(LOCTEXT("EffCritDamage", "+{0}% crit damage"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::ExecutionerPct:
		return FText::Format(LOCTEXT("EffExecutioner", "+{0}% damage below 30% target health"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::MaxHealth:
		return FText::Format(LOCTEXT("EffMaxHealth", "+{0} max health"), PerkNum(Total));
	case EMDPerkEffect::HealthRegen:
		return FText::Format(LOCTEXT("EffRegen", "+{0} health/s out of combat"), PerkNum(Total));
	case EMDPerkEffect::PotionHealPct:
		return FText::Format(LOCTEXT("EffPotion", "+{0}% potion healing"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::SecondWind:
		return LOCTEXT("EffSecondWind", "Survive one lethal hit per life");
	case EMDPerkEffect::MaxStamina:
		return FText::Format(LOCTEXT("EffMaxStamina", "+{0} max stamina"), PerkNum(Total));
	case EMDPerkEffect::StaminaRegenPct:
		return FText::Format(LOCTEXT("EffStaminaRegen", "+{0}% stamina regen"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::StaminaCostPct:
		return FText::Format(LOCTEXT("EffStaminaCost", "-{0}% stamina cost"), PerkNum(Total * 100.0f));
	case EMDPerkEffect::FleetSprint:
		return LOCTEXT("EffFleet", "+5% sprint speed, faster stamina regen start");
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
