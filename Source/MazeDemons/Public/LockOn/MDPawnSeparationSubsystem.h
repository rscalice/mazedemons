#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MDPawnSeparationSubsystem.generated.h"

class ACharacter;

// V1 WP7 pawn collision (Robert 2026-10-26: players overlap each other and enemies too much).
//
// Every machine: makes sure living characters' capsules BLOCK the Pawn channel (player <-> player, player <-> enemy, enemy <-> enemy), so
// walking, root motion, warps and knockback all sweep against each other instead of through. Done here so no Blueprint capsule needs
// editing; a capsule that already blocks is left alone.
// Server / standalone: a light separation push on top, so bodies that still end up overlapping (a warp that began close, a slide, a
// knockback into someone) ease apart instead of staying interpenetrated. Enemies move out of a player's way; the player itself is not
// pushed by default (its movement is client predicted, a server push would rubber-band), except against another player (half each).
// Console: md.Pawn.Block, md.Pawn.Separation, md.Pawn.SeparationRate, md.Pawn.Debug.
UCLASS()
class MAZEDEMONS_API UMDPawnSeparationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;

	// How much of an overlap each kind of pawn corrects (0..1; the pair's shares add up to 1). Pure, tested.
	static float PushShare(bool bSelfIsPlayer, bool bOtherIsPlayer);

	// Characters moved by the separation push in the last tick (tests / debugging).
	int32 GetLastPushCount() const { return LastPushCount; }

private:
	void EnforceBlocking();
	void Separate(float DeltaTime);

	float EnforceTimer = 0.0f;
	int32 LastPushCount = 0;
};
