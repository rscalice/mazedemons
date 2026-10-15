#include "MDPlayerState.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Net/UnrealNetwork.h"

static USkeletalMeshComponent* FindPawnMeshComponent(const APawn* Pawn, const TCHAR* Name)
{
	if (!Pawn)
	{
		return nullptr;
	}
	TArray<USkeletalMeshComponent*> Comps;
	Pawn->GetComponents<USkeletalMeshComponent>(Comps);
	for (USkeletalMeshComponent* C : Comps)
	{
		if (C && C->GetFName() == FName(Name))
		{
			return C;
		}
	}
	return nullptr;
}

AMDPlayerState::AMDPlayerState()
{
	AbilitySystem = CreateDefaultSubobject<UMDAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	Attributes = CreateDefaultSubobject<UMDAttributeSet>(TEXT("MazeDemonsAttributes"));

	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* AMDPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

UMDAbilitySystemComponent* AMDPlayerState::GetMDAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AMDPlayerState::HandleDeath()
{
	if (HasAuthority())
	{
		DeadPawn = GetPawn();
	}
	RagdollPawn(GetPawn());
}

void AMDPlayerState::OnRep_DeadPawn()
{
	// The pointer can arrive after the pawn itself (late join): ragdoll once both are known and match.
	if (DeadPawn && DeadPawn == GetPawn())
	{
		RagdollPawn(DeadPawn);
	}
}

void AMDPlayerState::RagdollPawn(APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return;
	}
	// The visible body is the "Manny" component; CharacterMesh0 is the hidden source. Fall back to
	// CharacterMesh0 if the pawn has no such component.
	USkeletalMeshComponent* Body = FindPawnMeshComponent(Pawn, TEXT("Manny"));
	USkeletalMeshComponent* Simulated = Body ? Body : Mesh;
	if (Simulated->IsSimulatingPhysics())
	{
		return;
	}

	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (!Body)
	{
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetSimulatePhysics(true);
		return;
	}

	// Stop the hidden source mesh from fighting the ragdoll.
	Mesh->bPauseAnims = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Body->SetLeaderPoseComponent(nullptr);
	Body->SetAnimInstanceClass(nullptr);
	Body->bPauseAnims = true;
	// Use the body mesh's own physics asset (MetaHuman body; PA_Mannequin bones would not match).
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsBlendWeight(1.0f);
	Body->WakeAllRigidBodies();

	// Armor/face parts follow the simulated body; they are not simulated themselves.
	static const TCHAR* FollowParts[] = { TEXT("Torso"), TEXT("Hands"), TEXT("Legs"), TEXT("Feet"), TEXT("Face") };
	for (const TCHAR* PartName : FollowParts)
	{
		if (USkeletalMeshComponent* Part = FindPawnMeshComponent(Pawn, PartName))
		{
			if (Part != Body)
			{
				Part->SetLeaderPoseComponent(Body, true);
			}
		}
	}
}

void AMDPlayerState::SyncArmorFromPawn()
{
	if (!HasAuthority())
	{
		return;
	}
	APawn* Pawn = GetPawn();
	auto Read = [Pawn](const TCHAR* Name) -> USkeletalMesh*
	{
		USkeletalMeshComponent* C = FindPawnMeshComponent(Pawn, Name);
		return C ? C->GetSkeletalMeshAsset() : nullptr;
	};
	TorsoMesh = Read(TEXT("Torso"));
	HandsMesh = Read(TEXT("Hands"));
	LegsMesh = Read(TEXT("Legs"));
	FeetMesh = Read(TEXT("Feet"));
	ApplyArmorVisuals();
}

void AMDPlayerState::OnRep_Armor()
{
	ApplyArmorVisuals();
}

void AMDPlayerState::ApplyArmorVisuals()
{
	APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}
	auto Apply = [Pawn](const TCHAR* Name, USkeletalMesh* Mesh)
	{
		USkeletalMeshComponent* C = FindPawnMeshComponent(Pawn, Name);
		if (C && C->GetSkeletalMeshAsset() != Mesh)
		{
			C->SetSkeletalMeshAsset(Mesh);
		}
	};
	Apply(TEXT("Torso"), TorsoMesh);
	Apply(TEXT("Hands"), HandsMesh);
	Apply(TEXT("Legs"), LegsMesh);
	Apply(TEXT("Feet"), FeetMesh);
}

void AMDPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMDPlayerState, TorsoMesh);
	DOREPLIFETIME(AMDPlayerState, HandsMesh);
	DOREPLIFETIME(AMDPlayerState, LegsMesh);
	DOREPLIFETIME(AMDPlayerState, FeetMesh);
	DOREPLIFETIME(AMDPlayerState, DeadPawn);
}

void AMDPlayerState::BeginPlay()
{
	Super::BeginPlay();

	OnPawnSet.AddDynamic(this, &AMDPlayerState::HandlePawnSet);
	if (APawn* ExistingPawn = GetPawn())
	{
		HandlePawnSet(this, ExistingPawn, nullptr);
	}
}

void AMDPlayerState::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (!NewPawn || !AbilitySystem)
	{
		return;
	}

	// Clients only: on the server the BP equip logic owns the meshes (SyncArmorFromPawn publishes them).
	if (!HasAuthority())
	{
		ApplyArmorVisuals();
	}

	AbilitySystem->InitAbilityActorInfo(this, NewPawn);
	AbilitySystem->RegisterAttributeSet(Attributes);

	// Single OnDeath binding for the PlayerState's lifetime; the handler always targets the current pawn,
	// so respawns (OldPawn -> NewPawn) cannot stack bindings.
	if (!bDeathBound)
	{
		bDeathBound = true;
		AbilitySystem->OnDeath.AddDynamic(this, &AMDPlayerState::HandleDeath);
	}

	if (HasAuthority())
	{
		if (!bStatsInitialized)
		{
			bStatsInitialized = true;
			AbilitySystem->InitializeStats(StartingMaxHealth, StartingAttackPower, StartingBaseDefense);
		}
		else if (AbilitySystem->IsDead() && NewPawn != DeadPawn)
		{
			// Respawn: stats persist, but a new pawn must not start dead.
			AbilitySystem->ResetHealthToMax();
		}

		if (NewPawn != DeadPawn)
		{
			DeadPawn = nullptr; // a different pawn than the corpse: the death is over
		}

		// The new avatar re-receives the persisted equipment (visuals via BP hooks, armor reductions).
		if (UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(NewPawn))
		{
			Inventory->ReapplyAllEquipment();
		}
	}

	// Pawn arrived after its death was already replicated (late join / pawn set later). Identity, not the death
	// flag, decides: a respawn pawn is never ragdolled even while the replicated flag still reads dead.
	if (NewPawn == DeadPawn)
	{
		RagdollPawn(NewPawn);
	}
}
