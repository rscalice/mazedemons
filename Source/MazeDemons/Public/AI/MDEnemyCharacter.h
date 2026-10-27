#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GenericTeamAgentInterface.h"
#include "MDCombatTypes.h"
#include "MDEnemyCharacter.generated.h"

class UMDAbilitySystemComponent;
class UMDBlinkComponent;
class UMDCombatFeedbackComponent;
class UMDEnemyArchetype;
class UMDEnemyAttributeSet;
class UMDPoiseComponent;
class UMDTargetableComponent;

// Base class of every V1 enemy (v1-design 2.2). Blueprint children carry defaults only (mesh, anim class, Archetype asset).
//
// It reuses the C1 and U-series systems rather than adding parallel ones:
//   hits       UMDCombatStatics::ApplyMeleeHit -> UMDAbilitySystemComponent::ApplyDamage -> UMDPoiseComponent::ApplyHit
//   death      the ASC's replicated dead state fires OnDeath on every machine; each one ragdolls through AMDPlayerState::RagdollPawn
//   quest      the ASC's kill report reads GetOwnedGameplayTags (the archetype tag) -> UMDQuestComponent::ReportKill (zone check there)
//   kill gold  UMDGameplayStatics::AwardKillGold (U2 split) with the archetype's KillGold
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDEnemyCharacter : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AMDEnemyCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// Takes a plain damage event (e.g. UGameplayStatics::ApplyDamage) through the ability system, like the test dummy.
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// Stats, poise profile and tag for this kind of enemy. Safe on every machine; health and attributes are set on the server only.
	UFUNCTION(BlueprintCallable, Category = "Enemy")
	void ApplyArchetype(UMDEnemyArchetype* InArchetype);

	UFUNCTION(BlueprintPure, Category = "Enemy")
	bool IsEnemyDead() const { return bDeathHandled; }

	// Gold the kill awards (the archetype's, never negative).
	int32 GetKillGoldAward() const;

	// Server. Who gets the kill: the controller of the last damage instigator (a pawn or a controller itself), else null.
	static const AController* GetKillerController(const AActor* LastDamageInstigator);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDEnemyAttributeSet> Attributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDPoiseComponent> Poise;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDBlinkComponent> Blink;

	// Inert on enemies (it acts for the local player's controller); present so enemy Blueprints can reuse its hit-flash and
	// impact hooks.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDCombatFeedbackComponent> Feedback;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDTargetableComponent> Targetable;

	// What kind of enemy this is. Set on the Blueprint class; a spawner may set it before FinishSpawning (replicated once).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Archetype, Category = "Enemy")
	TObjectPtr<UMDEnemyArchetype> Archetype;

	// Current intent (EMDIntent) for clients' animation and tells. Reserved: nothing writes it before the V1 brain, but it is
	// in the class now so later work packages do not change the replicated layout.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy|AI")
	uint8 ReplicatedIntent = 0;

	// Seconds the corpse stays before it is destroyed (server). 0 or less keeps it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy", meta = (ClampMin = "0"))
	float CorpseLifetime = 15.0f;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Archetype();

private:
	// Bound to the ASC's OnDeath on every machine (the server fires it directly, clients through the replicated dead state).
	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleReaction(const FMDHitResultRep& Hit);

	void AwardKillRewards();

	bool bDeathHandled = false;
};
