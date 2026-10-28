#include "MDPoiseComponent.h"

#include "AbilitySystemComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatDebug.h"
#include "MDComboComponent.h"
#include "MDCombatFeedbackComponent.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDPhysicalHitReactComponent.h"
#include "MDPoiseProfile.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

UMDPoiseComponent::UMDPoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.05f;
	SetIsReplicatedByDefault(true);
}

void UMDPoiseComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDPoiseComponent, LastHit);
	DOREPLIFETIME(UMDPoiseComponent, bStunned);
	DOREPLIFETIME(UMDPoiseComponent, PoisePercent);
}

void UMDPoiseComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server simulates poise.
	SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority());
	if (Poise < 0.0f)
	{
		Poise = GetMaxPoise();
	}
}

void UMDPoiseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The stun tag lives on an ASC that can outlive this pawn (PlayerState): never leave it behind.
	ApplyStunTag(false);
	Super::EndPlay(EndPlayReason);
}

UMDPoiseProfile* UMDPoiseComponent::GetProfile() const
{
	return Profile ? Profile.Get() : GetMutableDefault<UMDPoiseProfile>();
}

float UMDPoiseComponent::GetMaxPoise() const
{
	return GetProfile()->MaxPoise;
}

float UMDPoiseComponent::GetPoiseFraction() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const float Max = GetMaxPoise();
		return Max > 0.0f ? FMath::Clamp((Poise < 0.0f ? Max : Poise) / Max, 0.0f, 1.0f) : 0.0f;
	}
	return static_cast<float>(PoisePercent) / 100.0f;
}

bool UMDPoiseComponent::IsHyperArmored() const
{
	return ServerNow() < HyperArmorUntil;
}

double UMDPoiseComponent::ServerNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

UMDPoiseComponent* UMDPoiseComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMDPoiseComponent>() : nullptr;
}

bool UMDPoiseComponent::IsActorStunned(const AActor* Actor)
{
	const UMDPoiseComponent* Poise = FindFor(Actor);
	return Poise && Poise->IsStunned();
}

USkeletalMeshComponent* UMDPoiseComponent::GetReactionMesh() const
{
	if (ReactionMesh.IsValid())
	{
		return ReactionMesh.Get();
	}
	AActor* Owner = GetOwner();
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		return Character->GetMesh();
	}
	return Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FMDPoiseHitResult UMDPoiseComponent::ApplyHit(const FMDHitContext& Context, const FMDDamageResult& Damage)
{
	FMDPoiseHitResult Result;
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return Result;
	}

	const UMDPoiseProfile& Config = *GetProfile();
	if (Poise < 0.0f)
	{
		Poise = Config.MaxPoise;
	}
	const double Now = ServerNow();

	const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Owner);

	FMDReactionInput In;
	In.bDead = ASC && ASC->IsDead();
	In.bHyperArmor = Now < HyperArmorUntil;
	In.bImmune = Now < ImmuneUntil;
	In.bStunned = bStunned;
	In.bFlinchOnly = Config.bFlinchOnly;
	In.StepReaction = Context.Reaction;
	In.MaxReaction = Context.MaxReaction;
	In.Poise = Poise;
	In.MaxPoise = Config.MaxPoise;
	In.PoiseDamage = UMDCombatRules::CritPoiseBonus(Context.PoiseDamage, Damage.bCrit);

	FMDReactionResult Resolved = UMDCombatRules::ResolveReaction(In);

	const int32 Forced = MDCombatDebug::ForceReaction();
	if (Forced > 0 && !In.bDead)
	{
		Resolved.Reaction = static_cast<EMDHitReaction>(FMath::Clamp(Forced, 1, static_cast<int32>(EMDHitReaction::Knockdown)));
		Resolved.bStartStun = Forced >= static_cast<int32>(EMDHitReaction::Stagger);
	}

	Poise = Resolved.PoiseAfter;
	LastHitTime = Now;

	Result.bValid = true;
	Result.Reaction = Resolved.Reaction;
	Result.Direction = UMDCombatRules::HitDirection(Owner->GetActorForwardVector(), Owner->GetActorLocation(), Context.AttackerLocation);
	Result.bStunStarted = Resolved.bStartStun;

	if (Resolved.bStartStun)
	{
		StartStun(Config.StunDuration, Config);
	}
	if (static_cast<int32>(Resolved.Reaction) >= static_cast<int32>(EMDHitReaction::Stagger))
	{
		// A stagger cancels whatever swing the victim was in and resets their combo.
		if (UMDComboComponent* Combo = UMDComboComponent::FindFor(Owner))
		{
			Combo->NotifyInterrupted();
		}
	}

	if (Resolved.Reaction != EMDHitReaction::None)
	{
		FMDHitResultRep Rep;
		Rep.Reaction = Resolved.Reaction;
		Rep.Direction = Result.Direction;
		Rep.ImpactPoint = Context.ImpactPoint;
		Rep.ImpactTag = Context.ImpactTag;
		Rep.Surface = Context.PhysMaterial ? Context.PhysMaterial->SurfaceType.GetValue() : static_cast<EPhysicalSurface>(SurfaceType_Default);
		Rep.bCrit = Damage.bCrit;
	Rep.DamageType = Damage.DamageType;
		Rep.Damage = Damage.Damage;
		Rep.Instigator = Context.Instigator;
		NextSeq = (NextSeq == 255) ? 1 : static_cast<uint8>(NextSeq + 1); // 0 means "no hit yet"
		Rep.Seq = NextSeq;
		Rep.ServerTime = Now;
		LastHit = Rep;
		HandleLastHit(); // the server plays it too; clients get OnRep_LastHit
	}

	PoisePercent = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(GetPoiseFraction() * 100.0f), 0, 100));
	return Result;
}

void UMDPoiseComponent::StartStun(float Seconds, const UMDPoiseProfile& Config)
{
	const double Now = ServerNow();
	StunEndTime = Now + FMath::Max(Seconds, 0.05f);
	ImmuneUntil = StunEndTime + FMath::Max(Config.StaggerImmunityAfterStun, 0.0f);
	SetStunned(true);
}

void UMDPoiseComponent::Stun(float Seconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UMDPoiseProfile& Config = *GetProfile();
	StartStun(Seconds > 0.0f ? Seconds : Config.StunDuration, Config);
	if (UMDComboComponent* Combo = UMDComboComponent::FindFor(GetOwner()))
	{
		Combo->NotifyInterrupted();
	}
}

void UMDPoiseComponent::ClearStun()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		StunEndTime = 0.0;
		SetStunned(false);
	}
}

void UMDPoiseComponent::ResetState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Poise = GetMaxPoise();
	PoisePercent = 100;
	StunEndTime = 0.0;
	ImmuneUntil = 0.0;
	HyperArmorUntil = 0.0;
	SetStunned(false);
}

void UMDPoiseComponent::SetHyperArmor(float Seconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	HyperArmorUntil = Seconds > 0.0f ? ServerNow() + Seconds : 0.0;
}

void UMDPoiseComponent::SetPoise(float NewPoise)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Poise = FMath::Clamp(NewPoise, 0.0f, GetMaxPoise());
		LastHitTime = ServerNow();
		PoisePercent = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(GetPoiseFraction() * 100.0f), 0, 100));
	}
}

void UMDPoiseComponent::ApplyKnockback(FVector Velocity, bool bPlayerSlide)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !GetOwner()->HasAuthority() || Velocity.IsNearlyZero())
	{
		return;
	}
	if (bPlayerSlide)
	{
		Velocity.Z = 0.0f; // a slide on their feet, never a launch
		Character->LaunchCharacter(Velocity, true, false);
		OnKnockbackSlide(Velocity);
	}
	else
	{
		Character->LaunchCharacter(Velocity, true, true);
	}
}

void UMDPoiseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const double Now = ServerNow();
	if (bStunned && Now >= StunEndTime)
	{
		SetStunned(false);
	}
	if (HyperArmorUntil > 0.0 && Now >= HyperArmorUntil)
	{
		HyperArmorUntil = 0.0;
	}

	const UMDPoiseProfile& Config = *GetProfile();
	if (Poise < 0.0f)
	{
		Poise = Config.MaxPoise;
	}
	if (Poise < Config.MaxPoise)
	{
		FMDPoiseTickInput In;
		In.Poise = Poise;
		In.MaxPoise = Config.MaxPoise;
		In.RegenDelay = Config.RegenDelay;
		In.RegenPerSec = Config.RegenPerSec;
		In.TimeSinceHit = static_cast<float>(FMath::Min(Now - LastHitTime, 1000.0));
		In.DeltaTime = DeltaTime;
		Poise = UMDCombatRules::TickPoise(In);
	}
	const uint8 Percent = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(GetPoiseFraction() * 100.0f), 0, 100));
	if (Percent != PoisePercent)
	{
		PoisePercent = Percent;
	}
}

void UMDPoiseComponent::SetStunned(bool bNewStunned)
{
	if (bStunned == bNewStunned)
	{
		return;
	}
	bStunned = bNewStunned;
	ApplyStunTag(bStunned);
	OnStunChanged.Broadcast(bStunned);
}

void UMDPoiseComponent::ApplyStunTag(bool bOn)
{
	if (bOn)
	{
		UAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner());
		if (ASC && !TaggedASC.IsValid())
		{
			ASC->AddLooseGameplayTag(TAG_State_Staggered.GetTag());
			TaggedASC = ASC;
		}
	}
	else if (UAbilitySystemComponent* ASC = TaggedASC.Get())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Staggered.GetTag());
		TaggedASC.Reset();
	}
}

void UMDPoiseComponent::OnRep_Stunned()
{
	ApplyStunTag(bStunned);
	if (bStunned)
	{
		// The owner's combo state machine resets on the stagger (the server told us via bStunned, which is correct on join).
		if (UMDComboComponent* Combo = UMDComboComponent::FindFor(GetOwner()))
		{
			Combo->NotifyInterrupted();
		}
	}
	OnStunChanged.Broadcast(bStunned);
}

void UMDPoiseComponent::OnRep_LastHit()
{
	HandleLastHit();
}

void UMDPoiseComponent::HandleLastHit()
{
	if (LastHit.Seq == 0 || LastHit.Reaction == EMDHitReaction::None)
	{
		return;
	}
	// Late join / relevancy: ignore a record older than a second.
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			if (GameState->GetServerWorldTimeSeconds() - LastHit.ServerTime > 1.0)
			{
				return;
			}
		}
	}
	PlayReaction(LastHit);
}

void UMDPoiseComponent::PlayReaction(const FMDHitResultRep& Hit)
{
	const UMDPoiseProfile& Config = *GetProfile();
	USkeletalMeshComponent* Mesh = GetReactionMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim)
	{
		if (UAnimMontage* Montage = Config.FindMontage(Hit.Reaction, Hit.Direction))
		{
			const bool bFullBody = static_cast<int32>(Hit.Reaction) >= static_cast<int32>(EMDHitReaction::Stagger);
			if (bFullBody)
			{
				Anim->StopAllMontages(0.1f); // full-body reaction interrupts the swing
			}
			// A flinch is additive and must not stop the swing that is playing: do not stop other montages.
			Anim->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, bFullBody);
		}
	}

	// Cosmetic physical flinch, layered under every reaction (flinch and stronger). Local on each machine; the component is
	// created on first use so no Blueprint needs editing. Skips meshes whose physics asset has no spine body.
	if (AActor* Owner = GetOwner())
	{
		if (Mesh)
		{
			UMDPhysicalHitReactComponent* Physical = Owner->FindComponentByClass<UMDPhysicalHitReactComponent>();
			if (!Physical && Mesh->GetPhysicsAsset())
			{
				Physical = NewObject<UMDPhysicalHitReactComponent>(Owner, TEXT("MDPhysicalHitReact"));
				Physical->RegisterComponent();
			}
			if (Physical)
			{
				Physical->PlayHitReaction(Mesh, Hit.Direction, Hit.Reaction);
			}
		}
	}

	OnReaction.Broadcast(Hit);

	if (UMDCombatFeedbackComponent* Feedback = UMDCombatFeedbackComponent::FindLocal(GetWorld()))
	{
		Feedback->NotifyReactionObserved(GetOwner(), Hit);
	}
}
