#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GenericTeamAgentInterface.h"
#include "AI/MDAITypes.h"
#include "MDCombatTypes.h"
#include "MDEnemyCharacter.generated.h"

class UMDAbilitySystemComponent;
class UMDBlinkComponent;
class UMDEnemyBehaviourComponent;
class UMDCombatFeedbackComponent;
class UMDEnemyArchetype;
class UMDEnemyAttackComponent;
class UMDEnemyAttributeSet;
class UMDPoiseComponent;
class UMDStatusEffectComponent;
class UMDTargetableComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMotionWarpingComponent;

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

	// V1 WP6: Bleed / Frost / Poison / Burn on this enemy (replicated; the WP8 health bars read it).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDStatusEffectComponent> StatusEffects;

	// Inert on enemies (it acts for the local player's controller); present so enemy Blueprints can reuse its hit-flash and
	// impact hooks.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDCombatFeedbackComponent> Feedback;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDTargetableComponent> Targetable;

	// Plays the archetype's attacks (V1 WP1): montage multicast, motion warp to the target, claw traces.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDEnemyAttackComponent> AttackComponent;

	// V1 WP5: timers and the long actions of the advanced behaviours (feint, blink, charge, ranged draw), the behaviour cues.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMDEnemyBehaviourComponent> Behaviour;

	// V1 WP5 ambush: hidden, collision-free and dormant until the marker's trigger fires. Replicated so a client never sees
	// (or bumps into) an ambusher that has not arrived yet.
	UPROPERTY(ReplicatedUsing = OnRep_AmbushHidden, BlueprintReadOnly, Category = "Enemy|AI")
	bool bAmbushHidden = false;

	// Server: hide / reveal the enemy for an ambush. Revealing restores walking.
	void SetAmbushHidden(bool bNewHidden);
	bool IsAmbushHidden() const { return bAmbushHidden; }

	// "AttackTarget" warp for the attack montages (the same warp name the player's combo steps use).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMotionWarpingComponent> MotionWarping;

	// What kind of enemy this is. Set on the Blueprint class; a spawner may set it before FinishSpawning (replicated once).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Archetype, Category = "Enemy")
	TObjectPtr<UMDEnemyArchetype> Archetype;

	// Current intent (EMDIntent) for clients' animation and tells. The brain writes it on the server; late joiners get the
	// current value on first replication.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy|AI")
	uint8 ReplicatedIntent = 0;

	// Server: the brain's current intent, mirrored for clients.
	void SetReplicatedIntent(EMDIntent Intent);

	// ---- Off-screen threat (V1 WP4, v1-design 1.3) ----
	// True while this enemy holds an attack token on ThreatTarget and is about to swing (or swinging). Replicated so a client
	// knows which enemies threaten it; a late joiner gets the current values on first replication. The local player's
	// UMDThreatIndicatorSubsystem draws the screen-edge arrow for threatening enemies that are off-screen.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy|AI")
	bool bThreatening = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<AActor> ThreatTarget;

	// Server: the brain says who this enemy threatens (null = nobody). Only writes on a change.
	void SetThreat(AActor* Target);

	// ---- Attack tell (V1 WP4) ----
	// Every machine, from the MDAN_Tell anim notify at the start of an attack wind-up: the archetype's TellSound at low volume
	// plus a brief overlay flash on the enemy. Cosmetic; a dedicated server skips it. The montage is multicast, so the notify
	// fires on each machine and the cue reaches everyone (and a late joiner simply sees the next tell).
	// bWithSound = false: the flash only (the V1 WP5 behaviour cues bring their own sound).
	void PlayTellCue(bool bWithSound = true);

	// Every enemy that exists on this machine (server and clients), for the threat indicator.
	static const TArray<TWeakObjectPtr<AMDEnemyCharacter>>& GetAllEnemies();

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	EMDIntent GetIntent() const;

	// The facing rule for every enemy (server only; the rotation replicates with the movement):
	//   FaceMovement  while chasing or idle: turn with the movement direction (bOrientRotationToMovement, RotationRate yaw),
	//                 no controller focus; with no velocity the facing is frozen (the swing goes where it was aimed).
	//   FaceTarget    while holding the threat ring or winding up an attack: the controller focuses the target and the
	//                 movement component turns toward the controller rotation at RotationRate (bUseControllerDesiredRotation).
	// Motion warping still owns the facing inside the attack's warp window; both aim at the same target.
	void FaceMovement();
	void FaceTarget(AActor* Target);
	bool IsFacingTarget() const { return bFacingTarget; }

	// Level-placed enemies with the same non-empty SpawnGroupId form one pack (V1 WP2) and share a retreat threshold. Empty =
	// a loose enemy (a pack of one). Spawners that know their group call UMDPackDirectorSubsystem::RegisterSpawnGroup instead.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	FName SpawnGroupId;

	// Seconds the corpse stays before it is destroyed (server). 0 or less keeps it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy", meta = (ClampMin = "0"))
	float CorpseLifetime = 15.0f;

	// Collision rules shared by every enemy, on the capsule and the mesh: Visibility BLOCKS (the weapon sweeps, the interact
	// and aim traces) and Camera IGNORES (the player's camera probe must pass through enemies, or the view collapses into
	// the player's shoulder whenever an enemy stands close). Called from the constructor and again from PostInitializeComponents
	// because a Blueprint subclass serializes its own collision settings over the constructor's.
	void ApplyCollisionResponses();

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_Archetype();

	UFUNCTION()
	void OnRep_AmbushHidden();

private:
	// Bound to the ASC's OnDeath on every machine (the server fires it directly, clients through the replicated dead state).
	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleReaction(const FMDHitResultRep& Hit);

	void AwardKillRewards();

	void EndTellFlash();

	void ApplyAmbushHidden();

	bool bDeathHandled = false;
	bool bFacingTarget = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TellFlashMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TellFlashPreviousOverlay;

	FTimerHandle TellFlashTimer;
};
