#include "MDRagdollTuning.h"

#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

namespace MDRagdollTuningPrivate
{
	TAutoConsoleVariable<float> CVarPlayerLinear(TEXT("md.Combat.RagdollLinearDamping"), 0.75f,
		TEXT("Linear damping on a dead player's ragdoll bodies."), ECVF_Default);
	TAutoConsoleVariable<float> CVarPlayerAngular(TEXT("md.Combat.RagdollAngularDamping"), 6.0f,
		TEXT("Angular damping on a dead player's ragdoll bodies."), ECVF_Default);
	TAutoConsoleVariable<float> CVarPlayerStiffness(TEXT("md.Combat.RagdollJointStiffness"), 300.0f,
		TEXT("Weak angular drive stiffness (muscle tone) on a dead player's joints. 0 = fully limp."), ECVF_Default);
	TAutoConsoleVariable<float> CVarPlayerJointDamping(TEXT("md.Combat.RagdollJointDamping"), 75.0f,
		TEXT("Angular drive damping on a dead player's joints."), ECVF_Default);

	TAutoConsoleVariable<float> CVarEnemyLinear(TEXT("md.Combat.RagdollEnemyLinearDamping"), 0.05f,
		TEXT("Linear damping on a dead enemy's ragdoll bodies."), ECVF_Default);
	TAutoConsoleVariable<float> CVarEnemyAngular(TEXT("md.Combat.RagdollEnemyAngularDamping"), 0.4f,
		TEXT("Angular damping on a dead enemy's ragdoll bodies."), ECVF_Default);
	TAutoConsoleVariable<float> CVarEnemyStiffness(TEXT("md.Combat.RagdollEnemyJointStiffness"), 0.0f,
		TEXT("Weak angular drive stiffness (muscle tone) on a dead enemy's joints. 0 = fully limp."), ECVF_Default);
	TAutoConsoleVariable<float> CVarEnemyJointDamping(TEXT("md.Combat.RagdollEnemyJointDamping"), 0.0f,
		TEXT("Angular drive damping on a dead enemy's joints."), ECVF_Default);
}

FMDRagdollTuning MDRagdollTuning::Get(bool bPlayer)
{
	using namespace MDRagdollTuningPrivate;
	FMDRagdollTuning Out;
	Out.LinearDamping = FMath::Max(0.0f, (bPlayer ? CVarPlayerLinear : CVarEnemyLinear).GetValueOnGameThread());
	Out.AngularDamping = FMath::Max(0.0f, (bPlayer ? CVarPlayerAngular : CVarEnemyAngular).GetValueOnGameThread());
	Out.JointStiffness = FMath::Max(0.0f, (bPlayer ? CVarPlayerStiffness : CVarEnemyStiffness).GetValueOnGameThread());
	Out.JointDamping = FMath::Max(0.0f, (bPlayer ? CVarPlayerJointDamping : CVarEnemyJointDamping).GetValueOnGameThread());
	return Out;
}

void MDRagdollTuning::Apply(USkeletalMeshComponent* Mesh, const FMDRagdollTuning& Tuning)
{
	if (!Mesh)
	{
		return;
	}
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body)
		{
			Body->LinearDamping = Tuning.LinearDamping;
			Body->AngularDamping = Tuning.AngularDamping;
			Body->UpdateDampingProperties();
		}
	}
	const bool bDrive = Tuning.JointStiffness > 0.0f;
	for (FConstraintInstance* Constraint : Mesh->Constraints)
	{
		if (!Constraint)
		{
			continue;
		}
		if (bDrive)
		{
			Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
			Constraint->SetOrientationDriveSLERP(true);
			Constraint->SetAngularVelocityDriveSLERP(true);
			Constraint->SetAngularDriveParams(Tuning.JointStiffness, Tuning.JointDamping, 0.0f);
		}
		else
		{
			Constraint->SetOrientationDriveSLERP(false);
			Constraint->SetAngularVelocityDriveSLERP(false);
		}
	}
}

void MDRagdollTuning::ApplyForDeath(USkeletalMeshComponent* Mesh, bool bPlayer)
{
	Apply(Mesh, Get(bPlayer));
}
