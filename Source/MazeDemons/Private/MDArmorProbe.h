#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MDArmorProbe.generated.h"

// Trace line (LogMDArmorProbe, Warning) tagged with the net mode; prints only while md.ArmorProbe is on. No-op in shipping builds.
void MDArmorTrace(const UObject* Context, const FString& Message);

// Debug (non-shipping): md.ArmorProbe 1 logs every machine's view of every player's armor (LogMDArmorProbe) and saves a screenshot at
// the times in md.ArmorProbe.Times (seconds after BeginPlay, e.g. "8,25"). md.ArmorProbe.EquipAt / .Equip make the SERVER give and
// equip the listed item rows to every player at that second. Used to verify armor replication in a real listen-server + client run.
UCLASS()
class UMDArmorProbeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void Dump(int32 Second) const;
	void EquipRows() const;
	void UnequipArmor() const;

	FTSTicker::FDelegateHandle TickHandle;
	int32 Second = -1;
};
