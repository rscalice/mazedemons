// V1 WP6 debug commands: put a status effect on yourself, on every player, on enemies and dummies, or clear them, and dump what is active.
//   md.Status.Apply <Bleed|Frost|Poison|Burn> [self|players|enemies|all] [Times]
//   md.Status.Clear [self|players|enemies|all]
//   md.Status.Dump
// Applying is server authority: in PIE run them in the server / listen-server window (a client window reports it cannot apply).
#include "AI/MDEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MDStatusEffectComponent.h"
#include "MDStatusEffects.h"
#include "MDTestDummy.h"

namespace MDStatusDebugLocal
{
	enum class ETargets : uint8
	{
		Self = 0,
		Players,
		Enemies,
		All
	};

	bool ParseStatus(const FString& Text, EMDStatusType& Out)
	{
		for (const EMDStatusType Type : { EMDStatusType::Bleed, EMDStatusType::Frost, EMDStatusType::Poison, EMDStatusType::Burn })
		{
			if (Text.Equals(FMDStatusRules::GetDisplayName(Type).ToString(), ESearchCase::IgnoreCase))
			{
				Out = Type;
				return true;
			}
		}
		return false;
	}

	ETargets ParseTargets(const FString& Text)
	{
		if (Text.Equals(TEXT("players"), ESearchCase::IgnoreCase))
		{
			return ETargets::Players;
		}
		if (Text.Equals(TEXT("enemies"), ESearchCase::IgnoreCase))
		{
			return ETargets::Enemies;
		}
		if (Text.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			return ETargets::All;
		}
		return ETargets::Self;
	}

	void CollectTargets(UWorld* World, ETargets Targets, TArray<UMDStatusEffectComponent*>& Out)
	{
		auto AddFor = [&Out](const AActor* Actor)
		{
			if (UMDStatusEffectComponent* Comp = UMDStatusEffectComponent::FindFor(Actor))
			{
				Out.AddUnique(Comp);
			}
		};
		if (Targets == ETargets::Self || Targets == ETargets::Players || Targets == ETargets::All)
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				if (PC && (Targets != ETargets::Self || PC->IsLocalController()))
				{
					AddFor(PC);
					if (Targets == ETargets::Self)
					{
						break;
					}
				}
			}
		}
		if (Targets == ETargets::Enemies || Targets == ETargets::All)
		{
			for (TActorIterator<AMDEnemyCharacter> It(World); It; ++It)
			{
				AddFor(*It);
			}
			for (TActorIterator<AMDTestDummy> It(World); It; ++It)
			{
				AddFor(*It);
			}
		}
	}

	void Apply(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		EMDStatusType Type = EMDStatusType::None;
		if (!World || Args.Num() < 1 || !ParseStatus(Args[0], Type))
		{
			Ar.Log(TEXT("usage: md.Status.Apply <Bleed|Frost|Poison|Burn> [self|players|enemies|all] [Times]"));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			Ar.Log(TEXT("md.Status.Apply: statuses are applied by the server; run it in the server / listen-server window"));
			return;
		}
		const ETargets Targets = Args.IsValidIndex(1) ? ParseTargets(Args[1]) : ETargets::Self;
		const int32 Times = Args.IsValidIndex(2) ? FMath::Clamp(FCString::Atoi(*Args[2]), 1, 10) : 1;

		TArray<UMDStatusEffectComponent*> Comps;
		CollectTargets(World, Targets, Comps);
		int32 Applied = 0;
		for (UMDStatusEffectComponent* Comp : Comps)
		{
			bool bAny = false;
			for (int32 Index = 0; Index < Times; ++Index)
			{
				bAny |= Comp->ApplyStatus(Type, nullptr);
			}
			Applied += bAny ? 1 : 0;
		}
		Ar.Logf(TEXT("md.Status.Apply %s x%d: %d of %d target(s)"), *FMDStatusRules::GetDisplayName(Type).ToString(), Times, Applied, Comps.Num());
	}

	void Clear(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			Ar.Log(TEXT("md.Status.Clear: run it in the server / listen-server window"));
			return;
		}
		TArray<UMDStatusEffectComponent*> Comps;
		CollectTargets(World, Args.IsValidIndex(0) ? ParseTargets(Args[0]) : ETargets::Self, Comps);
		for (UMDStatusEffectComponent* Comp : Comps)
		{
			Comp->ClearAll();
		}
		Ar.Logf(TEXT("md.Status.Clear: %d target(s)"), Comps.Num());
	}

	void Dump(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World)
		{
			return;
		}
		TArray<UMDStatusEffectComponent*> Comps;
		CollectTargets(World, ETargets::All, Comps);
		for (const UMDStatusEffectComponent* Comp : Comps)
		{
			FString Line;
			for (const FMDStatusRep& Rep : Comp->GetActiveStatuses())
			{
				Line += FString::Printf(TEXT(" %s x%d (%.1fs left)"), *FMDStatusRules::GetDisplayName(Rep.Type).ToString(), static_cast<int32>(Rep.Stacks), Comp->GetRemainingSeconds(Rep.Type));
			}
			Ar.Logf(TEXT("%s:%s  moveScale %.2f staminaDrain %.2f regenBlocked %d"), *GetNameSafe(Comp->GetOwner()), Line.IsEmpty() ? TEXT(" none") : *Line,
				Comp->GetMoveSpeedScale(), Comp->GetStaminaDrainScale(), Comp->BlocksStaminaRegen() ? 1 : 0);
		}
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdApply(TEXT("md.Status.Apply"),
		TEXT("<Bleed|Frost|Poison|Burn> [self|players|enemies|all] [Times]: apply a status effect (server)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Apply));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdClear(TEXT("md.Status.Clear"),
		TEXT("[self|players|enemies|all]: end every status effect (server)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Clear));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdDump(TEXT("md.Status.Dump"),
		TEXT("List the active status effects and their modifiers on every player, enemy and dummy."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Dump));
}
