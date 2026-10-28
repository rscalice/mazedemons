#include "Ranged/MDAmmoDrops.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "MDGameSettings.h"
#include "MDGameplayStatics.h"
#include "MDInventoryTypes.h"
#include "MDAbilitySystemComponent.h"
#include "Ranged/MDAmmoPickup.h"
#include "Ranged/MDAmmoRules.h"
#include "Ranged/MDRangedComponent.h"

const FMDAmmoDropRow* FMDAmmoDrops::FindRow(const UDataTable* Table, const FGameplayTagContainer& Tags)
{
	if (!Table)
	{
		return nullptr;
	}
	for (const FGameplayTag& Tag : Tags)
	{
		if (const FMDAmmoDropRow* Row = Table->FindRow<FMDAmmoDropRow>(Tag.GetTagName(), TEXT("AmmoDrops"), false))
		{
			return Row;
		}
	}
	return nullptr;
}

int32 FMDAmmoDrops::DropQuantity(const FMDAmmoDropRow& Row, int32 ShotsSinceDrop, const FMDEconomyTuning& Economy, float Roll01)
{
	if (Row.AmmoRow.IsNone() || Row.Quantity <= 0)
	{
		return 0;
	}
	const float Chance = FMDAmmoRules::PityDropChance(Row.Chance, ShotsSinceDrop, Economy.AmmoPityShotsPerStep, Economy.AmmoPityBonus);
	return FMDAmmoRules::RollDrops(Chance, Roll01) ? Row.Quantity : 0;
}

AMDAmmoPickup* FMDAmmoDrops::RollKillDrop(const AActor* Victim, const AController* Killer)
{
	UWorld* World = Victim ? Victim->GetWorld() : nullptr;
	if (!World || !Victim->HasAuthority())
	{
		return nullptr;
	}
	const UMDAbilitySystemComponent* VictimASC = UMDGameplayStatics::GetMDAbilitySystemComponent(Victim);
	if (!VictimASC)
	{
		return nullptr;
	}
	FGameplayTagContainer VictimTags;
	VictimASC->GetOwnedGameplayTags(VictimTags);

	const UDataTable* Table = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TablePath())).LoadSynchronous();
	const FMDAmmoDropRow* Row = FindRow(Table, VictimTags);
	if (!Row)
	{
		return nullptr;
	}

	UMDRangedComponent* Ranged = Killer && Killer->PlayerState ? UMDRangedComponent::Find(Killer->PlayerState) : nullptr;
	const int32 Quantity = DropQuantity(*Row, Ranged ? Ranged->GetShotsSinceAmmoDrop() : 0, UMDGameSettings::GetEconomy(), FMath::FRand());
	if (Quantity <= 0)
	{
		return nullptr;
	}
	if (Ranged)
	{
		Ranged->ResetShotsSinceAmmoDrop();
	}

	AMDAmmoPickup* Pickup = World->SpawnActorDeferred<AMDAmmoPickup>(AMDAmmoPickup::StaticClass(),
		FTransform(Victim->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Pickup)
	{
		Pickup->ItemRowName = Row->AmmoRow;
		Pickup->Quantity = Quantity;
		Pickup->FinishSpawning(Pickup->GetActorTransform());
	}
	return Pickup;
}
