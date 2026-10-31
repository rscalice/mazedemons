#include "AI/MDEnemyCharacter.h"

#include "AI/MDAIVoice.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyAttributeSet.h"
#include "AI/MDEnemyAttackComponent.h"
#include "AI/MDBehaviourRules.h"
#include "AI/MDEnemyBehaviourComponent.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyController.h"
#include "AI/MDTargetableComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDBlinkComponent.h"
#include "MDCombatFeedbackComponent.h"
#include "MDGameplayStatics.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"
#include "MDStatusEffectComponent.h"
#include "MDUISettings.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MotionWarpingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace MDEnemyCharacterLocal
{
	// Every live enemy on this machine, for the threat indicator (cheaper than iterating all actors ten times a second).
	TArray<TWeakObjectPtr<AMDEnemyCharacter>> AllEnemies;

	// The overlay the combat feedback component uses for hit flashes; it has a scalar parameter "HitFlash".
	const TCHAR* const FlashOverlayPath = TEXT("/Game/Materials/Combat/M_HitFlashOverlay.M_HitFlashOverlay");
	const FName FlashParameter(TEXT("HitFlash"));
	constexpr float TellFlashIntensity = 0.7f;
	constexpr float TellFlashSeconds = 0.22f;
}

AMDEnemyCharacter::AMDEnemyCharacter()
{
	bReplicates = true;
	AIControllerClass = AMDEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->bUseControllerDesiredRotation = false;
		Move->RotationRate = FRotator(0.0f, 640.0f, 0.0f); // used by both facing modes (FaceMovement / FaceTarget)
	}

	ApplyCollisionResponses();

	AbilitySystem = CreateDefaultSubobject<UMDAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	Attributes = CreateDefaultSubobject<UMDEnemyAttributeSet>(TEXT("EnemyAttributes"));

	Poise = CreateDefaultSubobject<UMDPoiseComponent>(TEXT("Poise"));
	Blink = CreateDefaultSubobject<UMDBlinkComponent>(TEXT("Blink"));
	StatusEffects = CreateDefaultSubobject<UMDStatusEffectComponent>(TEXT("StatusEffects"));
	Feedback = CreateDefaultSubobject<UMDCombatFeedbackComponent>(TEXT("Feedback"));
	Targetable = CreateDefaultSubobject<UMDTargetableComponent>(TEXT("Targetable"));
	AttackComponent = CreateDefaultSubobject<UMDEnemyAttackComponent>(TEXT("EnemyAttack"));
	Behaviour = CreateDefaultSubobject<UMDEnemyBehaviourComponent>(TEXT("EnemyBehaviour"));
	if (Blink)
	{
		Blink->Cooldown = static_cast<float>(MDBehaviour::BlinkCooldown); // max 1 blink per enemy per 6 s (v1-design 1.2)
	}
	MotionWarping = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));
}

void AMDEnemyCharacter::ApplyCollisionResponses()
{
	// The weapon sweep (UMDCombatStatics::SweepWeaponHand) traces the Visibility channel, which the Pawn profile ignores.
	// The player's camera probes the Camera channel: an enemy that blocks it pushes the camera into the player's shoulder.
	for (UPrimitiveComponent* Component : TArray<UPrimitiveComponent*>{ GetCapsuleComponent(), GetMesh() })
	{
		if (Component)
		{
			Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Component->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
	}
}

void AMDEnemyCharacter::SetReplicatedIntent(EMDIntent Intent)
{
	if (HasAuthority())
	{
		ReplicatedIntent = static_cast<uint8>(Intent);
		if (Intent == EMDIntent::Dead)
		{
			SetThreat(nullptr);
		}
	}
}

void AMDEnemyCharacter::SetThreat(AActor* Target)
{
	if (!HasAuthority())
	{
		return;
	}
	const bool bNewThreatening = Target != nullptr;
	if (bThreatening != bNewThreatening)
	{
		bThreatening = bNewThreatening;
	}
	if (ThreatTarget != Target)
	{
		ThreatTarget = Target;
	}
}

const TArray<TWeakObjectPtr<AMDEnemyCharacter>>& AMDEnemyCharacter::GetAllEnemies()
{
	return MDEnemyCharacterLocal::AllEnemies;
}

void AMDEnemyCharacter::PlayTellCue(bool bWithSound)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || bDeathHandled)
	{
		return;
	}
	if (bWithSound && Archetype && !Archetype->TellSound.IsNull())
	{
		// Through the voice gate: a tell is cut to 0.7 s, at most two play at once, one per enemy per 1.2 s, and the pack
		// cannot stack them on the listener (UMDAIVoiceSubsystem).
		if (USoundBase* Sound = Archetype->TellSound.LoadSynchronous())
		{
			if (UMDAIVoiceSubsystem* Voice = UMDAIVoiceSubsystem::Get(World))
			{
				Voice->Play(EMDVoiceKind::Tell, static_cast<int32>(GetUniqueID()), Sound, GetActorLocation(), Archetype->TellVolume);
			}
		}
	}

	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}
	if (!TellFlashMID)
	{
		if (UMaterialInterface* Overlay = LoadObject<UMaterialInterface>(nullptr, MDEnemyCharacterLocal::FlashOverlayPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			TellFlashMID = UMaterialInstanceDynamic::Create(Overlay, this);
		}
	}
	if (!TellFlashMID)
	{
		return;
	}
	const float Intensity = MDEnemyCharacterLocal::TellFlashIntensity * (GetDefault<UMDLocalSettings>()->VFXLevel == EMDVFXLevel::Reduced ? 0.5f : 1.0f);
	TellFlashMID->SetScalarParameterValue(MDEnemyCharacterLocal::FlashParameter, Intensity);
	if (Body->GetOverlayMaterial() != TellFlashMID)
	{
		TellFlashPreviousOverlay = Body->GetOverlayMaterial();
		Body->SetOverlayMaterial(TellFlashMID);
	}
	World->GetTimerManager().SetTimer(TellFlashTimer, this, &AMDEnemyCharacter::EndTellFlash, MDEnemyCharacterLocal::TellFlashSeconds, false);
}

void AMDEnemyCharacter::EndTellFlash()
{
	if (TellFlashMID)
	{
		// Zero first: if a hit flash saved this overlay as "previous" meanwhile and restores it later, it then shows nothing.
		TellFlashMID->SetScalarParameterValue(MDEnemyCharacterLocal::FlashParameter, 0.0f);
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		if (Body->GetOverlayMaterial() == TellFlashMID)
		{
			Body->SetOverlayMaterial(TellFlashPreviousOverlay);
		}
	}
	TellFlashPreviousOverlay = nullptr;
}

EMDIntent AMDEnemyCharacter::GetIntent() const
{
	return static_cast<EMDIntent>(ReplicatedIntent);
}

void AMDEnemyCharacter::FaceMovement()
{
	bFacingTarget = false;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->bUseControllerDesiredRotation = false;
	}
}

void AMDEnemyCharacter::FaceTarget(AActor* Target)
{
	if (!Target)
	{
		FaceMovement();
		return;
	}
	bFacingTarget = true;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->SetFocus(Target, EAIFocusPriority::Gameplay); // AAIController::UpdateControlRotation aims the control rotation at it every tick
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = false;
		Move->bUseControllerDesiredRotation = true;
	}
}

void AMDEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMDEnemyCharacter, Archetype, COND_InitialOnly);
	DOREPLIFETIME(AMDEnemyCharacter, ReplicatedIntent);
	DOREPLIFETIME(AMDEnemyCharacter, bThreatening);
	DOREPLIFETIME(AMDEnemyCharacter, ThreatTarget);
	DOREPLIFETIME(AMDEnemyCharacter, bAmbushHidden);
}

void AMDEnemyCharacter::SetAmbushHidden(bool bNewHidden)
{
	if (!HasAuthority() || bAmbushHidden == bNewHidden)
	{
		return;
	}
	bAmbushHidden = bNewHidden;
	ApplyAmbushHidden();
}

void AMDEnemyCharacter::OnRep_AmbushHidden()
{
	ApplyAmbushHidden();
}

void AMDEnemyCharacter::ApplyAmbushHidden()
{
	SetActorHiddenInGame(bAmbushHidden);
	SetActorEnableCollision(!bAmbushHidden);
	if (Targetable)
	{
		Targetable->bTargetable = !bAmbushHidden && !bDeathHandled;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// No collision means no floor: freeze while hidden so the capsule does not fall through the level.
		if (bAmbushHidden)
		{
			Move->DisableMovement();
		}
		else
		{
			Move->SetMovementMode(MOVE_Walking);
		}
	}
}

UAbilitySystemComponent* AMDEnemyCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

FGenericTeamId AMDEnemyCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(MDTeams::Demons);
}

void AMDEnemyCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	// Read by the ASC's kill report (U4): the quest matches kills by this tag.
	if (Archetype)
	{
		TagContainer.AppendTags(Archetype->GetVictimTags());
	}
}

void AMDEnemyCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	ApplyCollisionResponses();

	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
		AbilitySystem->RegisterAttributeSet(Attributes);
		AbilitySystem->OnDeath.AddUniqueDynamic(this, &AMDEnemyCharacter::HandleDeath);
	}
	if (Poise)
	{
		Poise->SetReactionMesh(GetMesh());
		Poise->OnReaction.AddUniqueDynamic(this, &AMDEnemyCharacter::HandleReaction);
	}
}

void AMDEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	MDEnemyCharacterLocal::AllEnemies.RemoveSingleSwap(this);
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TellFlashTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void AMDEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	MDEnemyCharacterLocal::AllEnemies.AddUnique(this);
	if (Archetype)
	{
		ApplyArchetype(Archetype);
	}
}

void AMDEnemyCharacter::OnRep_Archetype()
{
	if (Archetype)
	{
		ApplyArchetype(Archetype);
	}
}

void AMDEnemyCharacter::ApplyArchetype(UMDEnemyArchetype* InArchetype)
{
	Archetype = InArchetype;
	if (!Archetype)
	{
		return;
	}

	if (Poise)
	{
		Poise->Profile = Archetype->ResolvePoiseProfile(this);
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = Archetype->MoveSpeed;
		if (Archetype->MaxAcceleration > 0.0f) { Move->MaxAcceleration = Archetype->MaxAcceleration; }
		if (Archetype->BrakingDeceleration > 0.0f) { Move->BrakingDecelerationWalking = Archetype->BrakingDeceleration; }
		if (Archetype->RotationYawRate > 0.0f) { Move->RotationRate.Yaw = Archetype->RotationYawRate; }
	}

	if (HasAuthority())
	{
		if (AbilitySystem)
		{
			AbilitySystem->InitializeStats(Archetype->MaxHealth, Archetype->AttackPower, Archetype->BaseDefense);
		}
		if (Attributes)
		{
			Attributes->SetMaxPoise(Archetype->MaxPoise);
			Attributes->SetPoise(Archetype->MaxPoise);
		}
		if (Poise)
		{
			Poise->ResetState();
		}
	}
}

float AMDEnemyCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AbilitySystem && Damage > 0.0f)
	{
		AActor* DamageInstigator = nullptr;
		if (EventInstigator)
		{
			DamageInstigator = EventInstigator->GetPawn() ? static_cast<AActor*>(EventInstigator->GetPawn()) : static_cast<AActor*>(EventInstigator);
		}
		else if (DamageCauser)
		{
			DamageInstigator = DamageCauser->GetInstigator() ? static_cast<AActor*>(DamageCauser->GetInstigator()) : DamageCauser;
		}
		AbilitySystem->ApplyDamage(Damage, DamageInstigator, DamageCauser);
	}
	return Damage;
}

int32 AMDEnemyCharacter::GetKillGoldAward() const
{
	return Archetype ? Archetype->GetKillGold() : 0;
}

const AController* AMDEnemyCharacter::GetKillerController(const AActor* LastDamageInstigator)
{
	if (const APawn* KillerPawn = Cast<APawn>(LastDamageInstigator))
	{
		return KillerPawn->GetController();
	}
	return Cast<AController>(LastDamageInstigator);
}

void AMDEnemyCharacter::AwardKillRewards()
{
	if (!HasAuthority() || !AbilitySystem)
	{
		return;
	}
	// The U2 split: even among players within the kill-gold radius, remainder to the killer. Not re-implemented here.
	UMDGameplayStatics::AwardKillGold(this, GetKillerController(AbilitySystem->GetLastDamageInstigator()), GetKillGoldAward());
}

void AMDEnemyCharacter::HandleReaction(const FMDHitResultRep& Hit)
{
	// Keep the replicated poise mirror in step with the component (the component stays the source of truth).
	if (HasAuthority() && Attributes && Poise && !bDeathHandled)
	{
		Attributes->SetPoise(Poise->GetPoiseFraction() * Poise->GetMaxPoise());
	}
}

void AMDEnemyCharacter::HandleDeath()
{
	if (bDeathHandled)
	{
		return;
	}
	bDeathHandled = true;

	if (Targetable)
	{
		Targetable->bTargetable = false;
	}

	if (HasAuthority())
	{
		// The brain goes Dead before the controller detaches (its own OnDeath binding may run after this one).
		SetReplicatedIntent(EMDIntent::Dead);
		if (UMDEnemyBrainComponent* Brain = UMDEnemyBrainComponent::FindFor(this))
		{
			Brain->NotifyDeath();
		}
		AwardKillRewards();
		// Quest credit needs nothing here: the ASC's kill report already ran (it reads GetOwnedGameplayTags).
		DetachFromControllerPendingDestroy();
		if (CorpseLifetime > 0.0f)
		{
			SetLifeSpan(CorpseLifetime);
		}
	}

	// Same local ragdoll the players use, on every machine.
	AMDPlayerState::RagdollPawn(this);
}
