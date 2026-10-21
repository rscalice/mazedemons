#include "MDAbilitySystemComponent.h"

#include "MDAttributeSet.h"
#include "MDDamageEffect.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDGameSettings.h"
#include "MDInventoryComponent.h"
#include "MDPerkCatalog.h"
#include "MDPerkComponent.h"
#include "MDPlayerController.h"
#include "MDPlayerState.h"
#include "MDStatCalculator.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDAbilitySystem, Log, All);

UMDAbilitySystemComponent::UMDAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

UMDAttributeSet* UMDAbilitySystemComponent::GetMazeDemonsAttributes() const
{
	if (MazeDemonsAttributes)
	{
		return MazeDemonsAttributes;
	}
	return const_cast<UMDAttributeSet*>(GetSet<UMDAttributeSet>());
}

void UMDAbilitySystemComponent::RegisterAttributeSet(UMDAttributeSet* InSet)
{
	MazeDemonsAttributes = InSet;
	if (InSet && !GetSet<UMDAttributeSet>())
	{
		AddAttributeSetSubobject(InSet);
	}
}

void UMDAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	BindAttributeDelegates();
}

void UMDAbilitySystemComponent::ResetHealthToMax()
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetHealth(Attrs->GetMaxHealth());
	}
}

float UMDAbilitySystemComponent::GetStamina() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetStamina() : 0.0f;
}

float UMDAbilitySystemComponent::GetMaxStamina() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetMaxStamina() : 0.0f;
}

float UMDAbilitySystemComponent::GetStaminaRegenRate() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetStaminaRegenRate() : 0.0f;
}

void UMDAbilitySystemComponent::InitializeStamina(float NewMaxStamina, float NewRegenRate)
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetMaxStamina(FMath::Max(NewMaxStamina, 1.0f));
		Attrs->SetStaminaRegenRate(FMath::Max(NewRegenRate, 0.0f));
		Attrs->SetStamina(Attrs->GetMaxStamina());
	}
}

void UMDAbilitySystemComponent::ResetStaminaToMax()
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetStamina(Attrs->GetMaxStamina());
	}
}

void UMDAbilitySystemComponent::SetStaminaClamped(float NewStamina)
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Attrs && GetOwner() && GetOwner()->HasAuthority())
	{
		Attrs->SetStamina(FMath::Clamp(NewStamina, 0.0f, Attrs->GetMaxStamina()));
	}
}

float UMDAbilitySystemComponent::GetCritChance() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetCritChance() : 0.0f;
}

float UMDAbilitySystemComponent::GetCritDamage() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetCritDamage() : 1.5f;
}

float UMDAbilitySystemComponent::GetHealthRegen() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetHealthRegen() : 0.0f;
}

float UMDAbilitySystemComponent::GetStaminaCostScale() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetStaminaCostScale() : 1.0f;
}

float UMDAbilitySystemComponent::GetSprintSpeedScale() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetSprintSpeedScale() : 1.0f;
}

float UMDAbilitySystemComponent::GetHealth() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetMaxHealth() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetMaxHealth() : 0.0f;
}

float UMDAbilitySystemComponent::GetAttackPower() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetAttackPower() : 0.0f;
}

float UMDAbilitySystemComponent::GetDefense() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs ? Attrs->GetDefense() : 0.0f;
}

void UMDAbilitySystemComponent::SetArmorPieceDamageReduction(FName ArmorSlot, float DamageReductionPercent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Add(ArmorSlot, FMath::Clamp(DamageReductionPercent, 0.0f, 90.0f));
	RecalculateDefense();
}

void UMDAbilitySystemComponent::RemoveArmorPieceDamageReduction(FName ArmorSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ArmorSlot.IsNone())
	{
		return;
	}

	EquippedArmorDamageReductions.Remove(ArmorSlot);
	RecalculateDefense();
}

float UMDAbilitySystemComponent::ComputeAttackPower(float InBaseAttackPower, const TMap<FName, float>& WeaponBonuses)
{
	// A two-handed weapon occupies both hands, so its bonus replaces the one-handed slots.
	static const FName TwoHanded(TEXT("TwoHanded"));
	static const FName MainHand(TEXT("MainHand"));
	static const FName OffHand(TEXT("OffHand"));
	const bool bTwoHanded = WeaponBonuses.Contains(TwoHanded);

	float Total = InBaseAttackPower;
	for (const TPair<FName, float>& Bonus : WeaponBonuses)
	{
		if (bTwoHanded && (Bonus.Key == MainHand || Bonus.Key == OffHand))
		{
			continue;
		}
		Total += Bonus.Value;
	}
	return FMath::Max(Total, 0.0f);
}

void UMDAbilitySystemComponent::SetWeaponAttackBonus(FName WeaponSlot, float AttackPowerBonus)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || WeaponSlot.IsNone())
	{
		return;
	}

	EquippedWeaponAttackBonuses.Add(WeaponSlot, AttackPowerBonus);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::RemoveWeaponAttackBonus(FName WeaponSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || WeaponSlot.IsNone())
	{
		return;
	}

	EquippedWeaponAttackBonuses.Remove(WeaponSlot);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::SetWeaponModifiers(int32 UpgradeLevel, float CondMult)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	WeaponUpgradeLevel = FMath::Max(0, UpgradeLevel);
	WeaponCondMult = FMath::Max(0.0f, CondMult);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::RecalculateAttackPower()
{
	if (UMDAttributeSet* Attrs = GetMazeDemonsAttributes())
	{
		// Perk attack % multiplies after the weapon maths; at 0 the factor is exactly 1.
		Attrs->SetAttackPower(UMDStatCalculator::EffectiveAttackPower(BaseAttackPower, EquippedWeaponAttackBonuses,
			WeaponUpgradeLevel, WeaponCondMult, UMDGameSettings::GetEconomy()) * (1.0f + PerkAttackPct));
	}
}

void UMDAbilitySystemComponent::SetPerkDamageReduction(float Percent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	static const FName PerkSlot(TEXT("Perk"));
	const float Clamped = FMath::Clamp(Percent, 0.0f, 90.0f);
	if (Clamped > 0.0f)
	{
		EquippedArmorDamageReductions.Add(PerkSlot, Clamped);
	}
	else
	{
		EquippedArmorDamageReductions.Remove(PerkSlot);
	}
	RecalculateDefense();
}

void UMDAbilitySystemComponent::SetPerkAttackPct(float Pct)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	PerkAttackPct = FMath::Max(Pct, 0.0f);
	RecalculateAttackPower();
}

void UMDAbilitySystemComponent::ConsumeSecondWind()
{
	PerkCombat.bSecondWindAvailable = false;
	OnSecondWindTriggered.Broadcast();
}

double UMDAbilitySystemComponent::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

double UMDAbilitySystemComponent::GetTimeSinceLastDamage() const
{
	return GetServerNow() - LastDamageTakenTime;
}

void UMDAbilitySystemComponent::DebugKill(AActor* DamageInstigator)
{
	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (!Attrs || !GetOwner() || !GetOwner()->HasAuthority() || IsDead())
	{
		return;
	}
	LastDamageInstigator = DamageInstigator;
	LastDamageTakenTime = GetServerNow();
	Attrs->SetHealth(0.0f);
}

void UMDAbilitySystemComponent::OnRep_BaseStats()
{
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::ApplyDamage(float Damage, AActor* DamageInstigator, AActor* DamageCauser, bool bAllowCrit)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogMDAbilitySystem, Verbose, TEXT("ApplyDamage ignored on non-authority."));
		return;
	}
	if (Damage <= 0.0f || IsDead())
	{
		return;
	}

	AActor* Target = GetAvatarActor();
	if (!Target)
	{
		Target = GetOwner();
	}
	if (!UMDGameplayStatics::CanDamage(DamageInstigator, Target))
	{
		UE_LOG(LogMDAbilitySystem, Verbose, TEXT("ApplyDamage blocked by team rules: %s -> %s"),
			*GetNameSafe(DamageInstigator), *GetNameSafe(Target));
		return;
	}

	// Attacker side (before the effect): critical hit, then Executioner. Both read the attacker's perk modifiers, which are
	// zero for anyone without perks, so nothing changes for them (and no RNG is consumed unless CritChance > 0).
	float FinalDamage = Damage;
	bool bCrit = false;
	UMDAbilitySystemComponent* AttackerASC = nullptr;
	if (DamageInstigator && DamageInstigator != Target)
	{
		AttackerASC = UMDGameplayStatics::GetMDAbilitySystemComponent(DamageInstigator);
		if (AttackerASC == this)
		{
			AttackerASC = nullptr;
		}
	}
	if (AttackerASC)
	{
		if (bAllowCrit)
		{
			const float Chance = AttackerASC->GetCritChance();
			if (Chance > 0.0f)
			{
				if (UMDPerkComponent* Perk = UMDPerkComponent::FindFor(DamageInstigator))
				{
					bCrit = Perk->RollCrit(Chance);
					if (bCrit)
					{
						FinalDamage = UMDPerkCatalog::ApplyCrit(FinalDamage, 1.0f, AttackerASC->GetCritDamage(), 0.0f);
					}
				}
			}
		}
		const float ExecutionerPct = AttackerASC->GetPerkCombatState().ExecutionerPct;
		if (ExecutionerPct > 0.0f)
		{
			const float MaxHealthNow = GetMaxHealth();
			const float HealthFraction = MaxHealthNow > 0.0f ? GetHealth() / MaxHealthNow : 1.0f;
			FinalDamage = UMDPerkCatalog::ApplyExecutioner(FinalDamage, HealthFraction, ExecutionerPct,
				UMDGameSettings::GetPerkTuning().ExecutionerThreshold);
		}
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddInstigator(DamageInstigator, DamageCauser ? DamageCauser : DamageInstigator);

	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UMDDamageEffect::StaticClass(), 1.0f, Context);
	if (Spec.IsValid() && Spec.Data.IsValid())
	{
		// Set before the effect lands so a lethal hit's OnDeath handlers already see who killed.
		LastDamageInstigator = DamageInstigator;
		const float HealthBefore = GetHealth();
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Damage.GetTag(), FinalDamage);
		ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

		const float Applied = HealthBefore - GetHealth();
		if (Applied > 0.0f)
		{
			LastDamageTakenTime = GetServerNow(); // the 5 s out-of-combat clock for perk spending and Recovery
			OnDamageApplied.Broadcast(DamageInstigator, Applied);

			// Run tab stats (owner-only replicated on the PlayerState). The victim is a player when this ASC lives on a PlayerState.
			AMDPlayerState* VictimState = Cast<AMDPlayerState>(GetOwner());
			if (VictimState)
			{
				VictimState->AddDamageTaken(Applied);
			}
			if (DamageInstigator && DamageInstigator != Target)
			{
				const APawn* AttackerPawn = Cast<APawn>(DamageInstigator);
				AMDPlayerState* AttackerState = AttackerPawn ? AttackerPawn->GetPlayerState<AMDPlayerState>() : nullptr;
				if (AttackerState && AttackerState != VictimState)
				{
					AttackerState->AddDamageDealt(Applied);
					if (!VictimState && IsDead())
					{
						AttackerState->AddKill(); // alive at the top of this function, dead now: this hit killed it
					}
				}
			}
			// Weapon wear for the attacker (the victim's own inventory listens to OnDamageApplied for armor wear).
			UMDInventoryComponent::NotifyHitLanded(DamageInstigator, Target);

			if (bCrit && AttackerASC)
			{
				AttackerASC->OnCritLanded.Broadcast(Target, FinalDamage);
				if (const APawn* AttackerPawn = Cast<APawn>(DamageInstigator))
				{
					if (AMDPlayerController* AttackerController = Cast<AMDPlayerController>(AttackerPawn->GetController()))
					{
						AttackerController->NotifyCrit(FinalDamage);
					}
				}
			}
		}
	}
}

void UMDAbilitySystemComponent::ApplyHealing(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (Amount > 0.0f && Attrs && !IsDead())
	{
		Attrs->SetHealth(FMath::Clamp(Attrs->GetHealth() + Amount, 0.0f, Attrs->GetMaxHealth()));
	}
}

void UMDAbilitySystemComponent::InitializeStats(float NewMaxHealth, float NewAttackPower, float NewBaseDefense)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	if (!Attrs)
	{
		return;
	}

	BaseDefense = FMath::Clamp(NewBaseDefense, 0.0f, 90.0f);
	Attrs->SetMaxHealth(FMath::Max(NewMaxHealth, 1.0f));
	Attrs->SetHealth(Attrs->GetMaxHealth());
	BaseAttackPower = FMath::Max(NewAttackPower, 0.0f);
	RecalculateAttackPower();
	RecalculateDefense();
	OnStatsChanged.Broadcast();
}

FGameplayAbilitySpecHandle UMDAbilitySystemComponent::GrantAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	if (!AbilityClass || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return FGameplayAbilitySpecHandle();
	}
	return GiveAbility(FGameplayAbilitySpec(AbilityClass, Level, INDEX_NONE, GetOwner()));
}

bool UMDAbilitySystemComponent::IsDead() const
{
	const UMDAttributeSet* Attrs = GetMazeDemonsAttributes();
	return Attrs && Attrs->GetHealth() <= 0.0f;
}

void UMDAbilitySystemComponent::RecalculateDefense()
{
	TArray<float> Pieces;
	EquippedArmorDamageReductions.GenerateValueArray(Pieces);

	if (UMDAttributeSet* Attrs = GetMazeDemonsAttributes())
	{
		Attrs->SetDefense(UMDStatCalculator::CombineDR(BaseDefense, Pieces, DefenseCap));
	}
}

void UMDAbilitySystemComponent::BindAttributeDelegates()
{
	if (bDelegatesBound)
	{
		return;
	}
	bDelegatesBound = true;

	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleHealthChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleMaxHealthChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetAttackPowerAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleAttackPowerChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetDefenseAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleDefenseChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetStaminaAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleStaminaChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetMaxStaminaAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleMaxStaminaChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetStaminaRegenRateAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandleStaminaRegenRateChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetCritChanceAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandlePerkAttributeChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetCritDamageAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandlePerkAttributeChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetHealthRegenAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandlePerkAttributeChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetStaminaCostScaleAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandlePerkAttributeChanged);
	GetGameplayAttributeValueChangeDelegate(UMDAttributeSet::GetSprintSpeedScaleAttribute())
		.AddUObject(this, &UMDAbilitySystemComponent::HandlePerkAttributeChanged);
}

void UMDAbilitySystemComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();

	// Death state is server-authoritative; clients receive it through bIsDeadState (RepNotify).
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (ChangeData.NewValue <= 0.0f && !bIsDeadState)
		{
			bIsDeadState = true;
			OnDeath.Broadcast();
		}
		else if (ChangeData.NewValue > 0.0f && bIsDeadState)
		{
			bIsDeadState = false; // respawn / revive
		}
	}
}

void UMDAbilitySystemComponent::OnRep_IsDeadState(bool bOldValue)
{
	if (bIsDeadState && !bOldValue)
	{
		OnDeath.Broadcast();
	}
}

void UMDAbilitySystemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDAbilitySystemComponent, bIsDeadState);
	DOREPLIFETIME_CONDITION(UMDAbilitySystemComponent, BaseDefense, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDAbilitySystemComponent, BaseAttackPower, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMDAbilitySystemComponent, LastDamageTakenTime, COND_OwnerOnly);
}

void UMDAbilitySystemComponent::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnMaxHealthChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData)
{
	OnAttackPowerChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleDefenseChanged(const FOnAttributeChangeData& ChangeData)
{
	OnDefenseChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	OnStaminaChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleStaminaRegenRateChanged(const FOnAttributeChangeData& ChangeData)
{
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandlePerkAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	OnStatsChanged.Broadcast();
}

void UMDAbilitySystemComponent::HandleMaxStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	OnMaxStaminaChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
	OnStatsChanged.Broadcast();

	// A lowered maximum pulls current stamina down with it (authority only; clients receive the result).
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SetStaminaClamped(GetStamina());
	}
}
