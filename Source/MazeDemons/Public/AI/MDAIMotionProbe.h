#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "MDAIMotionProbe.generated.h"

class AMDEnemyCharacter;
class FOutputDevice;

// md.AI.DebugMotion <cm>: a per-frame motion probe for enemy pops, snaps and stretched bodies, on any machine (listen host,
// client, standalone). Every frame it compares each live enemy's capsule, mesh and pelvis with the previous frame and logs
// (LogMDAIMotion) when the capsule jumped more than the threshold in one frame, the pelvis sits further than that from
// the capsule (a physics blend or root-bone drift pulling the body away), the mesh's relative offset changed (a kick or a
// stray root motion), or the capsule or mesh tilted. Each line carries the intent, the attack / montage state, the
// velocity, the net role and whether a physical hit reaction is running, so the culprit is in the log, not guessed.
// 0 = off (default). md.AI.DebugMotionSummary prints the counters so far.
UCLASS()
class MAZEDEMONS_API UMDAIMotionProbe : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UMDAIMotionProbe* Get(const UWorld* World);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	static float GetThreshold();

	struct FCounters
	{
		int32 CapsulePops = 0;
		int32 PelvisAway = 0;
		int32 MeshOffsets = 0;
		int32 Tilts = 0;
		int32 Frames = 0;
	};
	const FCounters& GetCounters() const { return Counters; }
	void PrintSummary(FOutputDevice& Ar) const;

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
	struct FSample
	{
		FVector Capsule = FVector::ZeroVector;
		FVector MeshRelative = FVector::ZeroVector;
		bool bValid = false;
		double LastLogTime = -1.0;
	};
	TMap<TWeakObjectPtr<const AMDEnemyCharacter>, FSample> Samples;
	FCounters Counters;

	FString Describe(const AMDEnemyCharacter& Enemy) const;
};
