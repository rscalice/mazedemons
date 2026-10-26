#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MDCombatTypes.h"
#include "MDPhysicalHitReactComponent.generated.h"

class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

// Cosmetic physical hit reaction (C1 WP5): a short upper-body impulse from the hit direction, blended in and out, driven by
// a physical-animation profile on the spine and its children. Never a ragdoll. Local to each machine (never replicated); created
// on demand by UMDPoiseComponent::PlayReaction. Meshes without a physics asset or without a body on the spine bone are skipped.
// Toggle: console variable md.Combat.PhysicalReactions (default 1).
UCLASS(ClassGroup = (Gameplay))
class MAZEDEMONS_API UMDPhysicalHitReactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDPhysicalHitReactComponent();

	// Starts (or retriggers) the reaction on Mesh. Returns false when nothing was played (cvar off, no physics bodies, ...).
	bool PlayHitReaction(USkeletalMeshComponent* Mesh, EMDHitDirection Direction, EMDHitReaction Reaction);

	// True while the blend is running.
	bool IsReacting() const { return bActive; }

	// The pawn died and its whole mesh is about to ragdoll: end the reaction without restoring anything, drop the physical animation
	// drives (they would keep pulling the bodies below the root toward the animation) and refuse further reactions.
	void StopForDeath();

	// Pure blend curve: 0 at the start, PeakWeight at BlendIn, back to 0 at BlendIn + BlendOut.
	static float EvaluateBlend(float Elapsed, float BlendIn, float BlendOut, float PeakWeight);

	// World-space impulse direction for a hit from Direction, relative to the victim's facing (pushes away from the attacker).
	static FVector ImpulseDirection(const FVector& Forward, const FVector& Right, EMDHitDirection Direction);

	// Root of the simulated chain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical Reaction")
	FName RootBone = TEXT("spine_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical Reaction", meta = (ClampMin = "0.01"))
	float BlendInTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical Reaction", meta = (ClampMin = "0.01"))
	float BlendOutTime = 0.25f;

	// Peak physics blend weight (0..1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical Reaction", meta = (ClampMin = "0", ClampMax = "1"))
	float PeakWeight = 0.5f;

	// Velocity change (cm/s) given to every body below the root. Stronger reactions multiply it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical Reaction", meta = (ClampMin = "0"))
	float ImpulseSpeed = 120.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool EnsureSetup(USkeletalMeshComponent* Mesh);
	void Finish();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	TWeakObjectPtr<USkeletalMeshComponent> ActiveMesh;
	TWeakObjectPtr<USkeletalMeshComponent> ConfiguredMesh;

	bool bActive = false;
	bool bDead = false;
	float Elapsed = 0.0f;

	// Collision state to restore when a query-only mesh was made simulatable for the reaction.
	bool bRestoreCollision = false;
	TEnumAsByte<ECollisionEnabled::Type> SavedCollisionEnabled = ECollisionEnabled::QueryOnly;
	FCollisionResponseContainer SavedResponses;
};
