#include "AI/MDAISubsystem.h"

#include "AI/MDAIQueries.h"
#include "AI/MDEnemyBrainComponent.h"
#include "AI/MDRallyPoint.h"
#include "MDAbilitySystemComponent.h"
#include "MDFogComponent.h"
#include "MDGameplayStatics.h"
#include "MDGameplayTags.h"
#include "MDPlayerState.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "DrawDebugHelpers.h"
#include "AI/MDEnemyCharacter.h"
#include "AI/MDPackDirectorSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

namespace MDAISubsystemLocal
{
	TAutoConsoleVariable<int32> CVarDebug(TEXT("md.AI.Debug"), 0,
		TEXT("1 = draw each enemy's intent, top 3 scores and token state above it (server)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarForceTier(TEXT("md.AI.ForceTier"), -1,
		TEXT("-1 = off (every pack runs at the tier the pack director computed for it at spawn). 0..4 = every enemy brain runs at that intelligence tier."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarBrainHz(TEXT("md.AI.BrainHz"), 8.0f,
		TEXT("Decisions per second per enemy brain (5-10)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarSeed(TEXT("md.AI.Seed"), 0,
		TEXT("Run seed for the enemy brains' noise. 0 = drawn once per world (logged)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarDebugPerception(TEXT("md.AI.DebugPerception"), 0,
		TEXT("1 = draw each enemy's sight cone (green awake, red seeing a player, grey dormant), hearing range, the last-known positions it remembers and the noises it heard (server)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarDormancy(TEXT("md.AI.Dormancy"), 1,
		TEXT("1 (default) = enemies more than 40 m from every player stop sensing and thinking until a player comes in range or they are hurt. 0 = never dormant."), ECVF_Cheat);

	// Seconds between the dormancy / sprint-noise passes.
	constexpr float SlowInterval = 0.5f;
	// Seconds between perception debug redraws.
	constexpr float DebugInterval = 0.2f;
	// A player is "in melee" when an enemy stands this close (cm).
	constexpr double MeleeReach = 350.0;
}

UMDAISubsystem* UMDAISubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UMDAISubsystem>() : nullptr;
}

bool UMDAISubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld() || World->WorldType == EWorldType::Game);
}

void UMDAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RunSeed = MDAISubsystemLocal::CVarSeed.GetValueOnGameThread();
	if (RunSeed == 0)
	{
		RunSeed = static_cast<int32>(FPlatformTime::Cycles() & 0x7fffffff);
		if (RunSeed == 0)
		{
			RunSeed = 1;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("MDAISubsystem: run seed %d (md.AI.Seed to fix it)"), RunSeed);

	// DT_IntelligenceTiers (FMDIntelligenceTier rows T0..T4) is the tier source; a missing asset keeps the code defaults.
	TierTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/AI/DT_IntelligenceTiers.DT_IntelligenceTiers"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (TierTable && TierTable->GetRowStruct() == FMDIntelligenceTier::StaticStruct())
	{
		FMDTierParams::SetTierTable(TierTable);
	}
	else
	{
		TierTable = nullptr;
		FMDTierParams::SetTierTable(nullptr);
	}
}

bool UMDAISubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && !IsTemplate();
}

TStatId UMDAISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDAISubsystem, STATGROUP_Tickables);
}

double UMDAISubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UMDAISubsystem::TryGetForcedTier(EMDIntelligenceTier& OutTier)
{
	const int32 Forced = MDAISubsystemLocal::CVarForceTier.GetValueOnGameThread();
	if (Forced >= 0)
	{
		OutTier = static_cast<EMDIntelligenceTier>(FMath::Clamp(Forced, 0, static_cast<int32>(EMDIntelligenceTier::T4)));
		return true;
	}
	return false;
}

EMDIntelligenceTier UMDAISubsystem::ResolveTier()
{
	EMDIntelligenceTier Forced = EMDIntelligenceTier::T0;
	TryGetForcedTier(Forced);
	return Forced;
}

bool UMDAISubsystem::IsDebugEnabled()
{
	return MDAISubsystemLocal::CVarDebug.GetValueOnGameThread() != 0;
}

float UMDAISubsystem::GetBrainHz()
{
	return FMath::Clamp(MDAISubsystemLocal::CVarBrainHz.GetValueOnGameThread(), 1.0f, 30.0f);
}

// ---- Brains ----

int32 UMDAISubsystem::RegisterBrain(UMDEnemyBrainComponent* Brain)
{
	if (!Brain)
	{
		return 0;
	}
	Brains.AddUnique(Brain);
	return NextEnemyId++;
}

void UMDAISubsystem::UnregisterBrain(UMDEnemyBrainComponent* Brain)
{
	const int32 Index = Brains.IndexOfByPredicate([Brain](const TWeakObjectPtr<UMDEnemyBrainComponent>& B) { return B.Get() == Brain; });
	if (Index != INDEX_NONE)
	{
		Brains.RemoveAt(Index);
		if (Cursor > Index)
		{
			--Cursor;
		}
	}
	if (AActor* Enemy = Brain ? static_cast<AActor*>(Brain->GetEnemy()) : nullptr)
	{
		ReleaseAttackToken(Enemy); // tokens are keyed on the pawn, not the controller the brain lives on
	}
}

int32 UMDAISubsystem::StepScheduler(float DeltaTime)
{
	// Drop dead entries first so the batch covers live brains only.
	Brains.RemoveAll([](const TWeakObjectPtr<UMDEnemyBrainComponent>& B) { return !B.IsValid(); });
	if (Brains.Num() == 0)
	{
		Cursor = 0;
		Accumulator = 0.0f;
		return 0;
	}
	// Dormant brains are skipped (2.9): the batch is sized on the awake ones so they keep their BrainHz.
	int32 NumAwake = 0;
	for (const TWeakObjectPtr<UMDEnemyBrainComponent>& B : Brains)
	{
		NumAwake += (B.IsValid() && !B->IsDormant()) ? 1 : 0;
	}
	const int32 Batch = MDBrainScheduling::BatchSize(NumAwake, GetBrainHz(), DeltaTime, Accumulator);
	const double Time = Now();
	int32 Ran = 0;
	const int32 ScanLimit = Brains.Num() + Batch;
	for (int32 Scanned = 0; Ran < Batch && Scanned < ScanLimit; ++Scanned)
	{
		if (Cursor >= Brains.Num())
		{
			Cursor = 0;
		}
		UMDEnemyBrainComponent* Brain = Brains[Cursor].Get();
		++Cursor;
		if (Brain && !Brain->IsDormant())
		{
			Brain->RunDecision(Time);
			++Ran;
		}
	}
	return Ran;
}

void UMDAISubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Start = FPlatformTime::Seconds();
	StepScheduler(DeltaTime);

	SlowAccumulator += DeltaTime;
	if (SlowAccumulator >= MDAISubsystemLocal::SlowInterval)
	{
		SlowAccumulator = 0.0f;
		UpdateDormancy(Now());
		ReportSprintNoise();
	}
	Profile.Add(FPlatformTime::Seconds() - Start); // the brain budget (2.9): scheduler + dormancy + noise, averaged over frames

	if (IsDebugPerceptionEnabled())
	{
		DebugAccumulator += DeltaTime;
		if (DebugAccumulator >= MDAISubsystemLocal::DebugInterval)
		{
			DebugAccumulator = 0.0f;
			for (const TWeakObjectPtr<UMDEnemyBrainComponent>& B : Brains)
			{
				if (const UMDEnemyBrainComponent* Brain = B.Get())
				{
					Brain->DrawPerceptionDebug(MDAISubsystemLocal::DebugInterval + 0.05f);
				}
			}
		}
	}
}

bool UMDAISubsystem::IsDebugPerceptionEnabled()
{
	return MDAISubsystemLocal::CVarDebugPerception.GetValueOnGameThread() != 0;
}

bool UMDAISubsystem::IsDormancyEnabled()
{
	return MDAISubsystemLocal::CVarDormancy.GetValueOnGameThread() != 0;
}

// ---- Players ----

void UMDAISubsystem::TrackExtraPlayer(APawn* Pawn)
{
	if (Pawn)
	{
		ExtraPlayers.AddUnique(Pawn);
	}
}

void UMDAISubsystem::CollectPlayerPawns(TArray<AActor*>& Out) const
{
	Out.Reset();
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr;
			if (Pawn && !UMDGameplayStatics::IsPawnDead(Pawn))
			{
				Out.AddUnique(Pawn);
			}
		}
	}
	for (const TWeakObjectPtr<APawn>& Extra : ExtraPlayers)
	{
		if (APawn* Pawn = Extra.Get())
		{
			if (!UMDGameplayStatics::IsPawnDead(Pawn))
			{
				Out.AddUnique(Pawn);
			}
		}
	}
}

void UMDAISubsystem::CollectPlayerLocations(TArray<FVector>& Out) const
{
	TArray<AActor*> Players;
	CollectPlayerPawns(Players);
	Out.Reset();
	for (const AActor* P : Players)
	{
		Out.Add(P->GetActorLocation());
	}
}

void UMDAISubsystem::CollectMeleePlayerLocations(TArray<FVector>& Out) const
{
	TArray<AActor*> Players;
	CollectPlayerPawns(Players);
	Out.Reset();
	for (const AActor* P : Players)
	{
		for (const TWeakObjectPtr<UMDEnemyBrainComponent>& B : Brains)
		{
			const UMDEnemyBrainComponent* Brain = B.Get();
			const AMDEnemyCharacter* E = Brain ? Brain->GetEnemy() : nullptr;
			if (E && !Brain->IsDormant() && FVector::Dist2D(E->GetActorLocation(), P->GetActorLocation()) <= MDAISubsystemLocal::MeleeReach)
			{
				Out.Add(P->GetActorLocation());
				break;
			}
		}
	}
}

void UMDAISubsystem::ReportSprintNoise()
{
	// A sprinting player is heard (tag "Sprint", quiet). Polled here rather than hooked in the stamina code: the loose
	// State.Sprinting tag is already the server's truth for it.
	TArray<AActor*> Players;
	CollectPlayerPawns(Players);
	for (AActor* Actor : Players)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		const AMDPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMDPlayerState>() : nullptr;
		UMDAbilitySystemComponent* ASC = PS ? PS->GetMDAbilitySystemComponent() : nullptr;
		if (ASC && ASC->HasMatchingGameplayTag(TAG_State_Sprinting.GetTag()))
		{
			MDNoise::Report(this, Actor->GetActorLocation(), Actor, FName(MDNoise::Sprint));
		}
	}
}

// ---- Dormancy ----

int32 UMDAISubsystem::UpdateDormancy(double InNow)
{
	TArray<FVector> Players;
	CollectPlayerLocations(Players);
	const bool bEnabled = IsDormancyEnabled();
	int32 Dormant = 0;
	for (const TWeakObjectPtr<UMDEnemyBrainComponent>& B : Brains)
	{
		UMDEnemyBrainComponent* Brain = B.Get();
		const AMDEnemyCharacter* Enemy = Brain ? Brain->GetEnemy() : nullptr;
		if (!Brain || !Enemy)
		{
			continue;
		}
		float Nearest = -1.0f; // < 0 = no player known: never dormant
		if (bEnabled)
		{
			for (const FVector& P : Players)
			{
				const float D = static_cast<float>(FVector::Dist(P, Enemy->GetActorLocation()));
				Nearest = Nearest < 0.0f ? D : FMath::Min(Nearest, D);
			}
		}
		const double LastDamage = Brain->GetLastDamageTime();
		const double SinceDamage = LastDamage < 0.0 ? -1.0 : InNow - LastDamage;
		// V1 WP5: an ambusher waiting for its trigger stays dormant whatever the distance.
		Brain->SetDormant(Enemy->IsAmbushHidden() || MDDormancy::ShouldSleep(Nearest, SinceDamage), InNow);
		Brain->ApplyNetFrequency(MDDormancy::NetUpdateFrequencyFor(Nearest, Brain->GetDefaultNetFrequency()));
		Dormant += Brain->IsDormant() ? 1 : 0;
	}
	return Dormant;
}

int32 UMDAISubsystem::NumDormant() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UMDEnemyBrainComponent>& B : Brains)
	{
		N += (B.IsValid() && B->IsDormant()) ? 1 : 0;
	}
	return N;
}

// ---- Room reveal ----

void UMDAISubsystem::SetRevealGridOverride(const FMDFogGrid* Grid)
{
	if (Grid)
	{
		FogOverride = *Grid;
	}
	else
	{
		FogOverride.Reset();
	}
}

void UMDAISubsystem::RefreshFogCache(double InNow) const
{
	if (InNow < NextFogRefresh)
	{
		return;
	}
	NextFogRefresh = InNow + 0.5;
	const UMDFogComponent* Fog = UMDFogComponent::FindFor(GetWorld());
	bFogCacheValid = Fog && Fog->BuildGrid(FogCache) && FogCache.IsValid();
}

bool UMDAISubsystem::IsRoomRevealed(const FVector& Location) const
{
	if (FogOverride.IsSet())
	{
		return MDAIQueries::IsPointRevealed(FogOverride.GetValue(), Location);
	}
	RefreshFogCache(Now());
	return !bFogCacheValid || MDAIQueries::IsPointRevealed(FogCache, Location);
}

// ---- Rally points ----

void UMDAISubsystem::RefreshRallyCache(double InNow) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Rescan when the world's actor count changed (a rally point was spawned or destroyed) or every 5 s (streaming).
	const int32 Count = World->GetActorCount();
	if (Count == CachedActorCount && InNow < NextRallyRefresh)
	{
		return;
	}
	CachedActorCount = Count;
	NextRallyRefresh = InNow + 5.0;
	RallyActors.Reset();
	EntryActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(AMDRallyPoint::RallyTag))
		{
			RallyActors.Add(*It);
		}
		else if (It->ActorHasTag(AMDRallyPoint::RoomEntryTag))
		{
			EntryActors.Add(*It);
		}
	}
}

void UMDAISubsystem::CollectRallyPoints(TArray<FVector>& OutRallyPoints, TArray<FVector>& OutRoomEntries) const
{
	RefreshRallyCache(Now());
	OutRallyPoints.Reset();
	OutRoomEntries.Reset();
	for (const TWeakObjectPtr<AActor>& A : RallyActors)
	{
		if (const AActor* Actor = A.Get())
		{
			OutRallyPoints.Add(Actor->GetActorLocation());
		}
	}
	for (const TWeakObjectPtr<AActor>& A : EntryActors)
	{
		if (const AActor* Actor = A.Get())
		{
			OutRoomEntries.Add(Actor->GetActorLocation());
		}
	}
}

// ---- Navigation ----

bool UMDAISubsystem::TryConsumePathRequest()
{
	return PathBudget.TryConsume(GFrameCounter);
}

// ---- Tokens ----

int32 UMDAISubsystem::GetActorId(const AActor* Actor)
{
	if (!Actor)
	{
		return 0;
	}
	if (const int32* Found = ActorIds.Find(Actor))
	{
		return *Found;
	}
	// Forget stale actors now and then so the map does not grow with every spawn.
	if (ActorIds.Num() > 256)
	{
		for (auto It = ActorIds.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	const int32 Id = NextActorId++;
	ActorIds.Add(Actor, Id);
	return Id;
}

bool UMDAISubsystem::RequestAttackToken(AActor* Enemy, AActor* Target)
{
	UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	return Director && Director->RequestToken(Enemy, Target);
}

bool UMDAISubsystem::HoldsAttackToken(const AActor* Enemy) const
{
	const UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	return Director && Director->HoldsToken(Enemy);
}

void UMDAISubsystem::ReleaseAttackToken(AActor* Enemy)
{
	if (UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld()))
	{
		Director->ReleaseToken(Enemy);
	}
}

void UMDAISubsystem::ReleaseAttackTokenAfter(AActor* Enemy, double Delay)
{
	if (UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld()))
	{
		Director->ReleaseTokenAfter(Enemy, Delay);
	}
}

void UMDAISubsystem::CommitAttackToken(AActor* Enemy)
{
	if (UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld()))
	{
		Director->CommitToken(Enemy);
	}
}

int32 UMDAISubsystem::CountAttackTokensOn(const AActor* Target) const
{
	const UMDPackDirectorSubsystem* Director = UMDPackDirectorSubsystem::Get(GetWorld());
	return Director ? Director->CountTokensOn(Target) : 0;
}

int32 UMDAISubsystem::FindActorId(const AActor* Actor) const
{
	const int32* Found = Actor ? ActorIds.Find(Actor) : nullptr;
	return Found ? *Found : 0;
}