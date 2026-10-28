// R0 follow-up (GUNS): the Pistol and Rifle rows of the GASP motion-matching choosers must reference real, non-empty
// per-role pose-search databases built from Robert's _FixedPistol / _FixedRifle sets (Scripts/Python/stancechoosers.py),
// in BOTH the Dense and the Sparse chooser, never the unarmed placeholders. The Staff rows stay placeholders by design.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Chooser.h"
#include "Engine/SkeletalMesh.h"
#include "MD_ChooserLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Ranged/MDRangedTypes.h"

namespace MDStanceChooserTestsLocal
{
	struct FExpectedRole
	{
		const TCHAR* Table;
		const TCHAR* Role;
		const TCHAR* Schema;
	};

	// One database per movement type, same split and same schemas as the stock Dense databases:
	// idles / turn in place / stops share the Stand Idles table, starts / loops / pivots / spin the Walks and Runs tables.
	const FExpectedRole ExpectedRoles[] = {
		{ TEXT("Stand Idles"), TEXT("Idles"), TEXT("PSS_Idle") },
		{ TEXT("Stand Idles"), TEXT("TurnInPlace"), TEXT("PSS_Default") },
		{ TEXT("Stand Idles"), TEXT("WalkStops"), TEXT("PSS_Stop") },
		{ TEXT("Stand Idles"), TEXT("RunStops"), TEXT("PSS_Stop") },
		{ TEXT("Stand Idles"), TEXT("SprintStops"), TEXT("PSS_Stop") },
		{ TEXT("Stand Walks"), TEXT("WalkStarts"), TEXT("PSS_Default") },
		{ TEXT("Stand Walks"), TEXT("WalkLoops"), TEXT("PSS_Default") },
		{ TEXT("Stand Walks"), TEXT("WalkPivots"), TEXT("PSS_Default") },
		{ TEXT("Stand Walks"), TEXT("WalkSpin"), TEXT("PSS_Default") },
		{ TEXT("Stand Runs"), TEXT("RunStarts"), TEXT("PSS_Default") },
		{ TEXT("Stand Runs"), TEXT("RunLoops"), TEXT("PSS_Default") },
		{ TEXT("Stand Runs"), TEXT("RunPivots"), TEXT("PSS_Default") },
		{ TEXT("Stand Runs"), TEXT("RunSpin"), TEXT("PSS_Default") },
		{ TEXT("Stand Sprint"), TEXT("SprintLoops"), TEXT("PSS_Default") },
		{ TEXT("Stand Sprint"), TEXT("SprintStarts"), TEXT("PSS_Default") },
		{ TEXT("Stand Sprint"), TEXT("SprintPivots"), TEXT("PSS_Default") },
	};

	// Clips that must never reach a locomotion database: aim offsets (R4), traversal, crouch, jump (not wired yet).
	bool IsExcludedClipName(const FString& Name)
	{
		return Name.Contains(TEXT("_AO_")) || Name.Contains(TEXT("HeadAim")) || Name.Contains(TEXT("Traversal"))
			|| Name.Contains(TEXT("_Crouch_")) || Name.Contains(TEXT("_Jump_"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDRangedStanceChooserTest, "MazeDemons.Ranged.Anim.StanceChooserRowsNonEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMDRangedStanceChooserTest::RunTest(const FString& Parameters)
{
	using namespace MDStanceChooserTestsLocal;

	const USkeletalMesh* Mannequin = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	TestNotNull(TEXT("SKM_UEFN_Mannequin loads"), Mannequin);
	const USkeleton* MannequinSkeleton = Mannequin ? Mannequin->GetSkeleton() : nullptr;

	struct FChooserPath { const TCHAR* Label; const TCHAR* Path; };
	const FChooserPath Choosers[] = {
		{ TEXT("Dense"), TEXT("/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/CHT_PoseSearchDatabases_Dense.CHT_PoseSearchDatabases_Dense") },
		{ TEXT("Sparse"), TEXT("/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/CHT_PoseSearchDatabases_Sparse.CHT_PoseSearchDatabases_Sparse") },
	};
	struct FStanceCase { const TCHAR* Name; EMDWeaponStance Stance; };
	const FStanceCase Stances[] = { { TEXT("Pistol"), EMDWeaponStance::Pistol }, { TEXT("Rifle"), EMDWeaponStance::Rifle } };

	for (const FChooserPath& ChooserPath : Choosers)
	{
		UChooserTable* Root = LoadObject<UChooserTable>(nullptr, ChooserPath.Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s chooser loads"), ChooserPath.Label), Root))
		{
			continue;
		}
		for (const FStanceCase& Case : Stances)
		{
			const int32 StanceMask = 1 << static_cast<int32>(Case.Stance);
			for (const FExpectedRole& Expected : ExpectedRoles)
			{
				const FString Where = FString::Printf(TEXT("%s/%s/%s %s"), ChooserPath.Label, Expected.Table, Case.Name, Expected.Role);
				UChooserTable* Table = UMD_ChooserLibrary::FindTable(Root, Expected.Table);
				if (!TestNotNull(*(Where + TEXT(": table exists")), Table))
				{
					continue;
				}
				const int32 Column = UMD_ChooserLibrary::FindColumnByBinding(Table, FName(TEXT("WeaponStances")));
				if (!TestTrue(*(Where + TEXT(": table has the WeaponStances column")), Column >= 0))
				{
					continue;
				}

				const FString ExpectedName = FString::Printf(TEXT("PSD_Ranged_%s_%s"), Case.Name, Expected.Role);
				int32 FoundRows = 0;
				for (int32 Row = 0; Row < UMD_ChooserLibrary::GetRowCount(Table); ++Row)
				{
					if (UMD_ChooserLibrary::GetEnumCellMask(Table, Column, Row) != StanceMask
						|| UMD_ChooserLibrary::GetRowAssetName(Table, Row) != ExpectedName)
					{
						continue;
					}
					++FoundRows;
					const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UMD_ChooserLibrary::GetRowObject(Table, Row));
					if (!TestNotNull(*(Where + TEXT(": row result is a pose-search database")), Database))
					{
						continue;
					}
					TestNotNull(*(Where + TEXT(": database has a schema")), Database->Schema.Get());
					if (Database->Schema)
					{
						TestEqual(*(Where + TEXT(": schema")), Database->Schema->GetName(), FString(Expected.Schema));
					}
					const int32 NumAnims = Database->GetNumAnimationAssets();
					TestTrue(*FString::Printf(TEXT("%s: database is not empty (%d clips)"), *Where, NumAnims), NumAnims > 0);
					for (int32 Index = 0; Index < NumAnims; ++Index)
					{
						const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(Index);
						const UAnimSequence* Sequence = Entry ? Cast<UAnimSequence>(Entry->AnimAsset) : nullptr;
						if (!Sequence)
						{
							AddError(FString::Printf(TEXT("%s: entry %d has no anim sequence"), *Where, Index));
							break;
						}
						if (IsExcludedClipName(Sequence->GetName()))
						{
							AddError(FString::Printf(TEXT("%s: %s must not be in a locomotion database"), *Where, *Sequence->GetName()));
							break;
						}
						if (MannequinSkeleton && Sequence->GetSkeleton() != MannequinSkeleton)
						{
							AddError(FString::Printf(TEXT("%s: %s is not on the SK_UEFN_Mannequin skeleton"), *Where, *Sequence->GetName()));
							break;
						}
					}
				}
				TestTrue(*FString::Printf(TEXT("%s: chooser row points at %s (found %d)"), *Where, *ExpectedName, FoundRows), FoundRows >= 1);
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
