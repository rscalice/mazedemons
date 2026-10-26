#include "MDCombatStatics.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "MDAbilitySystemComponent.h"
#include "MDCombatDebug.h"
#include "MDCombatFeedbackComponent.h"
#include "MDCombatRules.h"
#include "MDComboComponent.h"
#include "MDComboSet.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPoiseComponent.h"
#include "MDPoiseProfile.h"
#include "MDUISettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

bool UMDCombatStatics::ApplyMeleeHit(AActor* Attacker, AActor* Victim, const FHitResult& Hit)
{
	if (!Attacker || !Victim || Attacker == Victim || !Victim->HasAuthority())
	{
		return false;
	}
	UMDAbilitySystemComponent* VictimASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Victim);
	UMDAbilitySystemComponent* AttackerASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Attacker);
	if (!VictimASC || !AttackerASC)
	{
		return false;
	}

	UMDComboComponent* Combo = UMDComboComponent::FindFor(Attacker);
	FMDAttackStep Step; // default: a plain light hit
	const bool bHasStep = Combo && Combo->GetActiveStep(Step);
	if (bHasStep && !Combo->TryRegisterSwingHit(Victim))
	{
		return false; // this swing already hit this target
	}

	FMDHitContext Context;
	Context.Instigator = Attacker;
	Context.Causer = Attacker;
	Context.BaseDamage = AttackerASC->GetAttackPower();
	Context.Kind = bHasStep ? Combo->GetActiveKind() : EMDAttackKind::Light;
	Context.Step = Step;
	Context.PoiseDamage = Step.PoiseDamage;
	Context.Reaction = Step.Reaction;
	Context.Knockback = Step.KnockbackStrength;
	Context.ImpactPoint = Hit.ImpactPoint.IsNearlyZero() ? Victim->GetActorLocation() : FVector(Hit.ImpactPoint);
	Context.ImpactNormal = Hit.ImpactNormal;
	Context.AttackerLocation = Attacker->GetActorLocation();
	Context.ImpactTag = Step.ImpactTag;
	Context.PhysMaterial = Hit.PhysMaterial.Get();

	FMDDamageResult Damage;
	if (!VictimASC->ApplyDamage(Context, Damage))
	{
		return false; // blocked (team rules, i-frames, dead) or fully absorbed
	}

	if (MDCombatDebug::ShouldDrawTraces())
	{
		DrawDebugSphere(Victim->GetWorld(), Context.ImpactPoint, 6.0f, 8, Damage.bCrit ? FColor::Yellow : FColor::Red, false, 0.5f);
	}

	// Poise and reaction.
	UMDPoiseComponent* Poise = UMDPoiseComponent::FindFor(Victim);
	FMDPoiseHitResult PoiseResult;
	if (Poise)
	{
		PoiseResult = Poise->ApplyHit(Context, Damage);
	}
	const EMDHitReaction Reaction = PoiseResult.bValid ? PoiseResult.Reaction : Context.Reaction;

	// Knockback: only when the hit staggered (a flinch or hyper-armor does not push).
	if (Context.Knockback > 0.0f && static_cast<int32>(Reaction) >= static_cast<int32>(EMDHitReaction::Stagger))
	{
		const APawn* VictimPawn = Cast<APawn>(Victim);
		const bool bPlayer = VictimPawn && VictimPawn->GetPlayerState() != nullptr;
		const float Resist = Poise ? Poise->GetProfile()->KnockbackResist : 0.0f;
		const FVector Velocity = UMDCombatRules::KnockbackVelocity(Context.Knockback, Context.AttackerLocation, Victim->GetActorLocation(), bPlayer, Resist);
		if (Poise)
		{
			Poise->ApplyKnockback(Velocity, bPlayer);
		}
		else if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim))
		{
			VictimCharacter->LaunchCharacter(Velocity, true, !bPlayer);
		}
	}

	// Tell the attacker: hit-stop, shake, rumble, damage number.
	UMDCombatFeedbackComponent* Feedback = nullptr;
	if (const APawn* AttackerPawn = Cast<APawn>(Attacker))
	{
		if (const AController* Controller = AttackerPawn->GetController())
		{
			Feedback = Controller->FindComponentByClass<UMDCombatFeedbackComponent>();
		}
	}
	if (!Feedback)
	{
		Feedback = Attacker->FindComponentByClass<UMDCombatFeedbackComponent>();
	}
	if (Feedback)
	{
		const float StopScale = (Combo && Combo->ComboSet) ? Combo->ComboSet->HitStopScale : 1.0f;
		FMDHitConfirm Confirm;
		Confirm.Victim = Victim;
		Confirm.Damage = Damage.Applied;
		Confirm.bCrit = Damage.bCrit;
		Confirm.Location = Context.ImpactPoint;
		Confirm.HitStopFrames = FMath::RoundToInt(static_cast<float>(Step.HitStopFrames) * StopScale);
		Confirm.ShakeScale = Step.ShakeScale;
		Confirm.Kind = Context.Kind;
		Confirm.Reaction = Reaction;
		Feedback->Client_ConfirmHit(Confirm);
	}
	return true;
}

void UMDCombatStatics::GetWeaponTracePoints(const AActor* Weapon, FVector& Top, FVector& Bottom)
{
	Top = FVector::ZeroVector;
	Bottom = FVector::ZeroVector;
	if (!IsValid(Weapon))
	{
		return;
	}
	const USceneComponent* Root = Weapon->GetRootComponent();
	if (!Root)
	{
		Top = Bottom = Weapon->GetActorLocation();
		return;
	}
	static const FName TopSocket(TEXT("TopTrace"));
	static const FName BottomSocket(TEXT("BottomTrace"));
	if (Root->DoesSocketExist(TopSocket) && Root->DoesSocketExist(BottomSocket))
	{
		Top = Root->GetSocketLocation(TopSocket);
		Bottom = Root->GetSocketLocation(BottomSocket);
		return;
	}

	// No sockets: the long axis of the mesh bounds. The hilt end is the one nearer the component origin (a held sword's pivot is the grip).
	const FBox Local = Root->CalcBounds(FTransform::Identity).GetBox();
	if (!Local.IsValid)
	{
		Top = Bottom = Root->GetComponentLocation();
		return;
	}
	const FVector Size = Local.GetSize();
	int32 Axis = 0;
	if (Size.Y > Size.X)
	{
		Axis = 1;
	}
	if (Size.Z > Size[Axis])
	{
		Axis = 2;
	}
	const double Lo = Local.Min[Axis];
	const double Hi = Local.Max[Axis];
	const bool bHiltLow = FMath::Abs(Lo) <= FMath::Abs(Hi);
	const double Hilt = bHiltLow ? Lo : Hi;
	const double Tip = bHiltLow ? Hi : Lo;
	FVector TipLocal = Local.GetCenter();
	FVector BottomLocal = TipLocal;
	TipLocal[Axis] = Tip;
	BottomLocal[Axis] = Hilt + (Tip - Hilt) * 0.25;
	const FTransform& ToWorld = Root->GetComponentTransform();
	Top = ToWorld.TransformPosition(TipLocal);
	Bottom = ToWorld.TransformPosition(BottomLocal);
}

EMDWeaponHand UMDCombatStatics::GetAttackerHand(const AActor* Attacker)
{
	const UMDComboComponent* Combo = Attacker ? UMDComboComponent::FindFor(Attacker) : nullptr;
	return Combo ? Combo->GetActiveStepHand() : EMDWeaponHand::Main;
}

void UMDCombatStatics::SweepWeaponHand(AActor* Attacker, AActor* Weapon, FVector& PrevTop, FVector& PrevBottom, TArray<AActor*>& HitThisAttack)
{
	if (!IsValid(Attacker) || !IsValid(Weapon))
	{
		return; // empty off hand, or the weapon was just dropped
	}
	FVector Top;
	FVector Bottom;
	GetWeaponTracePoints(Weapon, Top, Bottom);
	const bool bHavePrev = !PrevTop.IsNearlyZero();
	const FVector StartTop = bHavePrev ? PrevTop : Top;
	const FVector StartBottom = bHavePrev ? PrevBottom : Bottom;
	PrevTop = Top;
	PrevBottom = Bottom;

	UWorld* World = Attacker->GetWorld();
	const float Length = static_cast<float>(FVector::Dist(Top, Bottom));
	if (!World || Length < 1.0f)
	{
		return;
	}
	const bool bDraw = MDCombatDebug::ShouldDrawTraces();

	const FVector PrevMid = (StartTop + StartBottom) * 0.5;
	const FVector Mid = (Top + Bottom) * 0.5;
	const FQuat Rotation = FRotationMatrix::MakeFromX(Top - Bottom).ToQuat();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDWeaponSweep), true, Attacker);
	Params.AddIgnoredActor(Weapon);
	TArray<AActor*> Attached;
	Attacker->GetAttachedActors(Attached, true);
	Params.AddIgnoredActors(Attached);
	Params.AddIgnoredActors(HitThisAttack);

	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, PrevMid, Mid, Rotation, ECC_Visibility, FCollisionShape::MakeBox(FVector(Length * 0.5f, 10.0f, 10.0f)), Params);
	if (bDraw)
	{
		// Mirrors Kismet's DrawDebugBoxTraceSingle: start/end boxes, connecting corner lines, red miss / green hit.
		const float DrawTime = 1.0f;
		const FVector HalfExtent(Length * 0.5f, 10.0f, 10.0f);
		const FColor SweepColor = bHit ? FColor::Green : FColor::Red;
		DrawDebugBox(World, PrevMid, HalfExtent, Rotation, SweepColor, false, DrawTime);
		DrawDebugBox(World, Mid, HalfExtent, Rotation, SweepColor, false, DrawTime);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset(
				(Corner & 1) ? HalfExtent.X : -HalfExtent.X,
				(Corner & 2) ? HalfExtent.Y : -HalfExtent.Y,
				(Corner & 4) ? HalfExtent.Z : -HalfExtent.Z);
			const FVector Local = Rotation.RotateVector(Offset);
			DrawDebugLine(World, PrevMid + Local, Mid + Local, SweepColor, false, DrawTime);
		}
		DrawDebugLine(World, PrevMid, Mid, SweepColor, false, DrawTime);
		if (bHit)
		{
			DrawDebugBox(World, Hit.ImpactPoint, FVector(8.0f), FColor::Orange, false, DrawTime);
		}
	}
	if (!bHit)
	{
		return;
	}
	AActor* Victim = Hit.GetActor();
	if (!Victim || !Attacker->HasAuthority())
	{
		return; // only the server applies hits
	}
	HitThisAttack.AddUnique(Victim);

	UMDAbilitySystemComponent* VictimASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Victim);
	if (VictimASC)
	{
		if (!VictimASC->IsDead())
		{
			ApplyMeleeHit(Attacker, Victim, Hit);
		}
		return;
	}
	// No ability system (props, old actors): plain damage at the attacker's attack power, as the old BP trace did.
	const UMDAbilitySystemComponent* AttackerASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Attacker);
	const APawn* AttackerPawn = Cast<APawn>(Attacker);
	if (AttackerASC && AttackerASC->GetAttackPower() > 0.0f)
	{
		UGameplayStatics::ApplyDamage(Victim, AttackerASC->GetAttackPower(), AttackerPawn ? AttackerPawn->GetController() : nullptr, Attacker, UDamageType::StaticClass());
	}
}

namespace MDCombatTraceLocal
{
	struct FHandTraceState
	{
		FVector PrevTop = FVector::ZeroVector;
		FVector PrevBottom = FVector::ZeroVector;
	};

	struct FSwingTraceState
	{
		FHandTraceState Hands[2]; // 0 = main side (MainHand / TwoHanded), 1 = off hand
		TArray<AActor*> HitThisAttack; // shared by both hands: a target is hit once per trace window
		bool bLogged = false;
	};

	static TMap<TWeakObjectPtr<const AActor>, FSwingTraceState>& States()
	{
		static TMap<TWeakObjectPtr<const AActor>, FSwingTraceState> Map;
		return Map;
	}

	static const FName MainHandSocket(TEXT("MainHand"));
	static const FName OffHandSocket(TEXT("OffHand"));
	static const FName TwoHandedSocket(TEXT("TwoHanded"));
}

TArray<FName> UMDCombatStatics::TraceSocketsForHand(EMDWeaponHand Hand)
{
	using namespace MDCombatTraceLocal;
	switch (Hand)
	{
	case EMDWeaponHand::Off:
		return { OffHandSocket };
	case EMDWeaponHand::Both:
		return { MainHandSocket, TwoHandedSocket, OffHandSocket };
	case EMDWeaponHand::Main:
	default:
		return { MainHandSocket, TwoHandedSocket };
	}
}

EMDWeaponHand UMDCombatStatics::ResolveTraceHand(EMDTraceHandOverride Override, bool bHasCombo, EMDWeaponHand StepHand, bool bDualPairAttached, bool bSetDualWield)
{
	switch (Override)
	{
	case EMDTraceHandOverride::Main:
		return EMDWeaponHand::Main;
	case EMDTraceHandOverride::Off:
		return EMDWeaponHand::Off;
	case EMDTraceHandOverride::Both:
		return EMDWeaponHand::Both;
	case EMDTraceHandOverride::Auto:
	default:
		break;
	}
	if (!bHasCombo)
	{
		return EMDWeaponHand::Both; // no combo component: trace every weapon found
	}
	if (StepHand != EMDWeaponHand::Main)
	{
		return StepHand;
	}
	return (bDualPairAttached && bSetDualWield) ? EMDWeaponHand::Both : EMDWeaponHand::Main;
}

void UMDCombatStatics::BeginWeaponTrace(const AActor* Attacker)
{
	if (Attacker)
	{
		MDCombatTraceLocal::States().Add(Attacker, MDCombatTraceLocal::FSwingTraceState());
	}
}

void UMDCombatStatics::EndWeaponTrace(const AActor* Attacker)
{
	auto& Map = MDCombatTraceLocal::States();
	if (Attacker)
	{
		Map.Remove(Attacker);
	}
	for (auto It = Map.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UMDCombatStatics::TraceEquippedWeapons(AActor* Attacker, EMDTraceHandOverride TraceHand)
{
	using namespace MDCombatTraceLocal;
	if (!IsValid(Attacker) || !Attacker->HasAuthority())
	{
		return;
	}
	FSwingTraceState& State = States().FindOrAdd(Attacker);

	// Weapon actors by the socket they are attached to on their parent.
	TArray<AActor*> Attached;
	Attacker->GetAttachedActors(Attached, true);
	AActor* Found[3] = { nullptr, nullptr, nullptr }; // MainHand, TwoHanded, OffHand
	for (AActor* Actor : Attached)
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		const FName Socket = Actor->GetAttachParentSocketName();
		if (Socket == MainHandSocket)
		{
			Found[0] = Actor;
		}
		else if (Socket == TwoHandedSocket)
		{
			Found[1] = Actor;
		}
		else if (Socket == OffHandSocket)
		{
			Found[2] = Actor;
		}
	}

	const UMDComboComponent* Combo = UMDComboComponent::FindFor(Attacker);
	const EMDWeaponHand StepHand = Combo ? Combo->GetActiveStepHand() : EMDWeaponHand::Main;
	const bool bDualPair = Found[0] != nullptr && Found[2] != nullptr;
	const bool bSetDual = Combo && Combo->ComboSet && Combo->ComboSet->bDualWield;
	const EMDWeaponHand Hand = ResolveTraceHand(TraceHand, Combo != nullptr, StepHand, bDualPair, bSetDual);
	const TArray<FName> Sockets = TraceSocketsForHand(Hand);

	if (!State.bLogged)
	{
		State.bLogged = true;
		UE_LOG(LogTemp, Log, TEXT("TraceEquippedWeapons %s: resolved hand=%d (override=%d stepHand=%d dualPair=%d setDualWield=%d) found MainHand=%s TwoHanded=%s OffHand=%s (attached actors: %d)%s"),
			*GetNameSafe(Attacker), static_cast<int32>(Hand), static_cast<int32>(TraceHand), static_cast<int32>(StepHand), bDualPair ? 1 : 0, bSetDual ? 1 : 0,
			*GetNameSafe(Found[0]), *GetNameSafe(Found[1]), *GetNameSafe(Found[2]), Attached.Num(),
			(!Found[0] && !Found[1] && !Found[2]) ? TEXT(" -- NO WEAPON FOUND, nothing will trace") : TEXT(""));
	}

	auto Trace = [&](AActor* Weapon, FHandTraceState& Hs)
	{
		if (Weapon)
		{
			SweepWeaponHand(Attacker, Weapon, Hs.PrevTop, Hs.PrevBottom, State.HitThisAttack);
		}
	};
	if (Sockets.Contains(MainHandSocket))
	{
		Trace(Found[0], State.Hands[0]);
	}
	if (Sockets.Contains(TwoHandedSocket))
	{
		Trace(Found[1], State.Hands[0]);
	}
	if (Sockets.Contains(OffHandSocket))
	{
		Trace(Found[2], State.Hands[1]);
	}
}

void UMDCombatStatics::GetSwingWeapons(const AActor* Attacker, EMDTraceHandOverride TraceHand, TArray<AActor*>& OutWeapons)
{
	using namespace MDCombatTraceLocal;
	OutWeapons.Reset();
	if (!IsValid(Attacker))
	{
		return;
	}
	TArray<AActor*> Attached;
	Attacker->GetAttachedActors(Attached, true);
	AActor* Found[3] = { nullptr, nullptr, nullptr }; // MainHand, TwoHanded, OffHand
	for (AActor* Actor : Attached)
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		const FName Socket = Actor->GetAttachParentSocketName();
		if (Socket == MainHandSocket)
		{
			Found[0] = Actor;
		}
		else if (Socket == TwoHandedSocket)
		{
			Found[1] = Actor;
		}
		else if (Socket == OffHandSocket)
		{
			Found[2] = Actor;
		}
	}
	const UMDComboComponent* Combo = UMDComboComponent::FindFor(Attacker);
	const EMDWeaponHand StepHand = Combo ? Combo->GetActiveStepHand() : EMDWeaponHand::Main;
	const bool bDualPair = Found[0] != nullptr && Found[2] != nullptr;
	const bool bSetDual = Combo && Combo->ComboSet && Combo->ComboSet->bDualWield;
	const TArray<FName> Sockets = TraceSocketsForHand(ResolveTraceHand(TraceHand, Combo != nullptr, StepHand, bDualPair, bSetDual));
	if (Found[0] && Sockets.Contains(MainHandSocket))
	{
		OutWeapons.Add(Found[0]);
	}
	if (Found[1] && Sockets.Contains(TwoHandedSocket))
	{
		OutWeapons.Add(Found[1]);
	}
	if (Found[2] && Sockets.Contains(OffHandSocket))
	{
		OutWeapons.Add(Found[2]);
	}
}

void UMDCombatStatics::ReleaseAbilityInput(AActor* Avatar, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Avatar);
	if (!ASC || !AbilityClass)
	{
		return;
	}
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->IsA(AbilityClass) || !Spec.IsActive())
		{
			continue;
		}
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			if (Instance && Instance->IsActive())
			{
				Instance->InputReleased(Spec.Handle, ASC->AbilityActorInfo.Get(), Instance->GetCurrentActivationInfo());
			}
		}
	}
}

EMDWeaponStance UMDCombatStatics::GetWeaponStanceForActor(const AActor* Actor)
{
	const UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(Actor);
	return Inv ? Inv->GetWeaponStance() : EMDWeaponStance::Unarmed;
}

bool UMDCombatStatics::ShouldDrawTraces()
{
	return MDCombatDebug::ShouldDrawTraces();
}

UMDLocalSettings* UMDCombatStatics::GetCombatOptions()
{
	return GetMutableDefault<UMDLocalSettings>();
}

void UMDCombatStatics::SaveCombatOptions()
{
	GetMutableDefault<UMDLocalSettings>()->SaveConfig();
}
