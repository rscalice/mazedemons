#include "AI/MDAIQueries.h"

#include "AI/MDAISubsystem.h"
#include "AI/MDEnvQueryContextPlayers.h"
#include "AI/MDPackRules.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "GameFramework/Actor.h"
#include "MDQuestTypes.h"
#include "NavigationSystem.h"

namespace MDAIQueriesLocal
{
	FVector FlatDirection(const FVector& V)
	{
		FVector D(V.X, V.Y, 0.0);
		return D.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}

	FVector Eye(const FVector& P)
	{
		return P + FVector(0.0, 0.0, MDAIQueries::EyeHeight);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Room reveal gate
// ---------------------------------------------------------------------------------------------------------------------

bool MDAIQueries::IsPointRevealed(const FMDFogGrid& Grid, const FVector& Point)
{
	if (!Grid.IsValid())
	{
		return true;
	}
	const int32 Cell = Grid.WorldToCell(FVector2D(Point.X, Point.Y));
	return Cell < 0 || Grid.IsRevealed(Cell);
}

// ---------------------------------------------------------------------------------------------------------------------
// Ring
// ---------------------------------------------------------------------------------------------------------------------

void MDAIQueries::RingSlots(const FVector& TargetLocation, float BaseYawDeg, float Radius, TArray<FVector>& Out)
{
	Out.Reset();
	for (int32 Slot = 0; Slot < MDPack::NumRingSlots; ++Slot)
	{
		Out.Add(MDPackRules::SlotLocation(TargetLocation, Slot, BaseYawDeg, Radius));
	}
}

void MDAIQueries::ValidRingSlots(const FVector& TargetLocation, float BaseYawDeg, float Radius, const FMDQueryEnvironment& Env, TArray<FVector>& Out)
{
	TArray<FVector> Raw;
	RingSlots(TargetLocation, BaseYawDeg, Radius, Raw);
	Out.Reset();
	for (const FVector& P : Raw)
	{
		FVector Projected;
		if (Env.Project(P, Projected) && Env.Revealed(Projected))
		{
			Out.Add(Projected);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Flank
// ---------------------------------------------------------------------------------------------------------------------

float MDAIQueries::AngleFromFacing(const FVector& TargetLocation, const FVector& TargetFacing, const FVector& Point)
{
	const FVector Facing = MDAIQueriesLocal::FlatDirection(TargetFacing);
	FVector ToPoint(Point.X - TargetLocation.X, Point.Y - TargetLocation.Y, 0.0);
	if (ToPoint.SizeSquared() < UE_SMALL_NUMBER)
	{
		return 0.0f;
	}
	ToPoint.Normalize();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Facing, ToPoint)), -1.0f, 1.0f)));
}

void MDAIQueries::FlankCandidates(const FMDFlankParams& Params, TArray<FVector>& Out)
{
	Out.Reset();
	const FVector Facing = MDAIQueriesLocal::FlatDirection(Params.TargetFacing);
	const int32 N = FMath::Max(Params.SamplesPerSide, 1);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const float Alpha = N > 1 ? static_cast<float>(i) / static_cast<float>(N - 1) : 0.0f;
			const float Angle = FMath::Lerp(Params.MinAngle, Params.MaxAngle, Alpha) * static_cast<float>(Side);
			const FVector Dir = Facing.RotateAngleAxis(Angle, FVector::UpVector);
			Out.Add(Params.TargetLocation + Dir * Params.Radius);
		}
	}
}

bool MDAIQueries::FindFlankPoint(const FMDFlankParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint)
{
	TArray<FVector> Candidates;
	FlankCandidates(Params, Candidates);
	bool bFound = false;
	float BestScore = -TNumericLimits<float>::Max();
	for (const FVector& C : Candidates)
	{
		FVector P;
		if (!Env.Project(C, P) || !Env.Revealed(P))
		{
			continue;
		}
		const float Angle = AngleFromFacing(Params.TargetLocation, Params.TargetFacing, P);
		if (Angle < Params.MinAngle)
		{
			continue; // the projection pulled it out of the band behind the target
		}
		float Score = Angle / 180.0f * 0.25f;
		Score -= static_cast<float>(FVector::Dist2D(Params.FromLocation, P)) / 10000.0f;
		if (!Env.LineOfSight(MDAIQueriesLocal::Eye(Params.TargetLocation), MDAIQueriesLocal::Eye(P)))
		{
			Score += 1.0f; // the target cannot see this point
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			OutPoint = P;
			bFound = true;
		}
	}
	return bFound;
}

// ---------------------------------------------------------------------------------------------------------------------
// Kite
// ---------------------------------------------------------------------------------------------------------------------

void MDAIQueries::KiteCandidates(const FMDKiteParams& Params, TArray<FVector>& Out)
{
	Out.Reset();
	const int32 NumAngles = FMath::Max(Params.NumAngles, 3);
	const float Radii[3] = { Params.PreferredRange - Params.Tolerance * 0.8f, Params.PreferredRange, Params.PreferredRange + Params.Tolerance * 0.8f };
	for (const float Radius : Radii)
	{
		if (Radius <= 0.0f)
		{
			continue;
		}
		for (int32 i = 0; i < NumAngles; ++i)
		{
			const float Angle = 360.0f * static_cast<float>(i) / static_cast<float>(NumAngles);
			const FVector Dir = FVector::ForwardVector.RotateAngleAxis(Angle, FVector::UpVector);
			Out.Add(Params.TargetLocation + Dir * Radius);
		}
	}
}

bool MDAIQueries::FindKitePoint(const FMDKiteParams& Params, const FMDQueryEnvironment& Env, FVector& OutPoint)
{
	TArray<FVector> Candidates;
	KiteCandidates(Params, Candidates);
	bool bFound = false;
	float BestScore = -TNumericLimits<float>::Max();
	for (const FVector& C : Candidates)
	{
		FVector P;
		if (!Env.Project(C, P) || !Env.Revealed(P))
		{
			continue;
		}
		const float Range = static_cast<float>(FVector::Dist2D(Params.TargetLocation, P));
		if (FMath::Abs(Range - Params.PreferredRange) > Params.Tolerance)
		{
			continue;
		}
		bool bTooCloseToMelee = false;
		for (const FVector& M : Params.MeleePlayers)
		{
			bTooCloseToMelee |= FVector::Dist2D(M, P) < Params.MinMeleeDistance;
		}
		if (bTooCloseToMelee || !Env.LineOfSight(MDAIQueriesLocal::Eye(P), MDAIQueriesLocal::Eye(Params.TargetLocation)))
		{
			continue;
		}
		const float Score = -static_cast<float>(FVector::Dist2D(Params.FromLocation, P)) / 10000.0f - FMath::Abs(Range - Params.PreferredRange) / FMath::Max(Params.Tolerance, 1.0f) * 0.1f;
		if (Score > BestScore)
		{
			BestScore = Score;
			OutPoint = P;
			bFound = true;
		}
	}
	return bFound;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rally
// ---------------------------------------------------------------------------------------------------------------------

bool MDAIQueries::FindRallyPoint(const TArray<FVector>& RallyPoints, const TArray<FVector>& RoomEntries, const FVector& Near, const FMDQueryEnvironment& Env, FVector& OutPoint)
{
	auto Nearest = [&Near, &Env, &OutPoint](const TArray<FVector>& Points) -> bool
	{
		double Best = TNumericLimits<double>::Max();
		bool bFound = false;
		for (const FVector& P : Points)
		{
			if (!Env.Revealed(P))
			{
				continue;
			}
			const double D = FVector::DistSquared(Near, P);
			if (D < Best)
			{
				Best = D;
				OutPoint = P;
				bFound = true;
			}
		}
		return bFound;
	};
	return Nearest(RallyPoints) || Nearest(RoomEntries);
}

// ---------------------------------------------------------------------------------------------------------------------
// World wrappers
// ---------------------------------------------------------------------------------------------------------------------

FMDQueryEnvironment MDAIQueries::MakeWorldEnvironment(UWorld* World, const AActor* IgnoreA, const AActor* IgnoreB)
{
	FMDQueryEnvironment Env;
	if (!World)
	{
		return Env;
	}
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const TWeakObjectPtr<const AActor> WeakA(IgnoreA);
	const TWeakObjectPtr<const AActor> WeakB(IgnoreB);

	Env.ProjectToNav = [WeakWorld](const FVector& In, FVector& Out)
	{
		UNavigationSystemV1* Nav = WeakWorld.IsValid() ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(WeakWorld.Get()) : nullptr;
		if (!Nav)
		{
			Out = In; // no navigation system in this world: nothing to project on
			return true;
		}
		FNavLocation Loc;
		if (Nav->ProjectPointToNavigation(In, Loc, FVector(150.0, 150.0, 250.0)))
		{
			Out = Loc.Location;
			return true;
		}
		return false;
	};
	Env.HasLineOfSight = [WeakWorld, WeakA, WeakB](const FVector& From, const FVector& To)
	{
		const UWorld* W = WeakWorld.Get();
		if (!W)
		{
			return true;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MDAILineOfSight), false);
		if (const AActor* A = WeakA.Get())
		{
			Params.AddIgnoredActor(A);
		}
		if (const AActor* B = WeakB.Get())
		{
			Params.AddIgnoredActor(B);
		}
		return !W->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
	};
	Env.IsRevealed = [WeakWorld](const FVector& Point)
	{
		const UMDAISubsystem* AI = UMDAISubsystem::Get(WeakWorld.Get());
		return !AI || AI->IsRoomRevealed(Point);
	};
	return Env;
}

bool MDAIQueries::FindFlankPointFor(const AActor* Enemy, const AActor* Target, float Radius, FVector& OutPoint)
{
	UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return false;
	}
	FMDFlankParams P;
	P.TargetLocation = Target->GetActorLocation();
	P.TargetFacing = Target->GetActorForwardVector();
	P.FromLocation = Enemy->GetActorLocation();
	P.Radius = Radius;
	return FindFlankPoint(P, MakeWorldEnvironment(World, Enemy, Target), OutPoint);
}

bool MDAIQueries::FindKitePointFor(const AActor* Enemy, const AActor* Target, float PreferredRange, FVector& OutPoint)
{
	UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return false;
	}
	FMDKiteParams P;
	P.TargetLocation = Target->GetActorLocation();
	P.FromLocation = Enemy->GetActorLocation();
	P.PreferredRange = PreferredRange;
	if (const UMDAISubsystem* AI = UMDAISubsystem::Get(World))
	{
		AI->CollectMeleePlayerLocations(P.MeleePlayers);
	}
	return FindKitePoint(P, MakeWorldEnvironment(World, Enemy, Target), OutPoint);
}

bool MDAIQueries::FindRallyPointFor(UWorld* World, const FVector& Near, FVector& OutPoint)
{
	UMDAISubsystem* AI = UMDAISubsystem::Get(World);
	if (!World || !AI)
	{
		return false;
	}
	TArray<FVector> Rallies;
	TArray<FVector> Entries;
	AI->CollectRallyPoints(Rallies, Entries);
	return FindRallyPoint(Rallies, Entries, Near, MakeWorldEnvironment(World), OutPoint);
}

// ---------------------------------------------------------------------------------------------------------------------
// Ambush (the one EQS query)
// ---------------------------------------------------------------------------------------------------------------------

void UMDEnvQueryContext_Players::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	TArray<AActor*> Players;
	if (const UMDAISubsystem* AI = UMDAISubsystem::Get(Cast<UWorld>(QueryInstance.World)))
	{
		AI->CollectPlayerPawns(Players);
	}
	UEnvQueryItemType_Actor::SetContextHelper(ContextData, Players);
}

namespace MDAIAmbushLocal
{
	// Rooted for the life of the process (a static strong pointer would be destroyed after the object system shut down).
	UEnvQuery* GTemplate = nullptr;
}

UEnvQuery* UMDAIAmbushQuery::GetTemplate()
{
	if (!MDAIAmbushLocal::GTemplate || !IsValid(MDAIAmbushLocal::GTemplate))
	{
		UEnvQuery* Query = NewObject<UEnvQuery>(GetTransientPackage(), TEXT("MDAmbushQuery"));
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);

		// A grid around the ambusher's spawn point, projected onto the navmesh.
		UEnvQueryGenerator_SimpleGrid* Grid = NewObject<UEnvQueryGenerator_SimpleGrid>(Option);
		Grid->GridSize.DefaultValue = GridHalfSize;
		Grid->SpaceBetween.DefaultValue = GridSpacing;
		Grid->GenerateAround = UEnvQueryContext_Querier::StaticClass();
		Option->Generator = Grid;

		// At least MinPlayerDistance from every player.
		UEnvQueryTest_Distance* Far = NewObject<UEnvQueryTest_Distance>(Option);
		Far->TestPurpose = EEnvTestPurpose::Filter;
		Far->TestMode = EEnvTestDistance::Distance3D;
		Far->DistanceTo = UMDEnvQueryContext_Players::StaticClass();
		Far->FilterType = EEnvTestFilterType::Minimum;
		Far->FloatValueMin.DefaultValue = MinPlayerDistance;
		Option->Tests.Add(Far);

		Query->GetOptionsMutable().Add(Option);
		Query->AddToRoot();
		MDAIAmbushLocal::GTemplate = Query;
	}
	return MDAIAmbushLocal::GTemplate;
}

bool UMDAIAmbushQuery::Run(AActor* Querier, TArray<FVector>& OutPositions)
{
	OutPositions.Reset();
	UWorld* World = Querier ? Querier->GetWorld() : nullptr;
	UEnvQueryManager* Manager = World ? UEnvQueryManager::GetCurrent(World) : nullptr;
	if (!Manager)
	{
		return false;
	}
	FEnvQueryRequest Request(GetTemplate(), Querier);
	Request.SetWorldOverride(World);
	const TSharedPtr<FEnvQueryResult> Result = Manager->RunInstantQuery(Request, EEnvQueryRunMode::AllMatching);
	if (!Result.IsValid() || !Result->IsSuccessful())
	{
		return false;
	}
	TArray<FVector> Points;
	Result->GetAllAsLocations(Points);

	// Keep the points no player can see (the grid only guarantees distance), best score first.
	const FMDQueryEnvironment Env = MDAIQueries::MakeWorldEnvironment(World, Querier);
	TArray<AActor*> Players;
	if (const UMDAISubsystem* AI = UMDAISubsystem::Get(World))
	{
		AI->CollectPlayerPawns(Players);
	}
	for (const FVector& P : Points)
	{
		bool bSeen = false;
		for (const AActor* Player : Players)
		{
			bSeen |= Env.LineOfSight(MDAIQueriesLocal::Eye(P), MDAIQueriesLocal::Eye(Player->GetActorLocation()));
		}
		if (!bSeen && Env.Revealed(P))
		{
			OutPositions.Add(P);
		}
	}
	return OutPositions.Num() > 0;
}
