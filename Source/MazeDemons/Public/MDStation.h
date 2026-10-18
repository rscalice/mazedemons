#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDStation.generated.h"

class UBoxComponent;

// Services a station can offer; stored as bit flags in AMDStation::Services.
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMDStationService : uint8
{
	None = 0 UMETA(Hidden),
	Repair = 1 << 0,
	Upgrade = 1 << 1,
	Respec = 1 << 2
};

// A forge / anvil: an interactable that opens the repair + upgrade panel for the player using it. All rules (range, funds,
// slot checks) are enforced by UMDInventoryComponent on the server; the station only says which services it offers.
UCLASS(Blueprintable)
class MAZEDEMONS_API AMDStation : public AActor
{
	GENERATED_BODY()

public:
	AMDStation();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Bit mask of EMDStationService. Default: all services.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Station", meta = (Bitmask, BitmaskEnum = "/Script/MazeDemons.EMDStationService"))
	int32 Services = 7;

	// Shown in the focus prompt ("Use Forge") and the panel header.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
	FText StationName = NSLOCTEXT("MDStation", "DefaultName", "Forge");

	UFUNCTION(BlueprintPure, Category = "Station")
	bool Supports(EMDStationService Service) const;

	// Pure: A and B are at most Range apart.
	UFUNCTION(BlueprintPure, Category = "Station")
	static bool IsWithinRange(const FVector& A, const FVector& B, float Range);

	// Authority only: opens the panel on User's owning client if User is in range.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Station")
	void Interact(APawn* User);

	// Invisible box on the Visibility channel so the interact focus sweep hits the station. Resize / replace in the BP child.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractBox;
};
