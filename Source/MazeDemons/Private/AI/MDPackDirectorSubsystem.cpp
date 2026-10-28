#include "AI/MDPackDirectorSubsystem.h"

#include "AI/MDAIQueries.h"
#include "AI/MDAISubsystem.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDRallyPoint.h"
#include "MDAbilitySystemComponent.h"
#include "MDInventoryComponent.h"
#include "MDMapZone.h"
#include "MDPerkComponent.h"
#include "MDGameplayStatics.h"
#include "MDPlayerState.h"
#include "MDPoiseComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

namespace MDPackDirectorLocal
{
	TAutoConsoleVariable<int32> CVarDebugSlots(TEXT("md.AI.DebugSlots"), 0,
		TEXT("1 = draw each pack's ring slots (green = taken, grey = free), the threat ring and the focus link (server)."), ECVF_Cheat);

	// Pack update rate. Slots follow the target at this rate; the focus and pack state have their own (slower) rules inside.
	constexpr float StepInterval = 0.2f;

	// Damage that counts as "full threat" (see FMDFocusInputs::Threat).
	constexpr float FullThreatDamage = 60.0f;

	// Seconds between rescans for rally points.
	constexpr double RallyRefreshInterval = 2.0;

	float YawOf(const FVector& From, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
	}

	float PlayerHealthFraction(const APawn* Pawn)
	{
		const AMDPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMDPlayerState>() : nullptr;
		UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
		if (ASC && ASC->GetMaxHealth() > 0.0f)
		{
			return FMath::Clamp(ASC->GetHealth() / ASC->GetMaxHealth(), 0.0f, 1.0f);
		}
		return 1.0f;
	}
}

UMDPackDirectorSubsystem* UMDPackDirectorSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMDPackDirectorSubsystem>() : nullptr;
}

bool UMDPackDirectorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld() || World->WorldType == EWorldType::Game);
}

bool UMDPackDirectorSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && !IsTemplate();
}

TStatId UMDPackDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDPackDirectorSubsystem, STATGROUP_Tickables);
}

double UMDPackDirectorSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UMDPackDirectorSubsystem::IsDebugSlotsEnabled()
{
	return MDPackDirectorLocal::CVarDebugSlots.GetValueOnGameThread() != 0;
}

void UMDPackDirectorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Start = FPlatformTime::Seconds();
	StepAccumulator += DeltaTime;
	if (StepAccumulator >= MDPackDirectorLocal::StepInterval)
	{
		StepAccumulator = 0.0f;
		StepPacks(Now());
	}
	Profile.Add(FPlatformTime::Seconds() - Start); // the 0.3 ms/frame director budget (2.9), averaged over frames
	if (IsDebugSlotsEnabled())
	{
		DrawSlotsDebug();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Packs
// ---------------------------------------------------------------------------------------------------------------------

FMDPack* UMDPackDirectorSubsystem::FindPackMutable(int32 PackId)
{
	return Packs.FindByPredicate([PackId](const FMDPack& P) { return P.PackId == PackId; });
}

FMDPack* UMDPackDirectorSubsystem::FindPackOfMutable(const AActor* Enemy)
{
	return Packs.FindByPredicate([Enemy](const FMDPack& P) { return P.FindMember(Enemy) != nullptr; });
}

const FMDPack* UMDPackDirectorSubsystem::FindPack(int32 PackId) const
{
	return Packs.FindByPredicate([PackId](const FMDPack& P) { return P.PackId == PackId; });
}

const FMDPack* UMDPackDirectorSubsystem::FindPackOf(const AActor* Enemy) const
{
	return Packs.FindByPredicate([Enemy](const FMDPack& P) { return P.FindMember(Enemy) != nullptr; });
}

int32 UMDPackDirectorSubsystem::GetPackIdOf(const AActor* Enemy) const
{
	const FMDPack* Pack = FindPackOf(Enemy);
	return Pack ? Pack->PackId : 0;
}

FMDPack& UMDPackDirectorSubsystem::CreatePack(int32 PackId, FName GroupKey, FName RoomRef)
{
	if (PackId <= 0)
	{
		PackId = NextPackId;
	}
	NextPackId = FMath::Max(NextPackId, PackId + 1);
	FMDPack& Pack = Packs.AddDefaulted_GetRef();
	Pack.PackId = PackId;
	Pack.GroupKey = GroupKey;
	Pack.RoomRef = RoomRef;
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(AI ? AI->GetRunSeed() : 0), GetTypeHash(PackId))));
	Pack.RingYawDeg = Stream.FRandRange(0.0f, 360.0f);
	return Pack;
}

void UMDPackDirectorSubsystem::AddToPack(FMDPack& Pack, AActor* Enemy)
{
	UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	FMDPackMember& Member = Pack.Members.AddDefaulted_GetRef();
	Member.Enemy = Enemy;
	Member.EnemyId = AI ? AI->GetActorId(Enemy) : 0;
	Pack.bRolesDirty = true;
}

int32 UMDPackDirectorSubsystem::RegisterEnemy(AActor* Enemy, int32 PackId)
{
	if (!Enemy)
	{
		return 0;
	}
	if (const FMDPack* Existing = FindPackOf(Enemy))
	{
		return Existing->PackId;
	}
	FMDPack* Pack = PackId > 0 ? FindPackMutable(PackId) : nullptr;
	if (!Pack)
	{
		Pack = &CreatePack(PackId, NAME_None, NAME_None);
	}
	AddToPack(*Pack, Enemy);
	++Pack->SpawnCount;
	if (!Pack->bTierSet)
	{
		ComputeTier(*Pack, Enemy);
	}
	return Pack->PackId;
}

int32 UMDPackDirectorSubsystem::RegisterSpawnGroup(const TArray<AActor*>& Enemies, int32 PackId, FName RoomRef)
{
	FMDPack* Pack = PackId > 0 ? FindPackMutable(PackId) : nullptr;
	if (!Pack)
	{
		Pack = &CreatePack(PackId, NAME_None, RoomRef);
	}
	if (!RoomRef.IsNone())
	{
		Pack->RoomRef = RoomRef;
	}
	const int32 Id = Pack->PackId;
	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}
		if (FMDPack* Other = FindPackOfMutable(Enemy))
		{
			if (Other->PackId == Id)
			{
				continue;
			}
			UnregisterEnemy(Enemy); // moves it into this group
			Pack = FindPackMutable(Id);
			if (!Pack)
			{
				Pack = &CreatePack(Id, NAME_None, RoomRef);
			}
		}
		AddToPack(*Pack, Enemy);
		++Pack->SpawnCount;
		if (!Pack->bTierSet)
		{
			ComputeTier(*Pack, Enemy);
		}
	}
	return Id;
}

int32 UMDPackDirectorSubsystem::GetOrCreateGroupPack(FName GroupKey, FName RoomRef)
{
	if (GroupKey.IsNone())
	{
		return 0;
	}
	if (const int32* Found = GroupPacks.Find(GroupKey))
	{
		if (FindPack(*Found))
		{
			return *Found;
		}
	}
	FMDPack& Pack = CreatePack(0, GroupKey, RoomRef);
	GroupPacks.Add(GroupKey, Pack.PackId);
	return Pack.PackId;
}

void UMDPackDirectorSubsystem::UnregisterEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}
	ReleaseToken(Enemy);
	FMDPack* Pack = FindPackOfMutable(Enemy);
	if (!Pack)
	{
		return;
	}
	Pack->Members.RemoveAll([Enemy](const FMDPackMember& M) { return M.Enemy.Get() == Enemy; });
	Pack->bRolesDirty = true;
	RemovePackIfEmpty(Pack->PackId);
}

void UMDPackDirectorSubsystem::RemovePackIfEmpty(int32 PackId)
{
	Packs.RemoveAll([PackId](const FMDPack& P) { return P.PackId == PackId && P.Members.Num() == 0; });
}

void UMDPackDirectorSubsystem::PruneMembers(FMDPack& Pack)
{
	const int32 Before = Pack.Members.Num();
	Pack.Members.RemoveAll([](const FMDPackMember& M)
	{
		const AMDEnemyCharacter* E = Cast<AMDEnemyCharacter>(M.Enemy.Get());
		return !M.Enemy.IsValid() || (E && E->IsEnemyDead());
	});
	if (Pack.Members.Num() != Before)
	{
		Pack.bRolesDirty = true;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Intelligence tier (V1 WP4)
// ---------------------------------------------------------------------------------------------------------------------

MDTier::FInputs UMDPackDirectorSubsystem::GatherTierInputs(const AActor* Sample, int32 PackId) const
{
	MDTier::FInputs In;
	In.PackId = PackId;
	if (const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld()))
	{
		In.RunSeed = AI->GetRunSeed();
		TArray<AActor*> Players;
		AI->CollectPlayerPawns(Players);
		In.NumPlayers = Players.Num();
		float PerkSum = 0.0f;
		float UpgradeSum = 0.0f;
		for (const AActor* Player : Players)
		{
			if (const UMDPerkComponent* Perks = UMDPerkComponent::FindFor(Player))
			{
				PerkSum += static_cast<float>(Perks->GetPointsSpent());
			}
			const APawn* Pawn = Cast<APawn>(Player);
			const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
			if (const UMDInventoryComponent* Inventory = PS ? PS->FindComponentByClass<UMDInventoryComponent>() : nullptr)
			{
				// Average upgrade level over the equipped items.
				float Levels = 0.0f;
				int32 Equipped = 0;
				for (const FMDEquippedSlot& Slot : Inventory->GetEquipped())
				{
					if (!Slot.ItemRowName.IsNone())
					{
						Levels += static_cast<float>(Inventory->GetSlotUpgradeLevel(Slot.Slot));
						++Equipped;
					}
				}
				UpgradeSum += Equipped > 0 ? Levels / static_cast<float>(Equipped) : 0.0f;
			}
		}
		if (Players.Num() > 0)
		{
			In.AvgPerksSpent = PerkSum / static_cast<float>(Players.Num());
			In.AvgUpgradeLevel = UpgradeSum / static_cast<float>(Players.Num());
		}
	}
	if (Sample)
	{
		if (const AMDMapZone* Zone = AMDMapZone::FindZoneAt(this, Sample->GetActorLocation()))
		{
			In.ZoneIndex = Zone->ZoneIndex;
		}
		if (const AMDEnemyCharacter* Enemy = Cast<AMDEnemyCharacter>(Sample))
		{
			if (Enemy->Archetype)
			{
				In.MinTier = Enemy->Archetype->MinTier;
				In.MaxTier = Enemy->Archetype->MaxTier;
			}
		}
	}
	return In;
}

void UMDPackDirectorSubsystem::ComputeTier(FMDPack& Pack, const AActor* Sample)
{
	const MDTier::FInputs In = GatherTierInputs(Sample, Pack.PackId);
	const MDTier::FResult Result = MDTier::Compute(In);
	Pack.bTierSet = true;
	Pack.Tier = Result.Tier;
	Pack.ITValue = Result.Value;
	Pack.PlayersAtSpawn = In.NumPlayers;
	Pack.AppliedTier = GetPackTier(Pack);
	Pack.bRolesDirty = true;
}

EMDIntelligenceTier UMDPackDirectorSubsystem::GetPackTier(const FMDPack& Pack) const
{
	EMDIntelligenceTier Forced;
	if (UMDAISubsystem::TryGetForcedTier(Forced))
	{
		return Forced;
	}
	return Pack.bTierSet ? Pack.Tier : EMDIntelligenceTier::T0;
}

EMDIntelligenceTier UMDPackDirectorSubsystem::GetTierOf(const AActor* Enemy) const
{
	if (const FMDPack* Pack = FindPackOf(Enemy))
	{
		return GetPackTier(*Pack);
	}
	return UMDAISubsystem::ResolveTier();
}

// ---------------------------------------------------------------------------------------------------------------------
// Reports and assignment
// ---------------------------------------------------------------------------------------------------------------------

void UMDPackDirectorSubsystem::ReportMember(AActor* Enemy, const FMDMemberReport& Report)
{
	if (FMDPack* Pack = FindPackOfMutable(Enemy))
	{
		if (FMDPackMember* Member = Pack->FindMember(Enemy))
		{
			Member->Report = Report;
		}
	}
}

void UMDPackDirectorSubsystem::ReportDamage(AActor* Enemy, AActor* Instigator, float Amount)
{
	if (!Instigator || Amount <= 0.0f)
	{
		return;
	}
	if (FMDPack* Pack = FindPackOfMutable(Enemy))
	{
		FMDPackDamageRecord& R = Pack->Damage.AddDefaulted_GetRef();
		R.Instigator = Instigator;
		R.Amount = Amount;
		R.Time = Now();
	}
}

int32 UMDPackDirectorSubsystem::ShareSighting(AActor* Seer, AActor* Player, const FVector& Location, double SightTime, float ShareDelay)
{
	if (!Seer || !Player || ShareDelay < 0.0f)
	{
		return 0;
	}
	const FMDPack* Pack = FindPackOf(Seer);
	if (!Pack)
	{
		return 0;
	}
	int32 Told = 0;
	for (const FMDPackMember& M : Pack->Members)
	{
		AActor* Mate = M.Enemy.Get();
		if (!Mate || Mate == Seer)
		{
			continue;
		}
		if (UMDEnemyBrainComponent* Brain = UMDEnemyBrainComponent::FindFor(Mate))
		{
			Brain->ReceiveSharedSighting(Player, Location, SightTime, ShareDelay);
			++Told;
		}
	}
	return Told;
}

bool UMDPackDirectorSubsystem::GetAssignment(const AActor* Enemy, FMDEnemyAssignment& Out) const
{
	const FMDPack* Pack = FindPackOf(Enemy);
	const FMDPackMember* Member = Pack ? Pack->FindMember(Enemy) : nullptr;
	if (!Member)
	{
		return false;
	}
	Out = FMDEnemyAssignment();
	Out.PackId = Pack->PackId;
	Out.Role = Member->Role;
	Out.PackState = Pack->State;
	Out.Tier = GetPackTier(*Pack);
	Out.ITValue = Pack->ITValue;
	Out.FocusTarget = Pack->FocusTarget;
	Out.Slot = Member->Slot;
	Out.bHasRally = Pack->bHasRally && Pack->State == EMDPackState::Regroup;
	Out.RallyPoint = Pack->RallyPoint;
	const AActor* Focus = Pack->FocusTarget.Get();
	if (Focus && Member->Slot >= 0)
	{
		const FVector Center = Focus->GetActorLocation();
		Out.bHasSlotPoint = true;
		Out.SlotPoint = MDPackRules::SlotLocation(Center, Member->Slot, Pack->RingYawDeg, Pack->SlotRadius);
		Out.ThreatPoint = MDPackRules::SlotLocation(Center, Member->Slot, Pack->RingYawDeg, Pack->ThreatRadius);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------------------------------------------------

bool UMDPackDirectorSubsystem::RequestToken(AActor* Enemy, AActor* Target)
{
	UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	if (!Enemy || !Target || !AI)
	{
		return false;
	}
	EMDRole Role = EMDRole::None;
	int32 MaxTokens = UMDAISubsystem::ResolveTierParams().MaxTokensPerPlayer;
	if (const FMDPack* Pack = FindPackOf(Enemy))
	{
		MaxTokens = FMDTierParams::ForTier(GetPackTier(*Pack)).MaxTokensPerPlayer; // the requester's own pack tier
		if (Pack->State == EMDPackState::Regroup)
		{
			return false; // a retreating pack does not attack
		}
		if (const FMDPackMember* Member = Pack->FindMember(Enemy))
		{
			Role = Member->Role;
		}
	}
	const int32 EnemyId = AI->GetActorId(Enemy);
	const float Dist = static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation()));
	const float Priority = MDPackRules::TokenPriority(Role, Dist, AI->GetRunSeed(), EnemyId);
	return Tokens.TryGrant(EnemyId, AI->GetActorId(Target), Now(), MaxTokens, Priority).bGranted;
}

bool UMDPackDirectorSubsystem::HoldsToken(const AActor* Enemy) const
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	const int32 Id = AI ? AI->FindActorId(Enemy) : 0;
	return Id != 0 && Tokens.Holds(Id, Now());
}

void UMDPackDirectorSubsystem::ReleaseToken(AActor* Enemy)
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	if (const int32 Id = AI ? AI->FindActorId(Enemy) : 0)
	{
		Tokens.Release(Id);
	}
}

void UMDPackDirectorSubsystem::ReleaseTokenAfter(AActor* Enemy, double Delay)
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	if (const int32 Id = AI ? AI->FindActorId(Enemy) : 0)
	{
		Tokens.ReleaseAfter(Id, Now(), Delay);
	}
}

void UMDPackDirectorSubsystem::CommitToken(AActor* Enemy)
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	if (const int32 Id = AI ? AI->FindActorId(Enemy) : 0)
	{
		Tokens.Commit(Id);
	}
}

int32 UMDPackDirectorSubsystem::CountTokensOn(const AActor* Target) const
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	const int32 Id = AI ? AI->FindActorId(Target) : 0;
	return Id != 0 ? Tokens.CountForTarget(Id, Now()) : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------------------------------------------------

void UMDPackDirectorSubsystem::StepPacks(double InNow)
{
	Tokens.Purge(InNow);

	for (FMDPack& Pack : Packs)
	{
		PruneMembers(Pack);
	}
	Packs.RemoveAll([](const FMDPack& P) { return P.Members.Num() == 0; });

	MergePacks();

	for (FMDPack& Pack : Packs)
	{
		const EMDIntelligenceTier Tier = GetPackTier(Pack);
		if (Tier != Pack.AppliedTier)
		{
			Pack.AppliedTier = Tier; // md.AI.ForceTier changed: assign the roles again
			Pack.bRolesDirty = true;
		}
		Pack.Damage.RemoveAll([InNow](const FMDPackDamageRecord& R) { return InNow - R.Time > MDPack::ThreatWindow || !R.Instigator.IsValid(); });
		UpdateRoles(Pack, Tier);
		UpdateFocus(Pack, Tier, InNow);
		UpdatePackState(Pack, Tier, InNow);
		UpdateSlots(Pack);
	}
}

void UMDPackDirectorSubsystem::CollectCandidates(const FMDPack& Pack, TArray<AActor*>& Out) const
{
	Out.Reset();
	for (const FMDPackMember& M : Pack.Members)
	{
		for (const TWeakObjectPtr<AActor>& K : M.Report.Known)
		{
			AActor* Actor = K.Get();
			if (!Actor || Out.Contains(Actor))
			{
				continue;
			}
			const APawn* Pawn = Cast<APawn>(Actor);
			if (Pawn && UMDGameplayStatics::IsPawnDead(Pawn))
			{
				continue;
			}
			Out.Add(Actor);
		}
	}
}

void UMDPackDirectorSubsystem::MergePacks()
{
	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (int32 i = 0; i < Packs.Num() && !bMerged; ++i)
		{
			const EMDIntelligenceTier Tier = GetPackTier(Packs[i]); // the older pack decides whether it takes others in
			if (static_cast<int32>(Tier) < static_cast<int32>(EMDIntelligenceTier::T2))
			{
				continue;
			}
			TArray<AActor*> KnownA;
			CollectCandidates(Packs[i], KnownA);
			if (KnownA.Num() == 0)
			{
				continue;
			}
			for (int32 j = i + 1; j < Packs.Num() && !bMerged; ++j)
			{
				TArray<AActor*> KnownB;
				CollectCandidates(Packs[j], KnownB);
				bool bShared = false;
				for (AActor* A : KnownA)
				{
					if (KnownB.Contains(A))
					{
						bShared = true;
						break;
					}
				}
				float MinDist = TNumericLimits<float>::Max();
				if (bShared)
				{
					for (const FMDPackMember& MA : Packs[i].Members)
					{
						for (const FMDPackMember& MB : Packs[j].Members)
						{
							if (MA.Enemy.IsValid() && MB.Enemy.IsValid())
							{
								MinDist = FMath::Min(MinDist, static_cast<float>(FVector::Dist(MA.Enemy->GetActorLocation(), MB.Enemy->GetActorLocation())));
							}
						}
					}
				}
				if (!MDPackRules::ShouldMerge(Tier, MinDist, bShared))
				{
					continue;
				}
				// Merge j into i (the older pack keeps its id, ring and state unless the other is retreating).
				FMDPack& Into = Packs[i];
				FMDPack& From = Packs[j];
				Into.Members.Append(From.Members);
				Into.SpawnCount += From.SpawnCount;
				Into.Damage.Append(From.Damage);
				if (Into.RoomRef.IsNone())
				{
					Into.RoomRef = From.RoomRef;
				}
				Into.bRolesDirty = true;
				const int32 FromId = From.PackId;
				const int32 IntoId = Into.PackId;
				Packs.RemoveAt(j);
				for (TPair<FName, int32>& Pair : GroupPacks)
				{
					if (Pair.Value == FromId)
					{
						Pair.Value = IntoId;
					}
				}
				bMerged = true;
			}
		}
	}
}

void UMDPackDirectorSubsystem::UpdateRoles(FMDPack& Pack, EMDIntelligenceTier Tier)
{
	if (!Pack.bRolesDirty)
	{
		return;
	}
	Pack.bRolesDirty = false;
	TArray<FMDRoleCandidate> Candidates;
	for (const FMDPackMember& M : Pack.Members)
	{
		FMDRoleCandidate& C = Candidates.AddDefaulted_GetRef();
		C.Id = M.EnemyId;
		C.AllowedMask = M.Report.AllowedMask;
	}
	TArray<EMDRole> Roles;
	MDPackRules::AssignRoles(Candidates, Tier, Roles);
	for (int32 i = 0; i < Pack.Members.Num(); ++i)
	{
		Pack.Members[i].Role = Roles[i];
	}
}

void UMDPackDirectorSubsystem::UpdateFocus(FMDPack& Pack, EMDIntelligenceTier Tier, double InNow)
{
	TArray<AActor*> Candidates;
	CollectCandidates(Pack, Candidates);
	if (Candidates.Num() == 0)
	{
		Pack.FocusTarget.Reset();
		return;
	}
	AActor* Current = Pack.FocusTarget.Get();
	const bool bCurrentValid = Current && Candidates.Contains(Current);
	if (!MDPackRules::CanRetarget(Tier, InNow - Pack.LastRetargetTime, bCurrentValid))
	{
		return;
	}

	// Every player in the world counts as an ally for isolation, not only the ones this pack knows.
	TArray<AActor*> AllPlayers = Candidates;
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APawn* Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr)
			{
				AllPlayers.AddUnique(Pawn);
			}
		}
	}

	TArray<float> Scores;
	int32 CurrentIndex = INDEX_NONE;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		AActor* Player = Candidates[i];
		if (Player == Current)
		{
			CurrentIndex = i;
		}
		float NearestAlly = -1.0f;
		for (const AActor* Other : AllPlayers)
		{
			if (Other != Player)
			{
				const float D = static_cast<float>(FVector::Dist(Player->GetActorLocation(), Other->GetActorLocation()));
				NearestAlly = NearestAlly < 0.0f ? D : FMath::Min(NearestAlly, D);
			}
		}
		const APawn* Pawn = Cast<APawn>(Player);
		const UMDPoiseComponent* Poise = Player->FindComponentByClass<UMDPoiseComponent>();

		float Damage = 0.0f;
		for (const FMDPackDamageRecord& R : Pack.Damage)
		{
			if (R.Instigator.Get() == Player)
			{
				Damage += R.Amount;
			}
		}
		int32 Targeting = 0;
		for (const FMDPackMember& M : Pack.Members)
		{
			Targeting += M.Report.Target.Get() == Player ? 1 : 0;
		}

		FMDFocusInputs In;
		In.Isolation = MDPackRules::NormalizeIsolation(NearestAlly);
		In.HealthFraction = MDPackDirectorLocal::PlayerHealthFraction(Pawn);
		In.bStaggered = Poise && Poise->IsStunned();
		In.Threat = FMath::Clamp(Damage / MDPackDirectorLocal::FullThreatDamage, 0.0f, 1.0f);
		In.Crowding = Pack.Members.Num() > 0 ? static_cast<float>(Targeting) / static_cast<float>(Pack.Members.Num()) : 0.0f;
		Scores.Add(MDPackRules::FocusScore(In));
	}

	const int32 Pick = MDPackRules::PickFocus(Scores, bCurrentValid ? CurrentIndex : INDEX_NONE);
	if (Pick != INDEX_NONE)
	{
		Pack.FocusTarget = Candidates[Pick];
		Pack.FocusScore = Scores[Pick];
		Pack.LastRetargetTime = InNow;
	}
}

void UMDPackDirectorSubsystem::UpdatePackState(FMDPack& Pack, EMDIntelligenceTier Tier, double InNow)
{
	const UMDAISubsystem* AI = UMDAISubsystem::Get(GetWorld());
	const bool bAnyRally = HasAnyRallyPoint(InNow);
	const int32 Alive = Pack.NumAlive();
	const float Fraction = MDPackRules::RetreatFraction(Tier);
	const bool bBelow = Fraction >= 0.0f && static_cast<float>(Alive) <= static_cast<float>(Pack.SpawnCount) * Fraction + KINDA_SMALL_NUMBER;
	if (!bBelow)
	{
		Pack.bRetreatDone = false; // recovered (reinforcements, a merge): the pack may retreat again at the next low point
	}

	const int32 MaxTokens = FMDTierParams::ForTier(Tier).MaxTokensPerPlayer;
	int32 TokensOnFocus = 0;
	if (const AActor* Focus = Pack.FocusTarget.Get())
	{
		const int32 FocusId = AI ? AI->FindActorId(Focus) : 0;
		TokensOnFocus = FocusId != 0 ? Tokens.CountForTarget(FocusId, InNow) : 0;
	}

	const EMDPackState Previous = Pack.State;
	EMDPackState Next = MDPackRules::ResolvePackState(Previous, Tier, Alive, Pack.SpawnCount, bAnyRally, Pack.bRetreatDone,
		Previous == EMDPackState::Regroup && InNow >= Pack.RegroupEndTime, TokensOnFocus, MaxTokens);

	if (Next == EMDPackState::Regroup && Previous != EMDPackState::Regroup)
	{
		// The retreat starts: pick the rally point nearest to the pack, hand every token back.
		FVector Centre = FVector::ZeroVector;
		for (const FMDPackMember& M : Pack.Members)
		{
			Centre += M.Enemy.IsValid() ? M.Enemy->GetActorLocation() : FVector::ZeroVector;
		}
		Centre /= static_cast<double>(FMath::Max(Pack.Members.Num(), 1));
		FVector Point;
		if (FindRallyPoint(Centre, Point))
		{
			Pack.RallyPoint = Point;
			Pack.bHasRally = true;
			Pack.RegroupEndTime = InNow + MDPack::RegroupTime;
			Pack.bRetreatDone = true;
			for (const FMDPackMember& M : Pack.Members)
			{
				ReleaseToken(M.Enemy.Get());
			}
		}
		else
		{
			Next = EMDPackState::Routed;
		}
	}
	Pack.State = Next;
	if (Next != EMDPackState::Regroup && Previous == EMDPackState::Regroup)
	{
		Pack.bHasRally = false; // back from the rally point
	}
}

void UMDPackDirectorSubsystem::UpdateSlots(FMDPack& Pack)
{
	const AActor* Focus = Pack.FocusTarget.Get();
	for (FMDPackMember& M : Pack.Members)
	{
		M.Slot = -1;
	}
	if (!Focus)
	{
		return;
	}
	const FVector Centre = Focus->GetActorLocation();

	TArray<int32> Indices;
	TArray<FMDSlotMember> Request;
	float RadiusSum = 0.0f;
	float ThreatSum = 0.0f;
	for (int32 i = 0; i < Pack.Members.Num(); ++i)
	{
		const FMDPackMember& M = Pack.Members[i];
		if (!M.Enemy.IsValid() || M.Report.Target.Get() != Focus)
		{
			continue;
		}
		Indices.Add(i);
		FMDSlotMember& S = Request.AddDefaulted_GetRef();
		S.Id = M.EnemyId;
		S.Role = M.Role;
		S.AngleDeg = MDPackDirectorLocal::YawOf(Centre, M.Enemy->GetActorLocation());
		S.CurrentSlot = M.Slot;
		RadiusSum += MDPackRules::SlotRadiusFor(M.Report.PreferredRange, M.Report.AttackRange);
		ThreatSum += MDPackRules::ThreatRadiusFor(M.Report.ThreatRange);
	}
	if (Request.Num() == 0)
	{
		return;
	}
	Pack.SlotRadius = RadiusSum / static_cast<float>(Request.Num());
	Pack.ThreatRadius = ThreatSum / static_cast<float>(Request.Num());

	TArray<int32> Slots;
	MDPackRules::AssignSlots(Request, Pack.RingYawDeg, Focus->GetActorRotation().Yaw, Slots);
	for (int32 k = 0; k < Indices.Num(); ++k)
	{
		Pack.Members[Indices[k]].Slot = Slots[k];
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Rally points
// ---------------------------------------------------------------------------------------------------------------------

bool UMDPackDirectorSubsystem::HasAnyRallyPoint(double InNow)
{
	// V1 WP3: the rally actors (cached by the AI subsystem) filtered through the room-reveal gate, with the room-entry fallback.
	FVector Unused;
	return FindRallyPoint(FVector::ZeroVector, Unused);
}

bool UMDPackDirectorSubsystem::FindRallyPoint(const FVector& Near, FVector& OutPoint) const
{
	return MDAIQueries::FindRallyPointFor(GetWorld(), Near, OutPoint);
}

// ---------------------------------------------------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------------------------------------------------

void UMDPackDirectorSubsystem::DrawSlotsDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Life = MDPackDirectorLocal::StepInterval + 0.05f;
	for (const FMDPack& Pack : Packs)
	{
		const AActor* Focus = Pack.FocusTarget.Get();
		if (!Focus)
		{
			continue;
		}
		const FVector Centre = Focus->GetActorLocation();
		const FVector Ground = Centre - FVector(0.0f, 0.0f, 80.0f);
		DrawDebugCircle(World, Ground, Pack.SlotRadius, 32, FColor(90, 90, 90), false, Life, 0, 1.5f, FVector::RightVector, FVector::ForwardVector, false);
		DrawDebugCircle(World, Ground, Pack.ThreatRadius, 48, FColor(70, 70, 120), false, Life, 0, 1.0f, FVector::RightVector, FVector::ForwardVector, false);
		for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
		{
			const FMDPackMember* Taker = Pack.Members.FindByPredicate([Slot](const FMDPackMember& M) { return M.Slot == Slot; });
			const FVector P = MDPackRules::SlotLocation(Ground, Slot, Pack.RingYawDeg, Pack.SlotRadius);
			DrawDebugSphere(World, P, 22.0f, 8, Taker ? FColor::Green : FColor(120, 120, 120), false, Life, 0, 1.5f);
			DrawDebugString(World, P + FVector(0.0f, 0.0f, 40.0f), FString::Printf(TEXT("%d"), Slot), nullptr, Taker ? FColor::Green : FColor(150, 150, 150), Life, false, 1.0f);
			if (const AActor* E = Taker ? Taker->Enemy.Get() : nullptr)
			{
				DrawDebugLine(World, E->GetActorLocation(), P, FColor::Green, false, Life, 0, 1.0f);
			}
		}
		DrawDebugString(World, Centre + FVector(0.0f, 0.0f, 140.0f),
			FString::Printf(TEXT("pack %d  %s  alive %d/%d  focus %s"), Pack.PackId, *UEnum::GetValueAsString(Pack.State), Pack.NumAlive(), Pack.SpawnCount, *Focus->GetName()),
			nullptr, FColor::Cyan, Life, false, 1.0f);
		if (Pack.bHasRally)
		{
			DrawDebugSphere(World, Pack.RallyPoint, 60.0f, 12, FColor::Yellow, false, Life, 0, 2.0f);
		}
	}
}
