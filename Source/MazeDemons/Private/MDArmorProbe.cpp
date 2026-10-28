#include "MDArmorProbe.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "MDGameplayStatics.h"
#include "MDInventoryComponent.h"
#include "MDPlayerState.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDArmorProbe, Log, All);

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarArmorProbe(TEXT("md.ArmorProbe"), 0, TEXT("Debug: log + screenshot every player's armor on this machine."));
static TAutoConsoleVariable<FString> CVarArmorProbeTimes(TEXT("md.ArmorProbe.Times"), TEXT("20,60"), TEXT("Seconds after BeginPlay to dump and screenshot."));
static TAutoConsoleVariable<int32> CVarArmorProbeEquipAt(TEXT("md.ArmorProbe.EquipAt"), 0, TEXT("Second at which the server gives and equips md.ArmorProbe.Equip to every player (0 = never)."));
static TAutoConsoleVariable<int32> CVarArmorProbeUnequipAt(TEXT("md.ArmorProbe.UnequipAt"), 0, TEXT("Second at which the server unequips every player's armor slots 4-8 (0 = never)."));
static TAutoConsoleVariable<int32> CVarArmorProbeExecAt(TEXT("md.ArmorProbe.ExecAt"), 0, TEXT("Second at which every machine runs md.ArmorProbe.Exec (0 = never)."));
static TAutoConsoleVariable<FString> CVarArmorProbeExec(TEXT("md.ArmorProbe.Exec"), TEXT(""), TEXT("Console command for md.ArmorProbe.ExecAt, e.g. KillPlayer."));
static TAutoConsoleVariable<FString> CVarArmorProbeEquip(TEXT("md.ArmorProbe.Equip"), TEXT(""), TEXT("Comma separated item rows."));
#endif

// Friend of AMDPlayerState: reads the replicated armor meshes.
struct FMDArmorProbeAccess
{
	static FString PSMeshes(const AMDPlayerState* PS)
	{
		auto N = [](const USkeletalMesh* M) { return M ? M->GetName() : FString(TEXT("-")); };
		return FString::Printf(TEXT("T=%s H=%s L=%s F=%s"), *N(PS->TorsoMesh), *N(PS->HandsMesh), *N(PS->LegsMesh), *N(PS->FeetMesh));
	}
};

void MDArmorTrace(const UObject* Context, const FString& Message)
{
#if !UE_BUILD_SHIPPING
	if (CVarArmorProbe.GetValueOnGameThread() <= 0)
	{
		return;
	}
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const TCHAR* Role = !World ? TEXT("?") : World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("host");
	UE_LOG(LogMDArmorProbe, Warning, TEXT("ARMORTRACE %s %s: %s"), Role, *GetNameSafe(Context), *Message);
#endif
}

bool UMDArmorProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* W = Cast<UWorld>(Outer);
	return W && W->IsGameWorld(); // the cvar is read per tick: -ExecCmds runs after the first world exists
#endif
}

void UMDArmorProbeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
#if !UE_BUILD_SHIPPING
	// Saved/armor_probe.cmds (one console command per line) lets a PIE session switch the probe on without a console.
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("armor_probe.cmds"))))
	{
		for (const FString& Line : Lines)
		{
			if (!Line.TrimStartAndEnd().IsEmpty() && GEngine)
			{
				GEngine->Exec(&InWorld, *Line);
			}
		}
	}
#endif
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMDArmorProbeSubsystem::Tick), 0.0f);
}

void UMDArmorProbeSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

bool UMDArmorProbeSubsystem::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (CVarArmorProbe.GetValueOnGameThread() <= 0)
	{
		return true;
	}
	// One shared clock: the server world time, so the host and the client run the same timeline however late the client joined.
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return true;
	}
	const int32 Now = FMath::FloorToInt(GameState->GetServerWorldTimeSeconds());
	if (Second < 0)
	{
		Second = Now; // a late joiner does not replay the seconds before it arrived
	}
	while (Second < Now)
	{
		++Second;
		TArray<FString> Times;
		CVarArmorProbeTimes.GetValueOnGameThread().Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Times, TEXT(","));  // '+' too: -ExecCmds splits on ','
		for (const FString& T : Times)
		{
			if (FCString::Atoi(*T) == Second)
			{
				Dump(Second);
			}
		}
		if (CVarArmorProbeUnequipAt.GetValueOnGameThread() == Second)
		{
			UnequipArmor();
		}
		if (CVarArmorProbeEquipAt.GetValueOnGameThread() == Second)
		{
			EquipRows();
		}
		if (CVarArmorProbeExecAt.GetValueOnGameThread() == Second && GEngine)
		{
			GEngine->Exec(GetWorld(), *CVarArmorProbeExec.GetValueOnGameThread());
		}
	}
#endif
	return true;
}

void UMDArmorProbeSubsystem::UnequipArmor() const
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	for (APlayerState* PS : World->GetGameState()->PlayerArray)
	{
		if (UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(PS))
		{
			for (uint8 Slot = MDItemType::Helmet; Slot <= MDItemType::Boots; ++Slot)
			{
				Inv->RequestUnequip(Slot);
			}
			UE_LOG(LogMDArmorProbe, Warning, TEXT("ARMORPROBE unequipped armor slots for %s"), *GetNameSafe(PS));
		}
	}
#endif
}

void UMDArmorProbeSubsystem::EquipRows() const
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	TArray<FString> Rows;
	CVarArmorProbeEquip.GetValueOnGameThread().Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Rows, TEXT(","));
	for (APlayerState* PS : World->GetGameState()->PlayerArray)
	{
		UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(PS);
		if (!Inv)
		{
			continue;
		}
		for (const FString& Row : Rows)
		{
			const FName RowName(*Row);
			if (Inv->AddItem(RowName, 1))
			{
				const int32 Index = Inv->GetItems().IndexOfByPredicate([RowName](const FMDItemStack& S) { return S.ItemRowName == RowName; });
				if (Index != INDEX_NONE)
				{
					Inv->RequestEquip(Index);
				}
			}
			UE_LOG(LogMDArmorProbe, Warning, TEXT("ARMORPROBE equip %s for %s"), *Row, *GetNameSafe(PS));
		}
	}
#endif
}

void UMDArmorProbeSubsystem::Dump(int32 InSecond) const
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World || !World->GetGameState())
	{
		return;
	}
	const TCHAR* Role = World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("host");
	for (APlayerState* PS : World->GetGameState()->PlayerArray)
	{
		const AMDPlayerState* MDPS = Cast<AMDPlayerState>(PS);
		const APawn* Pawn = PS->GetPawn();
		FString Equipped;
		if (const UMDInventoryComponent* Inv = UMDGameplayStatics::GetInventory(PS))
		{
			for (const FMDEquippedSlot& E : Inv->GetEquipped())
			{
				Equipped += FString::Printf(TEXT("%d:%s "), E.Slot, *E.ItemRowName.ToString());
			}
		}
		FString Comps;
		if (Pawn)
		{
			TArray<USkeletalMeshComponent*> Skels;
			Pawn->GetComponents<USkeletalMeshComponent>(Skels);
			for (const USkeletalMeshComponent* C : Skels)
			{
				const FString N = C->GetName();
				if (N == TEXT("Torso") || N == TEXT("Hands") || N == TEXT("Legs") || N == TEXT("Feet"))
				{
					Comps += FString::Printf(TEXT("%s=%s "), *N, C->GetSkeletalMeshAsset() ? *C->GetSkeletalMeshAsset()->GetName() : TEXT("-"));
				}
			}
		}
		UE_LOG(LogMDArmorProbe, Warning, TEXT("ARMORPROBE t=%d %s ps=%s local=%d pawn=%s equipped=[%s] psMeshes{%s} pawnMeshes{%s}"), InSecond, Role,
			*PS->GetPlayerName(), Pawn && Pawn->IsLocallyControlled(), *GetNameSafe(Pawn), *Equipped, MDPS ? *FMDArmorProbeAccess::PSMeshes(MDPS) : TEXT("?"), *Comps);
	}
#endif
}
