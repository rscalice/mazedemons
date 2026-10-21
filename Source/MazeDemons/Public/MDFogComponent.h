#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MDQuestTypes.h"
#include "MDFogComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDFogChanged);

// Shared party fog of war, on AMDGameState. The server reveals around every living player pawn every UpdateInterval and
// replicates one bit per cell and zone (only the zones whose bits changed are re-sent), so late joiners get the party's fog.
// Reveal only: bits are never cleared except by the ResetAllZones debug call.
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class MAZEDEMONS_API UMDFogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMDFogComponent();

	// The fog component of the world's AMDGameState, or null.
	static UMDFogComponent* FindFor(const UObject* WorldContextObject);

	// Unreal units around each living pawn that are revealed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Fog", meta = (ClampMin = "0"))
	float RevealRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze Demons|Fog", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.25f;

	UPROPERTY(ReplicatedUsing = OnRep_Fog)
	TArray<FMDFogZoneBits> Fog;

	// Fires on the server after a change and on clients after replication.
	UPROPERTY(BlueprintAssignable, Category = "Maze Demons|Fog")
	FMDFogChanged OnFogChanged;

	// Copies the replicated bits of Zone (false when nothing is revealed there yet).
	bool GetZoneBits(int32 Zone, TArray<uint8>& OutBits) const;

	// Server debug: reveal everything / hide everything.
	void RevealAllZones();
	void ResetAllZones();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void OnRep_Fog();

private:
	bool IsServer() const;
	void UpdateFog();
	void PublishZone(int32 Zone, const FMDFogGrid& Grid);

	// Server-side working grids by zone index (not replicated).
	TMap<int32, FMDFogGrid> Grids;
	FTimerHandle UpdateTimer;
};
