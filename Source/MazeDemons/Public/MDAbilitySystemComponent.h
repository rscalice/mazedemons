#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "MDCombatTypes.h"
#include "MDPerkTypes.h"
#include "MDAbilitySystemComponent.generated.h"

class UMDAttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDAttributeValueChanged, float, OldValue, float, NewValue);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDDeathSignature);

// Fires after any attribute or base-stat change (health, stamina, attack power, defense, replicated base values).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDStatsChanged);

// Server only: damage actually reduced Health by Applied (> 0). Instigator may be null (environment).
DECLARE_MULTICAST_DELEGATE_TwoParams(FMDDamageAppliedNative, AActor* /*Instigator*/, float /*Applied*/);

// Server only: this ASC's owner landed a critical hit on Target; FinalDamage is the raw damage after the crit and Executioner.
DECLARE_MULTICAST_DELEGATE_TwoParams(FMDCritLandedNative, AActor* /*Target*/, float /*FinalDamage*/);

// Server only: Second Wind just saved the owner from a lethal hit (health is already 1).
DECLARE_MULTICAST_DELEGATE(FMDSecondWindNative);

UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UMDAbilitySystemComponent();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	UMDAttributeSet* GetMazeDemonsAttributes() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetAttackPower() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetDefense() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetCritChance() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetCritDamage() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetHealthRegen() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetStaminaCostScale() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetSprintSpeedScale() const;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void SetArmorPieceDamageReduction(FName ArmorSlot, float DamageReductionPercent);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void RemoveArmorPieceDamageReduction(FName ArmorSlot);

	// Defense before armor (the base damage-reduction %). Replicated to the owner so the stats screen works on clients.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetBaseDefense() const { return BaseDefense; }

	// Attack power before weapon bonuses. Replicated to the owner.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetBaseAttackPower() const { return BaseAttackPower; }

	// Server-only. Weapon slot names: "MainHand", "OffHand", "TwoHanded". Recalculates the AttackPower attribute.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void SetWeaponAttackBonus(FName WeaponSlot, float AttackPowerBonus);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void RemoveWeaponAttackBonus(FName WeaponSlot);

	// ---- Perks (server only; UMDPerkComponent pushes these from the full modifier set on every change) ----

	// Damage reduction percentage points from perks (Hardened + Stalwart while active). One more entry ("Perk") in the armor
	// map, so CombineDR and the player cap apply as for any piece. 0 removes it.
	void SetPerkDamageReduction(float Percent);

	// Attack power multiplier from perks (0.12 = +12%). Recalculates the AttackPower attribute.
	void SetPerkAttackPct(float Pct);
	float GetPerkAttackPct() const { return PerkAttackPct; }

	// Thick Hide / Bulwark / Second Wind / Executioner. Never replicated: only the server mutates Health.
	void SetPerkCombatState(const FMDPerkCombatState& State) { PerkCombat = State; }
	const FMDPerkCombatState& GetPerkCombatState() const { return PerkCombat; }

	// Called by the attribute set when Second Wind saved the owner: clears the availability flag and fires OnSecondWindTriggered.
	void ConsumeSecondWind();

	FMDSecondWindNative OnSecondWindTriggered;

	// Server only; fires after ApplyDamage landed a critical hit with this ASC's owner as the instigator.
	FMDCritLandedNative OnCritLanded;

	// Server clock (AGameStateBase::GetServerWorldTimeSeconds) when this ASC last lost Health. Replicated to the owner only so
	// the Perks panel can show the 5 s countdown. -1000 = never.
	double GetLastDamageTakenTime() const { return LastDamageTakenTime; }

	// Seconds since Health was last reduced (server value on the server, owner mirror on the owning client). Large when never.
	double GetTimeSinceLastDamage() const;

	// The clock LastDamageTakenTime is measured on.
	double GetServerNow() const;

	// Debug: kills the owner outright (ignores Bulwark and Second Wind). Server only.
	void DebugKill(AActor* DamageInstigator);

	// Upper bound of the Defense attribute (damage reduction %). 90 for enemies and dummies; AMDPlayerState lowers it to the
	// player cap from the balance data.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze Demons|GAS", meta = (ClampMin = "0", ClampMax = "90"))
	float DefenseCap = 90.0f;

	// Server-only. Upgrade level and condition multiplier of the weapon in use (TwoHanded, else MainHand). Recalculates
	// the AttackPower attribute: ComputeAttackPower * (1 + upgrade %) * condition.
	void SetWeaponModifiers(int32 UpgradeLevel, float CondMult);

	// Server only; fires after ApplyDamage reduced Health. Wear and kill credit hang off this.
	FMDDamageAppliedNative OnDamageApplied;

	// Who dealt the most recent damage that reached Health (server only; may be null or stale after the pawn is destroyed).
	AActor* GetLastDamageInstigator() const { return LastDamageInstigator.Get(); }

	// Pure rule: Base + sum of weapon bonuses (never negative). A "TwoHanded" bonus replaces the "MainHand" and "OffHand" ones.
	static float ComputeAttackPower(float InBaseAttackPower, const TMap<FName, float>& WeaponBonuses);

	// Applies raw damage through GAS; Defense mitigation is applied by the attribute set.
	// Server-only. Instigator/causer feed team checks (friendly fire) and the effect context. bAllowCrit = false for DoT and
	// debug self-damage; a crit also needs instigator != target and an instigator with CritChance > 0.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void ApplyDamage(float Damage, AActor* DamageInstigator = nullptr, AActor* DamageCauser = nullptr, bool bAllowCrit = true);

	// C1 melee hit: same rules as the overload above (team check, crit, Executioner, damage effect, stats, kill report, wear,
	// events), with Damage = Context.BaseDamage * Context.Step.DamageMult and crit allowed. Additionally skipped while the target
	// has State.Invulnerable (dodge i-frames). Fills OutResult; returns true when Health went down. Server only.
	bool ApplyDamage(const FMDHitContext& Context, FMDDamageResult& OutResult);

	// V1 WP6, server only. One damage-over-time tick (Bleed, Poison, Burn): Damage is the raw amount; Defense still reduces it. Same team
	// rules, dodge i-frames, kill credit and run stats as a hit, but it never crits, never uses Executioner, wears no armor or weapon (it
	// broadcasts OnDoTDamageApplied, not OnDamageApplied, so the durability listeners never hear it) and is applied with UMDDoTEffect /
	// UMDDoTExecCalc. DamageType is a Damage.Type.* tag (copied to OutResult). Returns true when Health went down.
	bool ApplyDoTDamage(float Damage, AActor* DamageInstigator, FGameplayTag DamageType, FMDDamageResult& OutResult);

	// Server only; a DoT tick reduced Health by Applied (> 0). Armor wear listens to OnDamageApplied and deliberately not to this.
	FMDDamageAppliedNative OnDoTDamageApplied;

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void ApplyHealing(float Amount);

	// Sets base stats (e.g. for enemies). Health is filled to the new max.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	void InitializeStats(float NewMaxHealth, float NewAttackPower, float NewBaseDefense);

	// Grants an ability and returns its handle. Activate with TryActivateAbilityByClass.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|GAS")
	FGameplayAbilitySpecHandle GrantAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	bool IsDead() const;

	// Server-only: refills Health to MaxHealth (used on respawn).
	void ResetHealthToMax();

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|GAS")
	float GetStaminaRegenRate() const;

	// Server-only: sets the stamina pool and regen rate; stamina is filled to the new max.
	void InitializeStamina(float NewMaxStamina, float NewRegenRate);

	// Server-only: refills Stamina to MaxStamina (used on respawn).
	void ResetStaminaToMax();

	// Server-only: sets Stamina directly (clamped to [0, MaxStamina]). Used by UMDStaminaComponent.
	void SetStaminaClamped(float NewStamina);

	// Stores the attribute set (created as a default subobject by the owner actor).
	void RegisterAttributeSet(UMDAttributeSet* InSet);

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDDeathSignature OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnMaxHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnAttackPowerChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnDefenseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDAttributeValueChanged OnMaxStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|GAS")
	FMDStatsChanged OnStatsChanged;

	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Server sets this when Health reaches 0 (and clears it when Health > 0). Replicated so every
	// client fires OnDeath exactly once per death via OnRep_IsDeadState.
	UPROPERTY(ReplicatedUsing = OnRep_IsDeadState)
	bool bIsDeadState = false;

	UFUNCTION()
	void OnRep_IsDeadState(bool bOldValue);

	// The shared damage path behind both ApplyDamage overloads. OutResult may be null.
	// bWearAttackerWeapon = false for ranged / beam hits (a gun wears its own durability, never the melee weapon).
	// bDoT = a status effect tick (see ApplyDoTDamage): no crit, no Executioner, no wear, applied with the DoT effect.
	void ApplyDamageCore(float Damage, AActor* DamageInstigator, AActor* DamageCauser, bool bAllowCrit, FMDDamageResult* OutResult, bool bWearAttackerWeapon = true, bool bDoT = false);

	void BindAttributeDelegates();
	bool bDelegatesBound = false;

	void RecalculateDefense();
	void RecalculateAttackPower();

	UFUNCTION()
	void OnRep_BaseStats();

	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleAttackPowerChanged(const FOnAttributeChangeData& ChangeData);
	void HandleDefenseChanged(const FOnAttributeChangeData& ChangeData);
	void HandleStaminaChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxStaminaChanged(const FOnAttributeChangeData& ChangeData);
	void HandleStaminaRegenRateChanged(const FOnAttributeChangeData& ChangeData);
	void HandlePerkAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY()
	TObjectPtr<UMDAttributeSet> MazeDemonsAttributes;

	TMap<FName, float> EquippedArmorDamageReductions;

	TMap<FName, float> EquippedWeaponAttackBonuses;

	int32 WeaponUpgradeLevel = 0;
	float WeaponCondMult = 1.0f;
	TWeakObjectPtr<AActor> LastDamageInstigator;

	float PerkAttackPct = 0.0f;
	FMDPerkCombatState PerkCombat;

	UPROPERTY(Replicated)
	double LastDamageTakenTime = -1000.0;

	// Owner-only replicated so clients can show base vs. total on the character stats screen.
	UPROPERTY(ReplicatedUsing = OnRep_BaseStats)
	float BaseDefense = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_BaseStats)
	float BaseAttackPower = 0.0f;
};
