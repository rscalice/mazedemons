#pragma once

#include "CoreMinimal.h"
#include "MDPerkTypes.h"
#include "MDUIWidgets.h"
#include "MDPerksWidget.generated.h"

class UMDPerkComponent;

// Everything the Perks panel shows about one node, in one struct (UMDPerksWidget::GetNodeInfo).
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDPerkNodeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	FText Name;

	// Perk icon, null when the texture is missing.
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	TObjectPtr<UTexture2D> Icon = nullptr;

	// The node description with this rank's value filled in (the next rank when none is owned or pending).
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	FText Description;

	// Owned ranks.
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	int32 Rank = 0;

	// Ranks added in the panel and not committed yet.
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	int32 PendingRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	int32 MaxRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	EMDPerkPath Path = EMDPerkPath::None;

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	EMDPerkTier Tier = EMDPerkTier::None;

	// Points the path needs before rank 1 (0 / 0 / 2 / 4).
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	int32 RequiresPathPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	EMDPerkNodeState State = EMDPerkNodeState::None;

	// Why one more rank cannot be added right now ("Requires 2 points in Defense", "Path limit reached (5)", ...); empty when it can.
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	FText RejectText;

	// One more rank can be added / a pending rank can be removed.
	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	bool bCanAdd = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perk")
	bool bCanRemove = false;
};

// Native base of the Perks panel (W_MDPerksPanel, U3 section 3.1). Owns the client-side pending model: PendingRanks are
// ranks added on top of the owned ones and are never replicated. All rules come from the same pure UMDPerkCatalog functions
// the server validates with, so the panel and the server cannot disagree. The Blueprint child is layout only.
UCLASS(Abstract)
class MAZEDEMONS_API UMDPerksWidget : public UMDUserWidget
{
	GENERATED_BODY()

public:
	// Ranks added in this panel on top of the owned ones (length MD_NumPerkNodes, same indexing as the owned ranks).
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|Perks")
	TArray<uint8> PendingRanks;

	// ---- Edit the pending set ----
	// Adds one pending rank when the tree rules allow it (points, path cap, prerequisites). False when refused; GetNodeInfo
	// explains why. Allowed while in combat; only Confirm is blocked then.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	bool AddPendingRank(int32 Node);

	// Removes one PENDING rank (never an owned one). Pending ranks that depended on it are dropped too.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	bool RemovePendingRank(int32 Node);

	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	void CancelPending();

	// Sends owned + pending to the server (PerkComponent RequestCommit). No-op (false) when nothing is pending. The pending set
	// clears when the server's new ranks arrive; a refused commit leaves it as it is (the server shows the reason as a toast).
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Perks")
	bool ConfirmPending();

	// ---- Query ----
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	EMDPerkNodeState GetNodeState(int32 Node) const;

	// False for a bad node index.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	bool GetNodeInfo(int32 Node, FMDPerkNodeView& Out) const;

	// Points in Path: owned plus pending.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPathPoints(EMDPerkPath Path) const;

	// Per-path point cap (5), for the "x/5" header.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPathPointCap() const;

	// Points not spent yet: the owned view minus pending ranks.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPointsAvailable() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPointsEarned() const;

	// Total pending ranks.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	int32 GetPendingCount() const;

	// Seconds until committing is allowed (SpendDelay minus the time since the last damage taken), 0 when allowed.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	float GetSpendLockSeconds() const;

	// Something is pending and the server would accept owned + pending right now.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	bool CanConfirm() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	bool HasPending() const;

	UFUNCTION(BlueprintPure, Category = "Maze Demons|Perks")
	bool IsBound() const { return PerkComponent.IsValid(); }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Owned ranks, points or pending changed (also once when the widget binds). Re-read everything you show.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|Perks")
	void OnPerksRefreshed();

private:
	void TryBind();
	void Unbind();
	TArray<uint8> GetOwned() const;

	UFUNCTION()
	void HandlePerksChanged();

	TWeakObjectPtr<UMDPerkComponent> PerkComponent;

	// The desired array sent by the last ConfirmPending; when the owned ranks become equal to it, the pending set is spent.
	TArray<uint8> LastCommitted;

	// Owned rank total at the last refresh (-1 = none yet); a drop means a respec, which clears the pending set.
	int32 LastOwnedTotal = -1;
};
