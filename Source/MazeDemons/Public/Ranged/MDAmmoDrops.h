// R3: the ammo drop table and the kill hook. DT_AmmoDrops rows are keyed by the archetype tag name (Enemy.Ghoul ...); a kill rolls the
// row of the victim's archetype. The chance carries the killer's pity bonus (+10% per 20 shots without a drop, reset on a drop).
// Drops are per party: one pouch, picked up by whoever touches it first.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "MDAmmoDrops.generated.h"

class AController;
class AMDAmmoPickup;
struct FMDEconomyTuning;

USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDAmmoDropRow : public FTableRowBase
{
	GENERATED_BODY()

	// DT_Items row of the ammo (AmmoLight / AmmoHeavy).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Drop")
	FName AmmoRow = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Drop", meta = (ClampMin = "1"))
	int32 Quantity = 4;

	// Base chance 0..1 before the pity bonus.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo Drop", meta = (ClampMin = "0", ClampMax = "1"))
	float Chance = 0.15f;
};

struct MAZEDEMONS_API FMDAmmoDrops
{
	static const TCHAR* TablePath() { return TEXT("/Game/Blueprints/Items/DT_AmmoDrops.DT_AmmoDrops"); }

	// The row for the first archetype tag in Tags that has one; null when none does (or the table is null).
	static const FMDAmmoDropRow* FindRow(const UDataTable* Table, const FGameplayTagContainer& Tags);

	// Rounds that drop for one roll: the row's Quantity when Roll01 < chance-with-pity, else 0.
	static int32 DropQuantity(const FMDAmmoDropRow& Row, int32 ShotsSinceDrop, const FMDEconomyTuning& Economy, float Roll01);

	// Server: roll the victim's row for the killer's pity, spawn a pouch at the victim on a drop and reset the pity. Returns the
	// pouch (null on no drop). Called from the kill path (UMDGameplayStatics::AwardKillGold).
	static AMDAmmoPickup* RollKillDrop(const AActor* Victim, const AController* Killer);
};
