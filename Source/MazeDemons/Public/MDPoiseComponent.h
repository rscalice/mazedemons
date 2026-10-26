#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDCombatRules.h"
#include "MDCombatTypes.h"
#include "MDPoiseComponent.generated.h"

class UAbilitySystemComponent;
class UMDPoiseProfile;
class USkeletalMeshComponent;

// Every machine: a hit reaction was just played (from the replicated LastHit). The server fires it too (V1's brain listens).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDReactionSignature, const FMDHitResultRep&, Hit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDStunChangedSignature, bool, bStunned);

// What ApplyHit decided.
struct FMDPoiseHitResult
{
	bool bValid = false;
	EMDHitReaction Reaction = EMDHitReaction::None;
	EMDHitDirection Direction = EMDHitDirection::Front;
	bool bStunStarted = false;
};

// Hidden poise meter for players, the test dummy and enemies (Decision 3: a replicated component, not a GAS attribute).
// Hits drain poise; at 0 the target is stunned (State.Staggered) and cannot act; poise refills after RegenDelay without hits.
// The server decides; everyone plays the reaction from the replicated LastHit and sees bStunned.
UCLASS(Blueprintable, ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDPoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDPoiseComponent();

	// Poise tuning and reaction montages. Null = the class defaults of UMDPoiseProfile (the player numbers).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poise")
	TObjectPtr<UMDPoiseProfile> Profile;

	// ---- Authority ----
	// Applies one landed hit (after ApplyDamage said damage went through). Returns what happened.
	FMDPoiseHitResult ApplyHit(const FMDHitContext& Context, const FMDDamageResult& Damage);

	// Hyper-armor for Seconds (the swing ignores stagger; flinch at most). 0 clears it.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void SetHyperArmor(float Seconds);

	// Pushes the owner: enemies get the full launch, players only a horizontal slide. Server only.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void ApplyKnockback(FVector Velocity, bool bPlayerSlide);

	// Debug / scripted: sets poise directly (clamped).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void SetPoise(float NewPoise);

	// Debug / scripted: stun for Seconds (<= 0 uses the profile's stun duration). End it early with ClearStun.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void Stun(float Seconds = 0.0f);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void ClearStun();

	// Respawn / reset: poise full, no stun, no hyper-armor.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Maze Demons|Combat")
	void ResetState();

	// ---- Queries (all machines) ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsStunned() const { return bStunned; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	bool IsHyperArmored() const;

	// 0..1. Exact on the server, quantised to percent on clients.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	float GetPoiseFraction() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	float GetMaxPoise() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	FMDHitResultRep GetLastHit() const { return LastHit; }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Combat")
	UMDPoiseProfile* GetProfile() const;

	// The mesh reaction montages play on. Default: the character mesh, else the first skeletal mesh component.
	void SetReactionMesh(USkeletalMeshComponent* Mesh) { ReactionMesh = Mesh; }

	static UMDPoiseComponent* FindFor(const AActor* Actor);

	// True when Actor (a pawn or any actor with a poise component) is currently stunned.
	static bool IsActorStunned(const AActor* Actor);

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDReactionSignature OnReaction;

	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Combat")
	FMDStunChangedSignature OnStunChanged;

	// Server: a knocked-back player is sliding with this velocity. Blueprint can play the slide pose / lower friction.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Combat")
	void OnKnockbackSlide(FVector Velocity);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_LastHit)
	FMDHitResultRep LastHit;

	UPROPERTY(ReplicatedUsing = OnRep_Stunned)
	bool bStunned = false;

	// 0..100, server value for clients' debug and UI.
	UPROPERTY(Replicated)
	uint8 PoisePercent = 100;

	UFUNCTION()
	void OnRep_LastHit();

	UFUNCTION()
	void OnRep_Stunned();

	void HandleLastHit();
	void PlayReaction(const FMDHitResultRep& Hit);
	void SetStunned(bool bNewStunned);
	void ApplyStunTag(bool bOn);
	void StartStun(float Seconds, const UMDPoiseProfile& Config);
	USkeletalMeshComponent* GetReactionMesh() const;
	double ServerNow() const;

	TWeakObjectPtr<USkeletalMeshComponent> ReactionMesh;
	TWeakObjectPtr<UAbilitySystemComponent> TaggedASC;

	// Server state.
	float Poise = -1.0f; // -1 = not initialised yet (full)
	double LastHitTime = -1000.0;
	double StunEndTime = 0.0;
	double ImmuneUntil = 0.0;
	double HyperArmorUntil = 0.0;
	uint8 NextSeq = 0;
};
