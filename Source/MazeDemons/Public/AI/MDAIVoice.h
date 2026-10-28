#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MDAIVoice.generated.h"

class USoundBase;
class USoundConcurrency;
class UAudioComponent;

// What an enemy voice line is for. Each kind has its own gate (below).
UENUM()
enum class EMDVoiceKind : uint8
{
	Tell,  // the attack wind-up growl (UMDAN_Tell), and the short wind-up cues of a feint, a brace or a pre-blink
	Bark,  // pack barks: swarm howl, taunt, fall back, ambush growl, blink arrival, charge scrape, fake flinch
	Sting, // the off-screen threat sting (UMDThreatIndicator)
};

// The gate every enemy growl goes through, as plain rules so they run in MazeDemons.AI.Voice.* with no audio:
//   - at most MaxConcurrent lines of a kind play at once (a line counts as playing for MaxSeconds);
//   - one enemy repeats a kind no sooner than EnemyCooldown;
//   - the listener hears a kind no more often than ListenerInterval, whoever growls (a pack cannot stack them);
//   - a line is cut (faded) after MaxSeconds: a tell is a 0.5 - 0.8 s cue, not the whole Fab growl.
// Per listener: the subsystem lives in each machine's world, so a listen host and a client each gate their own audio.
struct MAZEDEMONS_API FMDVoiceRules
{
	int32 MaxConcurrent = 1;
	double EnemyCooldown = 1.0;
	double ListenerInterval = 0.25;
	float MaxSeconds = 0.7f;
	float VolumeScale = 1.0f;

	static FMDVoiceRules ForKind(EMDVoiceKind Kind);
};

struct MAZEDEMONS_API FMDVoiceGate
{
	// True when a line of Kind by EnemyId may start at Now; the gate then records it as playing. Reason (optional) says
	// why not. Deterministic: the same calls in the same order give the same answers.
	bool TryStart(EMDVoiceKind Kind, int32 EnemyId, double Now, FString* OutReason = nullptr);

	// How many lines of Kind are still playing at Now.
	int32 NumPlaying(EMDVoiceKind Kind, double Now) const;

	void Reset();

private:
	struct FPlaying
	{
		EMDVoiceKind Kind = EMDVoiceKind::Tell;
		int32 EnemyId = 0;
		double EndTime = 0.0;
	};
	TArray<FPlaying> Playing;
	TMap<uint64, double> LastByEnemy;   // (kind, enemy) -> last start
	double LastByKind[3] = { -1000.0, -1000.0, -1000.0 };

	void Purge(double Now);
};

// Plays enemy voice lines on this machine through FMDVoiceGate, with a USoundConcurrency per kind (MaxConcurrent, newest
// prevented) as the engine-side belt and braces, and cuts each line after the kind's MaxSeconds with a short fade.
UCLASS()
class MAZEDEMONS_API UMDAIVoiceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMDAIVoiceSubsystem* Get(const UWorld* World);

	// Plays Sound at Location for the enemy with EnemyId (any stable id: the enemy's unique id) if the gate allows it.
	// Volume is scaled by the kind's VolumeScale. Returns the component (null when gated or on a dedicated server).
	UAudioComponent* Play(EMDVoiceKind Kind, int32 EnemyId, USoundBase* Sound, const FVector& Location, float Volume);

	const FMDVoiceGate& GetGate() const { return Gate; }

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

private:
	USoundConcurrency* ConcurrencyFor(EMDVoiceKind Kind);
	void CutLine(TWeakObjectPtr<UAudioComponent> Component);

	FMDVoiceGate Gate;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundConcurrency>> Concurrency;

	TArray<FTimerHandle> CutTimers;
};
