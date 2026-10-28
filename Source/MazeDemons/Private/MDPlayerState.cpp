#include "MDPlayerState.h"

#include "MDAbilitySystemComponent.h"
#include "MDAttributeSet.h"
#include "MDBalanceData.h"
#include "MDGameSettings.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPhysicalHitReactComponent.h"
#include "MDPerkComponent.h"
#include "Ranged/MDRangedComponent.h"
#include "MDRagdollTuning.h"
#include "MDStaminaComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
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

	StaminaComponent = CreateDefaultSubobject<UMDStaminaComponent>(TEXT("StaminaComponent"));

	PerkComponent = CreateDefaultSubobject<UMDPerkComponent>(TEXT("PerkComponent"));

	RangedComponent = CreateDefaultSubobject<UMDRangedComponent>(TEXT("RangedComponent"));

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
		// Durability penalty for every equipped item (once per death: the ASC fires OnDeath a single time).
		if (UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(GetPawn()))
		{
			Inventory->ApplyDeathPenalty();
		}
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
		// Enemies and any pawn without a separate body mesh: the main mesh itself goes fully limp. Every body simulates at full
		// physics weight, the anim instance stops driving the pose, and the cosmetic hit reaction (whose physical animation drives
		// and partial blend weights would keep the upper body stiff) is shut down.
		if (UMDPhysicalHitReactComponent* HitReact = Character->FindComponentByClass<UMDPhysicalHitReactComponent>())
		{
			HitReact->StopForDeath();
		}
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->StopAllMontages(0.0f);
		}
		Mesh->bPauseAnims = true;
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetSimulatePhysics(true);
		Mesh->SetPhysicsBlendWeight(1.0f);
		Mesh->WakeAllRigidBodies();
		MDRagdollTuning::ApplyForDeath(Mesh, false);
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
	MDRagdollTuning::ApplyForDeath(Body, true);

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
	DOREPLIFETIME_CONDITION(AMDPlayerState, RunStats, COND_OwnerOnly);
}

void AMDPlayerState::OnRep_RunStats()
{
	OnRunStatsChanged.Broadcast();
}

void AMDPlayerState::AddDamageDealt(float Amount)
{
	if (HasAuthority())
	{
		const float New = FMDPlayerRunStats::AddAmount(RunStats.DamageDealt, Amount);
		if (New != RunStats.DamageDealt)
		{
			RunStats.DamageDealt = New;
			OnRunStatsChanged.Broadcast();
		}
	}
}

void AMDPlayerState::AddDamageTaken(float Amount)
{
	if (HasAuthority())
	{
		const float New = FMDPlayerRunStats::AddAmount(RunStats.DamageTaken, Amount);
		if (New != RunStats.DamageTaken)
		{
			RunStats.DamageTaken = New;
			OnRunStatsChanged.Broadcast();
		}
	}
}

void AMDPlayerState::AddKill()
{
	if (HasAuthority())
	{
		RunStats.Kills = FMDPlayerRunStats::AddCount(RunStats.Kills, 1);
		OnRunStatsChanged.Broadcast();
	}
}

void AMDPlayerState::AddGoldEarned(int32 Amount)
{
	if (HasAuthority() && Amount > 0)
	{
		RunStats.GoldEarned = FMDPlayerRunStats::AddCount(RunStats.GoldEarned, Amount);
		OnRunStatsChanged.Broadcast();
	}
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

void AMDPlayerState::GrantStarterKit()
{
	UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(this);
	if (!HasAuthority() || !Inventory)
	{
		return;
	}
	bool bEquippedGun = false;
	for (const FMDStarterItem& Item : UMDGameSettings::GetEconomy().StarterKit)
	{
		if (Item.ItemRow.IsNone() || Item.Quantity <= 0 || !Inventory->AddItem(Item.ItemRow, Item.Quantity))
		{
			continue;
		}
		if (Item.bEquip)
		{
			const TArray<FMDItemStack>& Bag = Inventory->GetItems();
			const int32 Index = Bag.IndexOfByPredicate([&Item](const FMDItemStack& Stack) { return Stack.ItemRowName == Item.ItemRow; });
			if (Index != INDEX_NONE)
			{
				Inventory->RequestEquip(Index);
				bEquippedGun = true;
			}
		}
	}
	// Equipping a gun with no melee weapon puts the player in ranged mode; the run starts in melee, the gun is one Q away.
	if (bEquippedGun && RangedComponent)
	{
		RangedComponent->SetRangedMode(false);
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
			AbilitySystem->DefenseCap = UMDGameSettings::GetEconomy().PlayerDRCap;
			// DA_BalanceReference is the source of truth; the Starting* properties stay as the fallback until it exists.
			if (const UMDBalanceData* Balance = UMDGameSettings::GetBalanceData())
			{
				AbilitySystem->InitializeStats(Balance->StartingMaxHealth, Balance->StartingAttackPower, Balance->StartingBaseDefense);
			}
			else
			{
				AbilitySystem->InitializeStats(StartingMaxHealth, StartingAttackPower, StartingBaseDefense);
			}
			if (StaminaComponent)
			{
				AbilitySystem->InitializeStamina(StaminaComponent->StartingMaxStamina, StaminaComponent->RegenPerSec);
			}
			if (PerkComponent)
			{
				PerkComponent->InitializeForRun(); // after the base stats, so the perk effect lands on top of them
			}
			GrantStarterKit(); // R3: pistol + 40 Light Rounds (FMDEconomyTuning::StarterKit)
		}
		else if (AbilitySystem->IsDead() && NewPawn != DeadPawn)
		{
			// Respawn: stats persist, but a new pawn must not start dead (or exhausted).
			AbilitySystem->ResetHealthToMax();
			AbilitySystem->ResetStaminaToMax();
			if (StaminaComponent)
			{
				StaminaComponent->ResetForRespawn();
			}
			if (PerkComponent)
			{
				PerkComponent->ResetLifeFlags(); // Second Wind is available again; ranks and points persist
			}
		}

		if (NewPawn != DeadPawn)
		{
			DeadPawn = nullptr; // a different pawn than the corpse: the death is over
		}

		// The new avatar re-receives the persisted equipment (visuals via BP hooks, armor reductions).
		if (UMDInventoryComponent* Inventory = UMDGameplayStatics::GetInventory(NewPawn))
		{
			Inventory->BindToASC();
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
