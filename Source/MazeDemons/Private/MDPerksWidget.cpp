#include "MDPerksWidget.h"

#include "MDGameSettings.h"
#include "MDPerkCatalog.h"
#include "MDPerkComponent.h"
#include "MDPlayerState.h"

#include "GameFramework/PlayerState.h"

TArray<uint8> UMDPerksWidget::GetOwned() const
{
	TArray<uint8> Owned;
	if (const UMDPerkComponent* Perks = PerkComponent.Get())
	{
		Owned = Perks->GetOwnedRanks();
	}
	Owned.SetNumZeroed(MD_NumPerkNodes);
	return Owned;
}

void UMDPerksWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PendingRanks.SetNumZeroed(MD_NumPerkNodes);
	TryBind();
}

void UMDPerksWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UMDPerksWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TryBind(); // PlayerState not replicated yet at construct time: retry until it is there
}

void UMDPerksWidget::Unbind()
{
	if (UMDPerkComponent* Perks = PerkComponent.Get())
	{
		Perks->OnPerksChanged.RemoveDynamic(this, &UMDPerksWidget::HandlePerksChanged);
	}
	PerkComponent.Reset();
	LastOwnedTotal = -1;
}

void UMDPerksWidget::TryBind()
{
	const AMDPlayerState* PS = Cast<AMDPlayerState>(GetOwningPlayerState());
	UMDPerkComponent* Perks = PS ? PS->PerkComponent.Get() : nullptr;
	if (!Perks || PerkComponent.Get() == Perks)
	{
		return;
	}
	Unbind();
	PerkComponent = Perks;
	Perks->OnPerksChanged.AddUniqueDynamic(this, &UMDPerksWidget::HandlePerksChanged);
	HandlePerksChanged();
}

void UMDPerksWidget::HandlePerksChanged()
{
	PendingRanks.SetNumZeroed(MD_NumPerkNodes);
	const TArray<uint8> Owned = GetOwned();
	const int32 OwnedTotal = UMDPerkCatalog::TotalRanks(Owned);
	const bool bRespecced = LastOwnedTotal >= 0 && OwnedTotal < LastOwnedTotal;
	LastOwnedTotal = OwnedTotal;
	if (bRespecced)
	{
		// Owned ranks dropped (station respec): the pending plan was made against the old build, start clean.
		PendingRanks.Init(0, MD_NumPerkNodes);
		LastCommitted.Reset();
	}
	else if (LastCommitted.Num() == MD_NumPerkNodes && Owned == LastCommitted)
	{
		// The commit arrived: the pending ranks are owned now.
		PendingRanks.Init(0, MD_NumPerkNodes);
		LastCommitted.Reset();
	}
	else
	{
		// Respec, debug ranks or fewer points: drop whatever stopped being legal.
		UMDPerkCatalog::SanitizePending(Owned, PendingRanks, GetPointsEarned(), *UMDGameSettings::GetPerkTree(), UMDGameSettings::GetPerkTuning());
	}
	OnPerksRefreshed();
}

// ---- Edit ----

bool UMDPerksWidget::AddPendingRank(int32 Node)
{
	if (!PerkComponent.IsValid())
	{
		return false;
	}
	PendingRanks.SetNumZeroed(MD_NumPerkNodes);
	int32 Arg = 0;
	EMDPerkPath Path = EMDPerkPath::None;
	if (UMDPerkCatalog::GetAddRankReject(GetOwned(), PendingRanks, Node, GetPointsEarned(), *UMDGameSettings::GetPerkTree(),
		UMDGameSettings::GetPerkTuning(), Arg, Path) != EMDPerkReject::None)
	{
		return false;
	}
	++PendingRanks[Node];
	LastCommitted.Reset();
	OnPerksRefreshed();
	return true;
}

bool UMDPerksWidget::RemovePendingRank(int32 Node)
{
	PendingRanks.SetNumZeroed(MD_NumPerkNodes);
	if (Node < 0 || Node >= MD_NumPerkNodes || PendingRanks[Node] == 0)
	{
		return false; // owned ranks are removed by a station respec, never here
	}
	--PendingRanks[Node];
	UMDPerkCatalog::SanitizePending(GetOwned(), PendingRanks, GetPointsEarned(), *UMDGameSettings::GetPerkTree(), UMDGameSettings::GetPerkTuning());
	LastCommitted.Reset();
	OnPerksRefreshed();
	return true;
}

void UMDPerksWidget::CancelPending()
{
	const bool bHad = HasPending();
	PendingRanks.Init(0, MD_NumPerkNodes);
	LastCommitted.Reset();
	if (bHad)
	{
		OnPerksRefreshed();
	}
}

bool UMDPerksWidget::ConfirmPending()
{
	UMDPerkComponent* Perks = PerkComponent.Get();
	if (!Perks || !HasPending())
	{
		return false;
	}
	LastCommitted = UMDPerkCatalog::MergeRanks(GetOwned(), PendingRanks, *UMDGameSettings::GetPerkTree());
	Perks->RequestCommit(LastCommitted); // set first: on a listen server the change is broadcast from inside this call
	return true;
}

// ---- Query ----

EMDPerkNodeState UMDPerksWidget::GetNodeState(int32 Node) const
{
	TArray<uint8> Pending = PendingRanks;
	Pending.SetNumZeroed(MD_NumPerkNodes);
	return UMDPerkCatalog::GetNodeState(Node, GetOwned(), Pending, *UMDGameSettings::GetPerkTree());
}

bool UMDPerksWidget::GetNodeInfo(int32 Node, FMDPerkNodeView& Out) const
{
	Out = FMDPerkNodeView();
	const UMDPerkTree& Tree = *UMDGameSettings::GetPerkTree();
	if (Node < 0 || Node >= MD_NumPerkNodes || !Tree.Nodes.IsValidIndex(Node))
	{
		return false;
	}
	const FMDPerkNode& N = Tree.Nodes[Node];
	const TArray<uint8> Owned = GetOwned();
	TArray<uint8> Pending = PendingRanks;
	Pending.SetNumZeroed(MD_NumPerkNodes);

	Out.Name = N.Name;
	Out.Rank = Owned[Node];
	Out.PendingRank = Pending[Node];
	Out.MaxRank = N.MaxRank;
	Out.Path = N.Path;
	Out.Tier = N.Tier;
	Out.RequiresPathPoints = N.RequiresPathPoints;
	Out.State = UMDPerkCatalog::GetNodeState(Node, Owned, Pending, Tree);
	Out.Description = UMDPerkCatalog::GetNodeDescription(N, Out.Rank + Out.PendingRank);
	Out.bCanRemove = Out.PendingRank > 0;

	const FMDPerkTuning& Tuning = UMDGameSettings::GetPerkTuning();
	int32 Arg = 0;
	EMDPerkPath ArgPath = EMDPerkPath::None;
	EMDPerkReject Reject = UMDPerkCatalog::GetAddRankReject(Owned, Pending, Node, GetPointsEarned(), Tree, Tuning, Arg, ArgPath);
	Out.bCanAdd = Reject == EMDPerkReject::None && PerkComponent.IsValid();
	if (Reject == EMDPerkReject::NoPoints && (Out.State == EMDPerkNodeState::Locked || Out.State == EMDPerkNodeState::BranchCapped))
	{
		// Out of points as well: still tell the player the structural reason (prerequisite / path limit).
		Reject = UMDPerkCatalog::GetAddRankReject(Owned, Pending, Node, 1000, Tree, Tuning, Arg, ArgPath);
	}
	Out.RejectText = UMDPerkCatalog::GetPerkRejectText(Reject, Arg, ArgPath);
	return true;
}

int32 UMDPerksWidget::GetPathPoints(EMDPerkPath Path) const
{
	TArray<uint8> Pending = PendingRanks;
	Pending.SetNumZeroed(MD_NumPerkNodes);
	return UMDPerkCatalog::PointsInPath(UMDPerkCatalog::MergeRanks(GetOwned(), Pending, *UMDGameSettings::GetPerkTree()), Path);
}

int32 UMDPerksWidget::GetPathPointCap() const
{
	return UMDGameSettings::GetPerkTree()->Caps.PathPointCap;
}

int32 UMDPerksWidget::GetPointsEarned() const
{
	const UMDPerkComponent* Perks = PerkComponent.Get();
	return Perks ? Perks->GetPointsEarned() : 0;
}

int32 UMDPerksWidget::GetPendingCount() const
{
	return UMDPerkCatalog::TotalRanks(PendingRanks);
}

int32 UMDPerksWidget::GetPointsAvailable() const
{
	const UMDPerkComponent* Perks = PerkComponent.Get();
	return Perks ? FMath::Max(0, Perks->GetPointsAvailable() - GetPendingCount()) : 0;
}

float UMDPerksWidget::GetSpendLockSeconds() const
{
	const UMDPerkComponent* Perks = PerkComponent.Get();
	return Perks ? UMDPerkCatalog::GetSpendLockSeconds(Perks->GetTimeSinceDamage(), UMDGameSettings::GetPerkTuning()) : 0.0f;
}

bool UMDPerksWidget::HasPending() const
{
	return GetPendingCount() > 0;
}

bool UMDPerksWidget::CanConfirm() const
{
	const UMDPerkComponent* Perks = PerkComponent.Get();
	if (!Perks || !HasPending())
	{
		return false;
	}
	return Perks->GetCommitReject(UMDPerkCatalog::MergeRanks(GetOwned(), PendingRanks, *UMDGameSettings::GetPerkTree())) == EMDPerkReject::None;
}
