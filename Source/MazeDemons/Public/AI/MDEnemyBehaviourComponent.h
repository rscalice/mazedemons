#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/MDAITypes.h"
#include "MDEnemyBehaviourComponent.generated.h"

class AMDEnemyCharacter;
class USoundBase;

// The barks and cosmetic cues of the V1 WP5 behaviours. A cue is played on every machine (NetMulticast), so a client sees and
// hears the same tells as the host: the tell-cue path of WP4 (sound + brief overlay flash), extended with a body "kick".
// Sounds are the Fab MonsterRoarsAndGrowls cues, referenced by path only.
UENUM(BlueprintType)
enum class EMDBehaviourCue : uint8
{
	SwarmHowl = 0,  // 17_Bloodthirsty_Growl_Cue  - the pack swarm bark
	Taunt,          // 25_Warning_Growl_Cue       - the bait's taunt
	FallBack,       // the retreat bark
	AmbushGrowl,    // 12_Ambush_Growl_Cue        - 0.75 s before the ambushers appear
	FeintJerk,      // a visible jerk + a distinct sound as the feint wind-up is cancelled
	FakeFlinch,     // the bait's fake stagger (T3+)
	PreBlink,       // 0.4 s before a blink
	BlinkArrive,
	Brace,          // punish greed: the braced stance
	ChargeScrape,   // the brute scrapes the ground before a charge
	RangedDraw      // the archer's draw (tell)
};

// Which long action is running (they are mutually exclusive and commit the brain: it does not re-score until they end).
UENUM(BlueprintType)
enum class EMDBehaviourAction : uint8
{
	None = 0,
	Feint,
	PreBlink,
	ChargeLineUp,
	Charging,
	ChargeRecover,
	Draw
};

// Server: a long action ended. Completed = false when it was cancelled (stagger, death, target lost).
DECLARE_MULTICAST_DELEGATE_TwoParams(FMDBehaviourActionEndedNative, EMDIntent /*Intent*/, bool /*bCompleted*/);

// Per-enemy state of the V1 WP5 behaviours (v1-design 1.2). The decisions are the scorer's and the rules' in MDBehaviourRules;
// the StateTree tasks are thin and call Begin* here. This component only keeps timers and runs the multi-step actions:
//   Feint   start the attack wind-up, cancel it with a visible jerk, wait 0.3 - 0.6 s, the brain then attacks for real;
//   Blink   0.4 s pre-blink cue, re-validate the arrival point, UMDBlinkComponent::TeleportTo (server), 0.3 s grace;
//   Charge  line up, then a committed straight run; a sideways dodge-slide cancels it;
//   Draw    a ranged enemy's arrow draw (the tell), then UMDRangedStatics::FireFromData;
// plus the no-attack grace (ambush arrival 0.5 s, blink arrival 0.3 s) and the cached flank / blink points.
// All movement is server-only (v1-design 2.8): clients get the cues and the replicated capsule.
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDEnemyBehaviourComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDEnemyBehaviourComponent();

	static UMDEnemyBehaviourComponent* FindFor(const AActor* Actor);

	// ---- Cues ----
	// Server: plays the cue on every machine.
	void PlayCue(EMDBehaviourCue Cue);

	// The sound asset path a cue plays (null path = silent), for tests and the report.
	static FSoftObjectPath GetCueSoundPath(EMDBehaviourCue Cue);

	// Every machine: a brief body kick (feint jerk, fake flinch). Cosmetic.
	void StartKick(float Strength, float DurationSeconds);

	// ---- No-attack grace ----
	// Server. The enemy may not start an attack until Seconds from now (an ambusher arriving 0.5 s, a blink 0.3 s).
	void StartGrace(double Seconds, bool bAmbushArrival = false);
	bool IsInGrace() const;
	bool IsInAmbushArrival() const;
	double GetGraceEnd() const { return GraceEnd; }

	// ---- Long actions (server) ----
	bool IsBusy() const { return Action != EMDBehaviourAction::None; }
	EMDBehaviourAction GetAction() const { return Action; }
	// Aborts the running action without finishing it (stagger, death).
	void CancelAction();

	// Starts the attack wind-up of Attacks[AttackIndex] and cancels it after MDBehaviour::FeintCancelAfter with a jerk; the
	// action ends 0.3 - 0.6 s later (DelayRoll 0..1 picks the delay). False when it cannot start.
	bool BeginFeint(int32 AttackIndex, AActor* Target, float TellTime, float DelayRoll);

	// 0.4 s pre-blink, then the teleport. Arrival defaults to the cached nav-valid point. False when none is valid.
	bool BeginBlink(AActor* Target);

	// Line up and charge Target. False when the archetype cannot or the line cannot be set up.
	bool BeginCharge(AActor* Target);

	// The archer's draw (DrawTime) then the shot. False when busy, on cooldown or the archetype has no ranged data.
	bool BeginRangedShot(AActor* Target, float DrawTime);

	// ---- Cooldowns and caches (server) ----
	bool CanBlinkNow(double Now) const;
	double GetLastBlinkTime() const { return LastBlinkTime; }
	bool CanChargeNow(double Now) const;
	bool CanShootNow(double Now) const { return Now >= NextShotTime; }
	double LastRepositionTime = -1000.0;
	void NoteReposition(double Now) { LastRepositionTime = Now; }

	// Throttled (0.6 s) recomputation of the flank point behind Target / the blink arrival point near it.
	void RefreshFlankPoint(AActor* Target, double Now);
	void RefreshBlinkArrival(AActor* Target, double Now);
	bool GetFlankPoint(FVector& Out) const { Out = FlankPoint; return bHasFlankPoint; }
	bool GetBlinkArrival(FVector& Out) const { Out = BlinkArrival; return bHasBlinkArrival; }

	// ---- Bait timers ----
	double NextTauntTime = 0.0;
	double NextFlinchTime = 0.0;

	// ---- Punish greed ----
	// Server: the braced stance (hyper armor for MDBehaviour::BraceDuration + the glow cue).
	void BeginBrace();
	bool IsBraced() const;

	// Fired on the server when a long action ends.
	FMDBehaviourActionEndedNative OnActionEnded;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCue(EMDBehaviourCue Cue);

	void PlayCueLocal(EMDBehaviourCue Cue);
	AMDEnemyCharacter* GetEnemy() const;
	double Now() const;
	void Finish(bool bCompleted);
	void EnterAction(EMDBehaviourAction NewAction);
	void UpdateTickEnabled();
	void TickKick(float DeltaTime);
	void TickAction(double InNow);
	void TickFeint(double InNow);
	void TickPreBlink(double InNow);
	void TickCharge(double InNow, float DeltaTime);
	void TickDraw(double InNow);
	void EndCharge(bool bCancelledBySidestep);
	void RestoreChargeSpeed();
	EMDIntent IntentOfAction() const;

	EMDBehaviourAction Action = EMDBehaviourAction::None;
	double ActionStart = 0.0;
	TWeakObjectPtr<AActor> ActionTarget;

	// Grace
	double GraceEnd = -1000.0;
	bool bAmbushArrival = false;

	// Feint
	bool bFeintCancelled = false;
	float FeintCancelAt = 0.35f;
	float FeintRecover = 0.45f;

	// Blink
	double LastBlinkTime = -1000.0;
	FVector BlinkArrival = FVector::ZeroVector;
	bool bHasBlinkArrival = false;
	double NextBlinkRefresh = 0.0;

	// Flank cache
	FVector FlankPoint = FVector::ZeroVector;
	bool bHasFlankPoint = false;
	double NextFlankRefresh = 0.0;

	// Charge
	FVector ChargeOrigin = FVector::ZeroVector;
	FVector ChargeDir = FVector::ForwardVector;
	float ChargeLength = 0.0f;
	float ChargeSavedSpeed = 0.0f;
	bool bChargeSpeedApplied = false;
	double LastChargeEnd = -1000.0;
	double ChargeRecoverTime = 0.8;

	// Draw
	float DrawTime = 1.2f;
	double NextShotTime = 0.0;

	// Brace
	double BraceEnd = -1000.0;

	// Cosmetic kick (every machine)
	float KickRemaining = 0.0f;
	float KickDuration = 0.0f;
	float KickStrength = 0.0f;
	bool bKickBaseCaptured = false;
	FVector KickBaseLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<USoundBase>> LoadedSounds;
};
