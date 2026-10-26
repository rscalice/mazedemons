#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

// Death-ragdoll feel, tunable live through console variables (read when the pawn dies, so change them and kill something again):
//   md.Combat.RagdollLinearDamping / AngularDamping / JointStiffness / JointDamping           players   (defaults 0.5 / 4 / 200 / 50)
//   md.Combat.RagdollEnemyLinearDamping / AngularDamping / JointStiffness / JointDamping      enemies   (defaults 0.05 / 0.4 / 0 / 0, floppy)
// Damping is applied to every simulated body. The joint drive is a weak SLERP orientation + velocity drive on every constraint that
// pulls each joint back toward its rest pose (muscle tone), so the body collapses and settles instead of folding like jelly.
// A JointStiffness of 0 turns the drive off.
struct FMDRagdollTuning
{
	float LinearDamping = 0.0f;
	float AngularDamping = 0.0f;
	float JointStiffness = 0.0f;
	float JointDamping = 0.0f;
};

namespace MDRagdollTuning
{
	// Current console variable values for the player or the enemy ragdoll.
	MAZEDEMONS_API FMDRagdollTuning Get(bool bPlayer);

	// Applies Tuning to every body and constraint of Mesh (which must already simulate physics).
	MAZEDEMONS_API void Apply(USkeletalMeshComponent* Mesh, const FMDRagdollTuning& Tuning);

	// Get(bPlayer) + Apply, the call made when a pawn dies.
	MAZEDEMONS_API void ApplyForDeath(USkeletalMeshComponent* Mesh, bool bPlayer);
}
