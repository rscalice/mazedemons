#include "MDPerkComponent.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDGameplayTags.h"
#include "MDGameSettings.h"
#include "MDGameState.h"
#include "MDPerkCatalog.h"
#include "MDPerkEffect.h"
#include "MDPlayerController.h"
#include "MDPlayerState.h"
#include "MDStaminaComponent.h"
#include "MDStation.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#ifdef LOCTEXT_NAMESPACE
#undef LOCTEXT_NAMESPACE
#endif
#define LOCTEXT_NAMESPACE "MDPerkComponent"

// XORed into the per-player crit seed so tests and 2P checks can repeat a roll sequence.
static TAutoConsoleVariable<int32> CVarMDCritSeed(
	TEXT("md.CritSeed"), 0,
	TEXT("Seed XORed with the player id for the per-player crit stream (0 = player id only)."),
	ECVF_Default);

namespace
{
	constexpr float MDPerkRegenInterval = 0.5f;
	constexpr float MDSecondWindTickInterval = 0.25f;
}

UMDPerkComponent::UMDPerkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Ranks.Init(0, MD_NumPerkNodes);
}

UMDPerkComponent* UMDPerkComponent::FindFor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDPerkComponent* Own = Actor->FindComponentByClass<UMDPerkComponent>())
	{
		return Own;
	}
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		PS = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		PS = Controller->PlayerState;
	}
	return PS ? PS->FindComponentByClass<UMDPerkComponent>() : nullptr;
}

void UMDPerkComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMDPerkComponent, Ranks, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDPerkComponent, DebugBonusPoints, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDPerkComponent, bSecondWindUsed, COND_OwnerOnly);
}

void UMDPerkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(RegenTimer);
		Timers.ClearTimer(SecondWindTimer);
	}
	Super::EndPlay(EndPlayReason);
}

// ---- Lookups ----

bool UMDPerkComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

AMDPlayerState* UMDPerkComponent::GetPlayerState() const
{
	return Cast<AMDPlayerState>(GetOwner());
}

UMDAbilitySystemComponent* UMDPerkComponent::GetASC() const
{
	const AMDPlayerState* PS = GetPlayerState();
	return PS ? PS->GetMDAbilitySystemComponent() : nullptr;
}

UMDStaminaComponent* UMDPerkComponent::GetStamina() const
{
	const AMDPlayerState* PS = GetPlayerState();
	return PS ? PS->StaminaComponent.Get() : nullptr;
}

bool UMDPerkComponent::IsOwnerDead() const
{
	const UMDAbilitySystemComponent* ASC = GetASC();
	return ASC && ASC->IsDead();
}

void UMDPerkComponent::EnsureRanks()
{
	if (Ranks.Num() != MD_NumPerkNodes)
	{
		Ranks.SetNumZeroed(MD_NumPerkNodes);
	}
}

void UMDPerkComponent::SendToast(const FText& Message, EMDColor Tone) const
{
	const AMDPlayerState* PS = GetPlayerState();
	if (AMDPlayerController* PC = PS ? Cast<AMDPlayerController>(PS->GetPlayerController()) : nullptr)
	{
		PC->NotifyToast(Message, Tone);
	}
}

// ---- Queries ----

int32 UMDPerkComponent::GetPointsEarned() const
{
	int32 Zones = 0;
	int32 Souls = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AMDGameState* State = World->GetGameState<AMDGameState>())
		{
			Zones = State->ZonesCompleted;
			Souls = State->SoulsSaved;
		}
	}
	return UMDPerkCatalog::ComputePointsEarned(Zones, Souls, DebugBonusPoints, UMDGameSettings::GetPerkTuning());
}

int32 UMDPerkComponent::GetPointsSpent() const
{
	return UMDPerkCatalog::TotalRanks(Ranks);
}

int32 UMDPerkComponent::GetPointsAvailable() const
{
	return FMath::Max(0, GetPointsEarned() - GetPointsSpent());
}

int32 UMDPerkComponent::GetRank(int32 Node) const
{
	return Ranks.IsValidIndex(Node) ? static_cast<int32>(Ranks[Node]) : 0;
}

int32 UMDPerkComponent::GetPathPoints(EMDPerkPath Path) const
{
	return UMDPerkCatalog::PointsInPath(Ranks, Path);
}

FMDPerkModifiers UMDPerkComponent::ComputeCurrentModifiers() const
{
	FMDPerkModifiers Mods;
	UMDPerkCatalog::ComputeModifiers(Ranks, *UMDGameSettings::GetPerkTree(), Mods, UMDGameSettings::GetPerkTuning());
	return Mods;
}

float UMDPerkComponent::GetPotionHealScale() const
{
	return 1.0f + ComputeCurrentModifiers().PotionHealPct;
}

float UMDPerkComponent::GetTimeSinceDamage() const
{
	const UMDAbilitySystemComponent* ASC = GetASC();
	return ASC ? static_cast<float>(FMath::Min(ASC->GetTimeSinceLastDamage(), 1.0e6)) : 1.0e6f;
}

EMDPerkReject UMDPerkComponent::GetCommitReject(const TArray<uint8>& Desired) const
{
	return UMDPerkCatalog::ValidateCommit(Ranks, Desired, GetPointsEarned(), GetTimeSinceDamage(), IsOwnerDead(),
		*UMDGameSettings::GetPerkTree(), UMDGameSettings::GetPerkTuning());
}

EMDPerkReject UMDPerkComponent::GetRespecReject(const AActor* Station) const
{
	const AMDStation* StationActor = Cast<AMDStation>(Station);
	if (!StationActor || !StationActor->Supports(EMDStationService::Respec))
	{
		return EMDPerkReject::NotAtStation;
	}
	if (IsOwnerDead())
	{
		return EMDPerkReject::Dead;
	}
	const AMDPlayerState* PS = GetPlayerState();
	const AActor* Body = PS ? PS->GetPawn() : nullptr;
	if (!Body)
	{
		Body = GetOwner();
	}
	if (!Body || !AMDStation::IsWithinRange(Body->GetActorLocation(), StationActor->GetActorLocation(), UMDGameSettings::GetEconomy().StationRange))
	{
		return EMDPerkReject::NotAtStation;
	}
	return EMDPerkReject::None;
}

void UMDPerkComponent::OnRep_Perks()
{
	OnPerksChanged.Broadcast();
}

// ---- Requests ----

void UMDPerkComponent::RequestCommit(const TArray<uint8>& Desired)
{
	if (IsServer())
	{
		Commit_Impl(Desired);
	}
	else
	{
		Server_CommitPerks(Desired);
	}
}

bool UMDPerkComponent::Server_CommitPerks_Validate(const TArray<uint8>& Desired)
{
	return Desired.Num() == MD_NumPerkNodes;
}

void UMDPerkComponent::Server_CommitPerks_Implementation(const TArray<uint8>& Desired)
{
	Commit_Impl(Desired);
}

void UMDPerkComponent::RequestRespec(AActor* Station)
{
	if (IsServer())
	{
		Respec_Impl(Station);
	}
	else
	{
		Server_Respec(Station);
	}
}

void UMDPerkComponent::Server_Respec_Implementation(AActor* Station)
{
	Respec_Impl(Station);
}

bool UMDPerkComponent::Commit_Impl(const TArray<uint8>& Desired)
{
	if (!IsServer())
	{
		return false;
	}
	EnsureRanks();

	int32 Arg = 0;
	EMDPerkPath Path = EMDPerkPath::None;
	const EMDPerkReject Reject = UMDPerkCatalog::ValidateCommitEx(Ranks, Desired, GetPointsEarned(), GetTimeSinceDamage(),
		IsOwnerDead(), *UMDGameSettings::GetPerkTree(), UMDGameSettings::GetPerkTuning(), Arg, Path);
	if (Reject != EMDPerkReject::None)
	{
		SendToast(UMDPerkCatalog::GetPerkRejectText(Reject, Arg, Path), EMDColor::Warning);
		return false;
	}

	Ranks = Desired;
	ApplyToASC();
	SendToast(LOCTEXT("PerksUpdated", "Perks updated"), EMDColor::Success);
	return true;
}

bool UMDPerkComponent::Respec_Impl(AActor* Station)
{
	if (!IsServer())
	{
		return false;
	}
	const EMDPerkReject Reject = GetRespecReject(Station);
	if (Reject != EMDPerkReject::None)
	{
		SendToast(UMDPerkCatalog::GetPerkRejectText(Reject), EMDColor::Warning);
		return false;
	}

	const int32 Returned = GetPointsSpent();
	Ranks.Init(0, MD_NumPerkNodes);
	ApplyToASC();
	SendToast(FText::Format(LOCTEXT("PerksReset", "Perks reset ({0} points returned)"), FText::AsNumber(Returned)), EMDColor::Success);
	return true;
}

// ---- Server lifecycle ----

int32 UMDPerkComponent::ComputeCritSeed(int32 PlayerId, int32 CvarSeed)
{
	return PlayerId ^ CvarSeed;
}

void UMDPerkComponent::BindToASC()
{
	UMDAbilitySystemComponent* ASC = GetASC();
	if (!ASC || bBoundToASC)
	{
		return;
	}
	bBoundToASC = true;
	ASC->OnHealthChanged.AddUniqueDynamic(this, &UMDPerkComponent::HandleHealthChanged);
	ASC->OnSecondWindTriggered.AddUObject(this, &UMDPerkComponent::HandleSecondWindTriggered);
}

void UMDPerkComponent::InitializeForRun()
{
	if (!IsServer())
	{
		return;
	}

	Ranks.Init(0, MD_NumPerkNodes);
	DebugBonusPoints = 0;
	bSecondWindUsed = false;
	SecondWindHealRemaining = 0.0f;

	const AMDPlayerState* PS = GetPlayerState();
	CritStream.Initialize(ComputeCritSeed(PS ? PS->GetPlayerId() : 0, CVarMDCritSeed.GetValueOnGameThread()));
	bCritSeeded = true;

	BindToASC();
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(SecondWindTimer);
		if (!Timers.IsTimerActive(RegenTimer))
		{
			Timers.SetTimer(RegenTimer, this, &UMDPerkComponent::TickRegen, MDPerkRegenInterval, true);
		}
	}
	ApplyToASC();
}

void UMDPerkComponent::ResetLifeFlags()
{
	if (!IsServer())
	{
		return;
	}
	bSecondWindUsed = false;
	SecondWindHealRemaining = 0.0f;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SecondWindTimer);
	}
	PushCombatState();
	RecomputeStalwartFlag();
	PushDefense();
	OnPerksChanged.Broadcast();
}

bool UMDPerkComponent::RollCrit(float Chance)
{
	if (Chance <= 0.0f)
	{
		return false;
	}
	if (!bCritSeeded)
	{
		const AMDPlayerState* PS = GetPlayerState();
		CritStream.Initialize(ComputeCritSeed(PS ? PS->GetPlayerId() : 0, CVarMDCritSeed.GetValueOnGameThread()));
		bCritSeeded = true;
	}
	return UMDPerkCatalog::IsCrit(Chance, CritStream.FRand());
}

// ---- Applying ----

bool UMDPerkComponent::RecomputeStalwartFlag()
{
	const UMDAbilitySystemComponent* ASC = GetASC();
	const bool bNew = ASC && AppliedMods.StalwartPP > 0.0f
		&& UMDPerkCatalog::IsBelowHealthFraction(ASC->GetHealth(), ASC->GetMaxHealth(), UMDGameSettings::GetPerkTuning().StalwartHealthThreshold);
	const bool bChanged = bNew != bStalwartActive;
	bStalwartActive = bNew;
	return bChanged;
}

void UMDPerkComponent::PushDefense()
{
	if (UMDAbilitySystemComponent* ASC = GetASC())
	{
		ASC->SetPerkDamageReduction(AppliedMods.DamageReductionPP + (bStalwartActive ? AppliedMods.StalwartPP : 0.0f));
	}
}

void UMDPerkComponent::PushCombatState()
{
	if (UMDAbilitySystemComponent* ASC = GetASC())
	{
		FMDPerkCombatState State;
		State.FlatDamageReduction = AppliedMods.FlatDamageReduction;
		State.bBulwark = AppliedMods.bBulwark;
		State.bSecondWindAvailable = AppliedMods.bSecondWind && !bSecondWindUsed;
		State.ExecutionerPct = AppliedMods.ExecutionerPct;
		ASC->SetPerkCombatState(State);
	}
}

void UMDPerkComponent::HandleHealthChanged(float OldValue, float NewValue)
{
	if (IsServer() && RecomputeStalwartFlag())
	{
		PushDefense(); // only the DR piece, only when the flag flips
	}
}

void UMDPerkComponent::ApplyToASC()
{
	if (!IsServer())
	{
		return;
	}
	UMDAbilitySystemComponent* ASC = GetASC();
	UMDAttributeSet* Attrs = ASC ? ASC->GetMazeDemonsAttributes() : nullptr;
	if (!ASC || !Attrs)
	{
		return;
	}
	EnsureRanks();
	BindToASC();

	AppliedMods = ComputeCurrentModifiers();
	const FMDPerkModifiers& Mods = AppliedMods;

	const float OldMaxHealth = ASC->GetMaxHealth();
	const float OldMaxStamina = ASC->GetMaxStamina();

	// One infinite effect, replaced on every change. No ranks = no effect at all, so a fresh character stays at its defaults.
	if (PerkEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(PerkEffectHandle);
		PerkEffectHandle = FActiveGameplayEffectHandle();
	}
	if (UMDPerkCatalog::TotalRanks(Ranks) > 0)
	{
		const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(UMDPerkEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (SpecHandle.IsValid() && SpecHandle.Data.IsValid())
		{
			const UMDStaminaComponent* Stamina = GetStamina();
			const float BaseRegen = Stamina ? Stamina->RegenPerSec : 20.0f;

			FGameplayEffectSpec& Spec = *SpecHandle.Data.Get();
			// Deltas from the attribute base values; every magnitude is set (0 when unused).
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_MaxHealth.GetTag(), Mods.MaxHealthBonus);
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_MaxStamina.GetTag(), Mods.MaxStaminaBonus);
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_StaminaRegen.GetTag(), BaseRegen * Mods.StaminaRegenPct);
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_CritChance.GetTag(), Mods.CritChance - Attrs->CritChance.GetBaseValue());
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_CritDamage.GetTag(), Mods.CritDamage - Attrs->CritDamage.GetBaseValue());
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_HealthRegen.GetTag(), Mods.HealthRegen - Attrs->HealthRegen.GetBaseValue());
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_StaminaCostScale.GetTag(), Mods.StaminaCostScale - Attrs->StaminaCostScale.GetBaseValue());
			Spec.SetSetByCallerMagnitude(TAG_Data_Perk_SprintSpeed.GetTag(), Mods.SprintSpeedScale - Attrs->SprintSpeedScale.GetBaseValue());
			PerkEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
		}
	}

	// A higher maximum adds the difference to the current value (no free refill); a lower one clamps it.
	if (!ASC->IsDead())
	{
		const float NewMaxHealth = ASC->GetMaxHealth();
		if (NewMaxHealth != OldMaxHealth)
		{
			Attrs->SetHealth(UMDPerkCatalog::CurrentAfterMaxChange(OldMaxHealth, NewMaxHealth, ASC->GetHealth()));
		}
	}
	const float NewMaxStamina = ASC->GetMaxStamina();
	if (NewMaxStamina != OldMaxStamina)
	{
		ASC->SetStaminaClamped(UMDPerkCatalog::CurrentAfterMaxChange(OldMaxStamina, NewMaxStamina, ASC->GetStamina()));
	}

	RecomputeStalwartFlag();
	PushDefense();
	ASC->SetPerkAttackPct(Mods.AttackPowerPct);
	PushCombatState();
	if (UMDStaminaComponent* Stamina = GetStamina())
	{
		Stamina->SetRegenDelayModifier(Mods.RegenDelayReduction);
	}
	OnPerksChanged.Broadcast();
}

// ---- Timers ----

void UMDPerkComponent::TickRegen()
{
	UMDAbilitySystemComponent* ASC = GetASC();
	if (!IsServer() || !ASC || ASC->IsDead())
	{
		return;
	}
	const float Regen = ASC->GetHealthRegen();
	if (Regen <= 0.0f || ASC->GetHealth() >= ASC->GetMaxHealth())
	{
		return;
	}
	if (ASC->GetTimeSinceLastDamage() < UMDGameSettings::GetPerkTuning().RegenOutOfCombat)
	{
		return;
	}
	ASC->ApplyHealing(Regen * MDPerkRegenInterval);
}

void UMDPerkComponent::HandleSecondWindTriggered()
{
	if (!IsServer())
	{
		return;
	}
	bSecondWindUsed = true;
	PushCombatState();

	const FMDPerkTuning& Tuning = UMDGameSettings::GetPerkTuning();
	const UMDAbilitySystemComponent* ASC = GetASC();
	const float MaxHealth = ASC ? ASC->GetMaxHealth() : 0.0f;
	SecondWindHealRemaining = FMath::Max(Tuning.SecondWindHealSeconds, 0.0f);
	SecondWindHealPerSecond = SecondWindHealRemaining > 0.0f ? Tuning.SecondWindHealFraction * MaxHealth / SecondWindHealRemaining : 0.0f;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SecondWindTimer, this, &UMDPerkComponent::TickSecondWind, MDSecondWindTickInterval, true);
	}
	SendToast(LOCTEXT("SecondWindToast", "Second Wind"), EMDColor::Success);
	OnPerksChanged.Broadcast();
}

void UMDPerkComponent::TickSecondWind()
{
	UMDAbilitySystemComponent* ASC = GetASC();
	if (!ASC || ASC->IsDead() || SecondWindHealRemaining <= 0.0f)
	{
		SecondWindHealRemaining = 0.0f;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SecondWindTimer);
		}
		return;
	}
	const float Step = FMath::Min(MDSecondWindTickInterval, SecondWindHealRemaining);
	SecondWindHealRemaining -= Step;
	ASC->ApplyHealing(SecondWindHealPerSecond * Step);
	if (SecondWindHealRemaining <= 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SecondWindTimer);
		}
	}
}

// ---- Debug ----

void UMDPerkComponent::DebugAddPoints(int32 Delta)
{
	if (!IsServer())
	{
		return;
	}
	DebugBonusPoints = FMath::Clamp(DebugBonusPoints + Delta, 0, 1000);
	OnPerksChanged.Broadcast();
}

void UMDPerkComponent::DebugSetRank(int32 Node, int32 Rank)
{
	if (!IsServer() || Node < 0 || Node >= MD_NumPerkNodes)
	{
		return;
	}
	EnsureRanks();
	const UMDPerkTree* Tree = UMDGameSettings::GetPerkTree();
	const int32 MaxRank = Tree->Nodes.IsValidIndex(Node) ? Tree->Nodes[Node].MaxRank : 2;
	Ranks[Node] = static_cast<uint8>(FMath::Clamp(Rank, 0, MaxRank));
	ApplyToASC();
}

void UMDPerkComponent::DebugReset()
{
	if (!IsServer())
	{
		return;
	}
	Ranks.Init(0, MD_NumPerkNodes);
	ApplyToASC();
}

#undef LOCTEXT_NAMESPACE
