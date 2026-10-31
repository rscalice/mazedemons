#include "Ranged/MDBeamComponent.h"

#include "AbilitySystemComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "LockOn/MDLockOnComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatStatics.h"
#include "MDDamageNumbers.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Ranged/MDRangedAttributeSet.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedFX.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedWeaponData.h"

namespace MDBeamLocal
{
	constexpr float MaxStepSeconds = 0.25f;     // a hitch never advances the rules by more than this
	constexpr int32 MaxTicksPerFrame = 3;       // catch-up cap for the 10 Hz ticks
	constexpr float PushThreshold = 0.5f;       // attribute writes smaller than this are skipped (the owner predicts anyway)
	constexpr float AttackNotifyInterval = 0.25f; // soft lock refresh while the beam fires
	constexpr double RejectGraceSeconds = 0.6;  // a client waits this long for the server to confirm a beam it started
	constexpr double HostGraceSeconds = 0.05;   // the host's server state is the truth: no wait
	constexpr int32 MaxSweepsPerTick = 8;
	constexpr float SweepRadius = 6.0f;
	constexpr float EndFollowSpeed = 25.0f;     // remote machines chase the replicated end point at this rate
	constexpr float OverheatFxMaxSeconds = 1.2f;

	// One step of the beam: geometry by the Visibility channel, creatures by object type (a capsule need not block Visibility, trigger
	// volumes must not stop a laser). The nearer of the two wins, the same rule as AMDProjectile.
	bool TraceStep(UWorld* World, const FVector& A, const FVector& B, const FCollisionQueryParams& Query, FHitResult& OutHit)
	{
		if (!World)
		{
			return false;
		}
		const FCollisionShape Shape = FCollisionShape::MakeSphere(SweepRadius);
		FHitResult WorldHit;
		const bool bWorld = World->SweepSingleByChannel(WorldHit, A, B, FQuat::Identity, ECC_Visibility, Shape, Query);
		FHitResult PawnHit;
		const bool bPawn = World->SweepSingleByObjectType(PawnHit, A, B, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), Shape, Query);
		if (bWorld && (!bPawn || WorldHit.Time <= PawnHit.Time))
		{
			OutHit = WorldHit;
			return true;
		}
		if (bPawn)
		{
			OutHit = PawnHit;
			return true;
		}
		return false;
	}

	float DamageScale()
	{
		const TConsoleVariableData<float>* Var = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("md.Ranged.DamageScale"));
		return Var ? Var->GetValueOnGameThread() : 1.0f;
	}
}

UMDBeamComponent::UMDBeamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // woken while a beam, heat, an overheat lock or a cosmetic is alive
	SetIsReplicatedByDefault(true);
}

void UMDBeamComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMDBeamComponent, bBeamActive);
	DOREPLIFETIME(UMDBeamComponent, BeamEnd);
	DOREPLIFETIME(UMDBeamComponent, bOverheated);
	DOREPLIFETIME(UMDBeamComponent, OverheatEndServerTime);
}

UMDBeamComponent* UMDBeamComponent::Find(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMDBeamComponent* Comp = Actor->FindComponentByClass<UMDBeamComponent>())
	{
		return Comp;
	}
	const APlayerState* PS = Cast<APlayerState>(Actor);
	if (!PS)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			PS = Pawn->GetPlayerState();
		}
		else if (const AController* Controller = Cast<AController>(Actor))
		{
			PS = Controller->PlayerState;
		}
	}
	return PS ? PS->FindComponentByClass<UMDBeamComponent>() : nullptr;
}

void UMDBeamComponent::BeginPlay()
{
	Super::BeginPlay();
	CachedState = Cast<AMDPlayerState>(GetOwner());
	ApplyOverheatTagLocally();
	if (bBeamActive || bOverheated)
	{
		WakeTick(); // a late joiner that already received the replicated state
	}
}

void UMDBeamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopCosmetics();
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(TAG_State_Overheated))
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Overheated);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// =====================================================================================================================
// Lookups
// =====================================================================================================================

UMDRangedComponent* UMDBeamComponent::GetRanged() const
{
	return UMDRangedComponent::Find(GetOwner());
}

UMDRangedWeaponData* UMDBeamComponent::GetData() const
{
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged ? Ranged->GetEquippedWeaponData() : nullptr;
}

APawn* UMDBeamComponent::GetPawn() const
{
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged ? Ranged->GetOwningPawn() : nullptr;
}

UMDRangedAttributeSet* UMDBeamComponent::GetAttributes() const
{
	const AMDPlayerState* State = CachedState.Get();
	if (!State)
	{
		State = Cast<AMDPlayerState>(GetOwner());
	}
	return State ? State->RangedAttributes.Get() : nullptr;
}

bool UMDBeamComponent::IsCurrentBeamWeapon() const
{
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged && Ranged->IsBeamWeapon();
}

bool UMDBeamComponent::IsChargeStyle() const
{
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged && Ranged->IsChargeStyleBeam();
}

double UMDBeamComponent::GetServerNow() const
{
	const UMDRangedComponent* Ranged = GetRanged();
	if (Ranged)
	{
		return Ranged->GetServerNow();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

int32 UMDBeamComponent::GetUpgradeLevel() const
{
	const UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetOwner());
	return Inv ? Inv->GetSlotUpgradeLevel(MDItemType::RangedSlot) : 0;
}

FMDHeatParams UMDBeamComponent::BuildHeatParams() const
{
	FMDHeatParams Params;
	if (const UMDRangedWeaponData* Data = GetData())
	{
		Params.HeatPerSecond = FMDBeamRules::UpgradedHeatPerSecond(Data->Beam.HeatPerSecond, GetUpgradeLevel());
		Params.VentPerSecond = Data->Beam.VentPerSecond;
		Params.MaxHeat = Data->Beam.MaxHeat;
		Params.OverheatLock = Data->Beam.OverheatLock;
	}
	return Params;
}

FMDChargeParams UMDBeamComponent::BuildChargeParams() const
{
	FMDChargeParams Params;
	if (const UMDRangedWeaponData* Data = GetData())
	{
		Params.MaxCharge = Data->Beam.MaxCharge;
		Params.DrainPerSecond = Data->Beam.ChargeDrainPerSecond;
		Params.RegenPerSecond = Data->Beam.ChargeRegenPerSecond;
	}
	return Params;
}

EMDBeamBlock UMDBeamComponent::ComputeBlock(bool bOverheatedNow, bool bNoCharge) const
{
	const UMDRangedComponent* Ranged = GetRanged();
	const APawn* Pawn = GetPawn();
	return FMDBeamRules::CanFire(Ranged && Ranged->IsRangedMode(), IsCurrentBeamWeapon(), !Pawn || UMDGameplayStatics::IsPawnDead(Pawn),
		Pawn && UMDPoiseComponent::IsActorStunned(Pawn), Ranged && Ranged->IsReloading(), bOverheatedNow, bNoCharge);
}

EMDBeamBlock UMDBeamComponent::GetBlockReason() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return ComputeBlock(ServerHeat.IsOverheated() || bOverheated, IsChargeStyle() && ServerCharge.bDry);
	}
	return ComputeBlock(PredHeat.IsOverheated() || bOverheated, IsChargeStyle() && PredCharge.bDry);
}

// =====================================================================================================================
// Views
// =====================================================================================================================

bool UMDBeamComponent::IsOverheatedView() const
{
	return bOverheated || PredHeat.IsOverheated() || ServerHeat.IsOverheated();
}

float UMDBeamComponent::GetHeatView() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return ServerHeat.Heat;
	}
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged && Ranged->IsOwnerLocal() ? PredHeat.Heat : 0.0f;
}

float UMDBeamComponent::GetMaxHeatView() const
{
	const UMDRangedWeaponData* Data = GetData();
	return Data ? Data->Beam.MaxHeat : FMDBeamRules::DefaultMaxHeat;
}

float UMDBeamComponent::GetChargeView() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return ServerCharge.Charge;
	}
	const UMDRangedComponent* Ranged = GetRanged();
	return Ranged && Ranged->IsOwnerLocal() ? PredCharge.Charge : 0.0f;
}

float UMDBeamComponent::GetMaxChargeView() const
{
	const UMDRangedWeaponData* Data = GetData();
	return Data ? Data->Beam.MaxCharge : 100.0f;
}

float UMDBeamComponent::GetOverheatLockFraction() const
{
	const UMDRangedWeaponData* Data = GetData();
	const float Lock = Data ? Data->Beam.OverheatLock : FMDBeamRules::DefaultOverheatLock;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return FMDBeamRules::OverheatLockFraction(ServerHeat, Lock);
	}
	const UMDRangedComponent* Ranged = GetRanged();
	if (Ranged && Ranged->IsOwnerLocal() && PredHeat.IsOverheated())
	{
		return FMDBeamRules::OverheatLockFraction(PredHeat, Lock);
	}
	if (bOverheated && Lock > KINDA_SMALL_NUMBER)
	{
		return FMath::Clamp(static_cast<float>(OverheatEndServerTime - GetServerNow()) / Lock, 0.0f, 1.0f);
	}
	return 0.0f;
}

// =====================================================================================================================
// Owner machine
// =====================================================================================================================

bool UMDBeamComponent::OwnerStartBeam()
{
	UMDRangedComponent* Ranged = GetRanged();
	UWorld* World = GetWorld();
	if (!Ranged || !World || !Ranged->IsOwnerLocal() || GetBlockReason() != EMDBeamBlock::None)
	{
		return false;
	}
	if (bLocalBeamOn)
	{
		return true;
	}
	bLocalBeamOn = true;
	LocalBeamStartTime = World->GetTimeSeconds();
	LockNotifyTimer = 0.0f;
	// Autonomous soft lock: firing picks / keeps / retargets the soft target (local controller only; a hard lock is left alone).
	if (APawn* Pawn = GetPawn())
	{
		if (Pawn->IsPlayerControlled())
		{
			if (UMDLockOnComponent* Lock = UMDLockOnComponent::FindFor(Pawn))
			{
				Lock->NotifyPlayerAttacked();
			}
		}
	}
	if (GetOwner()->HasAuthority())
	{
		ServerStartBeam();
	}
	else
	{
		Server_BeamStart();
	}
	WakeTick();
	return true;
}

void UMDBeamComponent::OwnerStopBeam()
{
	if (!bLocalBeamOn)
	{
		return;
	}
	bLocalBeamOn = false;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerStopBeam();
	}
	else
	{
		Server_BeamStop();
	}
	WakeTick();
}

void UMDBeamComponent::TickOwnerPrediction(float DeltaSeconds)
{
	SyncFromServerSample();
	if (IsChargeStyle())
	{
		PredCharge = FMDBeamRules::StepCharge(PredCharge, bLocalBeamOn, BuildChargeParams(), DeltaSeconds);
		if (PredCharge.bDry && bLocalBeamOn)
		{
			OwnerStopBeam();
		}
	}
	else
	{
		PredHeat = FMDBeamRules::StepHeat(PredHeat, bLocalBeamOn, BuildHeatParams(), DeltaSeconds);
		if (PredHeat.IsOverheated() && bLocalBeamOn)
		{
			OwnerStopBeam(); // the shooter's own prediction: the beam stops now, the server confirms with bOverheated
		}
	}
}

void UMDBeamComponent::SyncFromServerSample()
{
	const UMDRangedAttributeSet* Attrs = GetAttributes();
	if (!Attrs)
	{
		return;
	}
	const float ServerHeatNow = Attrs->GetHeat();
	if (!FMath::IsNearlyEqual(ServerHeatNow, LastSeenServerHeat, 0.001f))
	{
		LastSeenServerHeat = ServerHeatNow;
		if (!PredHeat.IsOverheated())
		{
			PredHeat.Heat = FMDBeamRules::ReconcileHeat(PredHeat.Heat, ServerHeatNow);
		}
	}
	const float ServerChargeNow = Attrs->GetCharge();
	if (!FMath::IsNearlyEqual(ServerChargeNow, LastSeenServerCharge, 0.001f))
	{
		LastSeenServerCharge = ServerChargeNow;
		PredCharge.Charge = FMDBeamRules::ReconcileHeat(PredCharge.Charge, ServerChargeNow);
	}
}

// =====================================================================================================================
// Server
// =====================================================================================================================

void UMDBeamComponent::Server_BeamStart_Implementation()
{
	ServerStartBeam();
}

void UMDBeamComponent::Server_BeamStop_Implementation()
{
	ServerStopBeam();
}

EMDBeamBlock UMDBeamComponent::ServerStartBeam()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return EMDBeamBlock::NotRangedMode;
	}
	const EMDBeamBlock Block = ComputeBlock(ServerHeat.IsOverheated() || bOverheated, IsChargeStyle() && ServerCharge.bDry);
	if (Block != EMDBeamBlock::None)
	{
		return Block;
	}
	if (!bBeamActive)
	{
		bBeamActive = true;
		TickAccumulator = 1.0f / FMath::Max(GetData() ? GetData()->Beam.TickHz : 10.0f, 1.0f); // the first damage tick lands next frame
		HitLog.Reset();
		if (UMDRangedComponent* Ranged = GetRanged())
		{
			FVector Muzzle;
			FVector Dir;
			if (Ranged->ComputeServerBeamAim(Muzzle, Dir))
			{
				BeamEnd = Muzzle;
			}
		}
		GetOwner()->ForceNetUpdate();
	}
	WakeTick();
	return EMDBeamBlock::None;
}

void UMDBeamComponent::ServerStopBeam()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bBeamActive)
	{
		return;
	}
	bBeamActive = false;
	TickAccumulator = 0.0f;
	FlushNumbers(true);
	GetOwner()->ForceNetUpdate();
	WakeTick();
}

void UMDBeamComponent::AdvanceServer(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, MDBeamLocal::MaxStepSeconds);

	// A running beam stops the moment any gate closes (death, stagger, a reload, leaving ranged mode, swapping the gun).
	if (bBeamActive)
	{
		const EMDBeamBlock Block = ComputeBlock(bOverheated, IsChargeStyle() && ServerCharge.bDry);
		if (Block != EMDBeamBlock::None)
		{
			ServerStopBeam();
		}
	}

	if (IsChargeStyle())
	{
		ServerCharge = FMDBeamRules::StepCharge(ServerCharge, bBeamActive, BuildChargeParams(), Dt);
		if (ServerCharge.bDry && bBeamActive)
		{
			ServerStopBeam();
		}
	}
	else
	{
		ServerHeat = FMDBeamRules::StepHeat(ServerHeat, bBeamActive, BuildHeatParams(), Dt);
		if (ServerHeat.IsOverheated() && !bOverheated)
		{
			SetOverheated(true);
			ServerStopBeam();
		}
		else if (!ServerHeat.IsOverheated() && bOverheated)
		{
			SetOverheated(false);
		}
	}

	if (bBeamActive)
	{
		const float Interval = 1.0f / FMath::Max(GetData() ? GetData()->Beam.TickHz : 10.0f, 1.0f);
		TickAccumulator += Dt;
		int32 Ticks = 0;
		while (TickAccumulator >= Interval - KINDA_SMALL_NUMBER && Ticks < MDBeamLocal::MaxTicksPerFrame && bBeamActive)
		{
			ServerBeamTick();
			TickAccumulator -= Interval;
			++Ticks;
		}
		TickAccumulator = FMath::Min(TickAccumulator, Interval); // a hitch drops the backlog instead of burst-firing it
	}

	FlushNumbers(false);
	PushAttributes(false);
}

void UMDBeamComponent::ServerBeamTick()
{
	UMDRangedComponent* Ranged = GetRanged();
	FVector Muzzle;
	FVector Dir;
	if (Ranged && Ranged->ComputeServerBeamAim(Muzzle, Dir))
	{
		ServerBeamTickAt(Muzzle, Dir);
	}
}

int32 UMDBeamComponent::ServerBeamTickAt(const FVector& Muzzle, const FVector& InDir)
{
	UWorld* World = GetWorld();
	const UMDRangedWeaponData* Data = GetData();
	APawn* Pawn = GetPawn();
	const FVector Dir = InDir.GetSafeNormal();
	if (!World || !Data || !GetOwner() || !GetOwner()->HasAuthority() || Dir.IsNearlyZero())
	{
		return 0;
	}
	++ServerTicks;
	HitLog.Reset();

	const float Range = FMath::Max(Data->Beam.Range, 100.0f);
	const float Hz = FMath::Max(Data->Beam.TickHz, 1.0f);
	float Scale = MDBeamLocal::DamageScale();
	if (const UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		Scale *= 1.0f + ASC->GetPerkAttackPct();
	}
	if (const UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetOwner()))
	{
		Scale *= Inv->GetSlotConditionMultiplier(MDItemType::RangedSlot);
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDBeamTick), false, Pawn);
	if (AActor* OwnerActor = GetOwner())
	{
		Query.AddIgnoredActor(OwnerActor);
	}
	const FVector FullEnd = Muzzle + Dir * Range;
	FVector End = FullEnd;
	int32 Hurt = 0;
	const double Now = World->GetTimeSeconds();

	for (int32 i = 0; i < MDBeamLocal::MaxSweepsPerTick; ++i)
	{
		FHitResult Hit;
		if (!MDBeamLocal::TraceStep(World, Muzzle, FullEnd, Query, Hit))
		{
			break; // nothing more in range: the beam runs to full length
		}
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || !MDDamageNumbers::IsEligibleVictim(HitActor))
		{
			End = Hit.ImpactPoint; // world geometry stops the beam
			break;
		}
		Query.AddIgnoredActor(HitActor);
		if (!HitLog.TryRegister(HitActor))
		{
			continue; // already hurt this tick (another component of the same victim)
		}

		const float Distance = static_cast<float>(FVector::Dist(Muzzle, Hit.ImpactPoint));
		FMDHitConfirm Confirm;
		FMDRangedHitParams Params;
		Params.Damage = FMDBeamRules::TickDamage(Data->Beam.DamagePerSecond, Hz, Distance, Data->FalloffStart, Data->FalloffEnd, Data->FalloffMinMult) * Scale;
		Params.PoiseDamage = Data->PoiseDamage;
		Params.ReactionCap = Data->ReactionCap;
		Params.DamageType = Data->DefaultDamageType;
		Params.Kind = EMDHitKind::Beam;
		Params.ShotOrigin = Muzzle;
		Params.OutConfirm = &Confirm;
		if (UMDCombatStatics::ApplyRangedHit(Pawn, HitActor, Hit, Params))
		{
			++Hurt;
			FMDBeamNumbers::Add(NumberBuckets, Confirm, Now);
			End = Hit.ImpactPoint;
			break; // the beam ends on the first victim that took damage
		}
		// Team rules (friendly fire off), dodge i-frames or a corpse: the beam passes through to whatever is behind.
	}

	BeamEnd = FMDBeamRules::QuantizeBeamEnd(End);
	if (Hurt > 0)
	{
		WearAccumulator += FMDBeamRules::WearPerLandedTick(Data->Beam.BeamDurabilityPerSecond, Hz);
		BankWear();
	}
	return Hurt;
}

void UMDBeamComponent::BankWear()
{
	// Whole points go to the weapon's durability; the fraction stays banked (0.5 / s at 10 Hz = 0.05 per landed tick).
	const int32 Points = FMath::FloorToInt(WearAccumulator + KINDA_SMALL_NUMBER);
	if (Points > 0)
	{
		WearAccumulator = FMath::Max(WearAccumulator - static_cast<float>(Points), 0.0f);
		if (UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(GetOwner()))
		{
			Inv->ApplyWearToSlot(MDItemType::RangedSlot, Points);
		}
	}
}

void UMDBeamComponent::FlushNumbers(bool bAll)
{
	if (NumberBuckets.IsEmpty())
	{
		return;
	}
	TArray<FMDHitConfirm> Due;
	FMDBeamNumbers::Collect(NumberBuckets, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, bAll, Due);
	for (const FMDHitConfirm& Confirm : Due)
	{
		++NumbersSent;
		UMDCombatStatics::SendHitConfirm(GetPawn(), Confirm);
	}
}

void UMDBeamComponent::SetOverheated(bool bNewOverheated)
{
	if (bOverheated == bNewOverheated)
	{
		return;
	}
	bOverheated = bNewOverheated;
	const UMDRangedWeaponData* Data = GetData();
	OverheatEndServerTime = bNewOverheated ? static_cast<float>(GetServerNow() + (Data ? Data->Beam.OverheatLock : FMDBeamRules::DefaultOverheatLock)) : 0.0f;
	ApplyOverheatTagLocally();
	PlayOverheatCue(bNewOverheated);
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
	WakeTick();
}

void UMDBeamComponent::PushAttributes(bool bForce)
{
	UMDRangedAttributeSet* Attrs = GetAttributes();
	if (!Attrs)
	{
		return;
	}
	const FMDHeatParams HeatParams = BuildHeatParams();
	if (!FMath::IsNearlyEqual(Attrs->GetMaxHeat(), HeatParams.MaxHeat, 0.01f))
	{
		Attrs->InitMaxHeat(HeatParams.MaxHeat);
	}
	const FMDChargeParams ChargeParams = BuildChargeParams();
	if (!FMath::IsNearlyEqual(Attrs->GetMaxCharge(), ChargeParams.MaxCharge, 0.01f))
	{
		Attrs->InitMaxCharge(ChargeParams.MaxCharge);
	}
	const bool bHeatEdge = (ServerHeat.Heat <= 0.0f) != (LastPushedHeat <= 0.0f) || ServerHeat.Heat >= HeatParams.MaxHeat;
	if (bForce || bHeatEdge || FMath::Abs(ServerHeat.Heat - LastPushedHeat) >= MDBeamLocal::PushThreshold)
	{
		LastPushedHeat = ServerHeat.Heat;
		Attrs->InitHeat(FMath::Clamp(ServerHeat.Heat, 0.0f, HeatParams.MaxHeat));
	}
	if (bForce || FMath::Abs(ServerCharge.Charge - LastPushedCharge) >= MDBeamLocal::PushThreshold)
	{
		LastPushedCharge = ServerCharge.Charge;
		Attrs->InitCharge(FMath::Clamp(ServerCharge.Charge, 0.0f, ChargeParams.MaxCharge));
	}
}

// =====================================================================================================================
// Replication callbacks (clients)
// =====================================================================================================================

void UMDBeamComponent::OnRep_BeamActive()
{
	if (!bBeamActive && bLocalBeamOn)
	{
		bLocalBeamOn = false; // the server ended the beam (overheat, stagger, ...): the shooter's prediction ends with it
	}
	bShownEndValid = false;
	WakeTick();
}

void UMDBeamComponent::OnRep_Overheated()
{
	ApplyOverheatTagLocally();
	PlayOverheatCue(bOverheated);
	const UMDRangedComponent* Ranged = GetRanged();
	if (Ranged && Ranged->IsOwnerLocal())
	{
		if (bOverheated)
		{
			const UMDRangedWeaponData* Data = GetData();
			const float Lock = Data ? Data->Beam.OverheatLock : FMDBeamRules::DefaultOverheatLock;
			PredHeat.LockRemaining = FMath::Clamp(static_cast<float>(OverheatEndServerTime - GetServerNow()), 0.05f, Lock);
			PredHeat.Heat = GetMaxHeatView() * FMath::Clamp(PredHeat.LockRemaining / FMath::Max(Lock, 0.01f), 0.0f, 1.0f);
			bLocalBeamOn = false;
		}
		else
		{
			PredHeat.LockRemaining = 0.0f;
			PredHeat.Heat = 0.0f;
		}
	}
	WakeTick();
}

void UMDBeamComponent::Test_SetReplicatedBeam(bool bActive, const FVector& End)
{
	bBeamActive = bActive;
	BeamEnd = FMDBeamRules::QuantizeBeamEnd(End);
	OnRep_BeamActive();
}

void UMDBeamComponent::Test_SetReplicatedOverheat(bool bOverheat, float EndServerTime)
{
	bOverheated = bOverheat;
	OverheatEndServerTime = EndServerTime;
	OnRep_Overheated();
}

// =====================================================================================================================
// Every machine: tag, cues, cosmetics, tick
// =====================================================================================================================

void UMDBeamComponent::ApplyOverheatTagLocally()
{
	if (UMDAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(GetOwner()))
	{
		const bool bHasTag = ASC->HasMatchingGameplayTag(TAG_State_Overheated);
		if (bOverheated && !bHasTag)
		{
			ASC->AddLooseGameplayTag(TAG_State_Overheated);
		}
		else if (!bOverheated && bHasTag)
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Overheated);
		}
	}
}

void UMDBeamComponent::PlayOverheatCue(bool bStarting)
{
	UWorld* World = GetWorld();
	const UMDRangedWeaponData* Data = GetData();
	UMDRangedComponent* Ranged = GetRanged();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Data || !Ranged || Data->OverheatFX.IsNull())
	{
		return;
	}
	UNiagaraSystem* System = Data->OverheatFX.LoadSynchronous();
	FVector Muzzle;
	if (System && Ranged->GetMuzzleWorld(Muzzle))
	{
		// The big puff when the weapon locks, a smaller one when it has vented and is free again.
		MDRangedFX::SpawnOneShot(World, System, Muzzle, FRotator::ZeroRotator, FVector(bStarting ? 1.0f : 0.5f), MDBeamLocal::OverheatFxMaxSeconds);
	}
}

void UMDBeamComponent::StartCosmetics()
{
	UWorld* World = GetWorld();
	const UMDRangedWeaponData* Data = GetData();
	UMDRangedComponent* Ranged = GetRanged();
	FVector Muzzle;
	if (!World || !Data || !Ranged || !Ranged->GetMuzzleWorld(Muzzle))
	{
		return;
	}
	if (!BeamFXComp && !Data->BeamFX.IsNull())
	{
		if (UNiagaraSystem* System = Data->BeamFX.LoadSynchronous())
		{
			BeamFXComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Muzzle, FRotator::ZeroRotator, FVector::OneVector, false, true, ENCPoolMethod::None, true);
			if (BeamFXComp)
			{
				BeamFXComp->SetVariableLinearColor(FName(TEXT("User.Color")), Data->BeamColor);
			}
		}
	}
	if (!MuzzleFXComp && !Data->MuzzleFlashFX.IsNull())
	{
		if (UNiagaraSystem* System = Data->MuzzleFlashFX.LoadSynchronous())
		{
			MuzzleFXComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Muzzle, FRotator::ZeroRotator, FVector::OneVector, false, true, ENCPoolMethod::None, true);
		}
	}
	if (!HitFXComp && !Data->ImpactFX.IsNull())
	{
		if (UNiagaraSystem* System = Data->ImpactFX.LoadSynchronous())
		{
			HitFXComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Muzzle, FRotator::ZeroRotator, FVector::OneVector, false, true, ENCPoolMethod::None, true);
		}
	}
}

void UMDBeamComponent::StopCosmetics()
{
	auto Kill = [](TObjectPtr<UNiagaraComponent>& Comp)
	{
		if (Comp)
		{
			Comp->Deactivate();
			Comp->DestroyComponent();
			Comp = nullptr;
		}
	};
	Kill(BeamFXComp);
	Kill(MuzzleFXComp);
	Kill(HitFXComp);
}

void UMDBeamComponent::UpdateCosmetics(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UMDRangedComponent* Ranged = GetRanged();
	const UMDRangedWeaponData* Data = GetData();
	const bool bShow = IsBeamShown() && !IsOverheatedView() && Ranged && Data && IsCurrentBeamWeapon() && Ranged->GetWeaponMesh();
	if (!bShow)
	{
		if (BeamFXComp || MuzzleFXComp || HitFXComp)
		{
			StopCosmetics();
		}
		bShownEndValid = false;
		return;
	}
	FVector Muzzle;
	if (!Ranged->GetMuzzleWorld(Muzzle))
	{
		return;
	}
	if (!BeamFXComp && !MuzzleFXComp && !HitFXComp)
	{
		StartCosmetics();
	}

	FVector End = Muzzle;
	bool bHaveEnd = false;
	if (Ranged->IsOwnerLocal())
	{
		// The shooter's own beam follows the camera aim every frame (no wait for the server).
		FVector AimMuzzle;
		FVector AimDir;
		float AimDistance = 0.0f;
		if (Ranged->ComputeLocalBeamAim(AimMuzzle, AimDir, AimDistance))
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MDBeamCosmetic), false, GetPawn());
			if (AActor* OwnerActor = GetOwner())
			{
				Query.AddIgnoredActor(OwnerActor);
			}
			const FVector Full = AimMuzzle + AimDir * FMath::Max(Data->Beam.Range, 100.0f);
			FHitResult Hit;
			End = MDBeamLocal::TraceStep(World, AimMuzzle, Full, Query, Hit) ? FVector(Hit.ImpactPoint) : Full;
			bHaveEnd = true;
		}
	}
	if (!bHaveEnd)
	{
		const FVector Target = FVector(BeamEnd);
		if (!bShownEndValid || DeltaSeconds <= 0.0f)
		{
			ShownEnd = Target;
			bShownEndValid = true;
		}
		else
		{
			ShownEnd = FMath::VInterpTo(ShownEnd, Target, DeltaSeconds, MDBeamLocal::EndFollowSpeed);
		}
		End = ShownEnd;
	}

	if (BeamFXComp)
	{
		BeamFXComp->SetWorldLocation(Muzzle);
		BeamFXComp->SetVariableVec3(FName(TEXT("User.LaserEnd")), End);
	}
	if (MuzzleFXComp)
	{
		MuzzleFXComp->SetWorldLocation(Muzzle);
		MuzzleFXComp->SetWorldRotation((End - Muzzle).Rotation());
	}
	if (HitFXComp)
	{
		HitFXComp->SetWorldLocation(End);
	}
}

void UMDBeamComponent::WakeTick()
{
	SetComponentTickEnabled(true);
}

bool UMDBeamComponent::NeedsTick() const
{
	return bBeamActive || bLocalBeamOn || bOverheated || ServerHeat.Heat > 0.0f || ServerHeat.IsOverheated() || PredHeat.Heat > 0.0f
		|| PredHeat.IsOverheated() || ServerCharge.Charge < 99.99f || PredCharge.Charge < 99.99f || !NumberBuckets.IsEmpty()
		|| BeamFXComp != nullptr || MuzzleFXComp != nullptr || HitFXComp != nullptr;
}

void UMDBeamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!World || !GetOwner())
	{
		return;
	}
	const bool bAuthority = GetOwner()->HasAuthority();
	UMDRangedComponent* Ranged = GetRanged();
	const bool bOwnerLocal = Ranged && Ranged->IsOwnerLocal();

	if (bAuthority)
	{
		AdvanceServer(DeltaTime);
		PredHeat = ServerHeat; // the host owns the truth: no separate prediction
		PredCharge = ServerCharge;
	}
	else if (bOwnerLocal)
	{
		TickOwnerPrediction(FMath::Min(DeltaTime, MDBeamLocal::MaxStepSeconds));
	}

	if (bOwnerLocal && bLocalBeamOn)
	{
		const EMDBeamBlock Block = GetBlockReason();
		const double Grace = bAuthority ? MDBeamLocal::HostGraceSeconds : MDBeamLocal::RejectGraceSeconds;
		if (Block != EMDBeamBlock::None && Block != EMDBeamBlock::Overheated)
		{
			OwnerStopBeam(); // the gates closed on this machine (stagger, death, a reload, left ranged mode)
		}
		else if (!bBeamActive && World->GetTimeSeconds() - LocalBeamStartTime > Grace)
		{
			bLocalBeamOn = false; // the server ended or never confirmed this beam: drop the prediction (the trigger retries while held)
		}
		else
		{
			LockNotifyTimer += DeltaTime;
			if (LockNotifyTimer >= MDBeamLocal::AttackNotifyInterval)
			{
				LockNotifyTimer = 0.0f;
				if (APawn* Pawn = GetPawn())
				{
					if (UMDLockOnComponent* Lock = UMDLockOnComponent::FindFor(Pawn))
					{
						Lock->NotifyPlayerAttacked();
					}
				}
			}
		}
	}

	UpdateCosmetics(DeltaTime);
	if (!NeedsTick())
	{
		SetComponentTickEnabled(false);
	}
}
