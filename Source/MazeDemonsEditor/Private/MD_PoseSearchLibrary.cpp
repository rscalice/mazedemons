#include "MD_PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

TArray<UAnimSequence*> UMD_PoseSearchLibrary::FindAnimsInFolder(const FString& FolderPath)
{
	TArray<UAnimSequence*> Result;
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = Module.Get();
	Registry.ScanPathsSynchronous({ FolderPath }, true);

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*FolderPath), Assets, false);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
	for (const FAssetData& Data : Assets)
	{
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Data.GetAsset()))
		{
			Result.Add(Sequence);
		}
	}
	return Result;
}

int32 UMD_PoseSearchLibrary::BuildStanceDatabase(const FString& DatabasePath, const UPoseSearchSchema* Schema, const FString& AnimFolder, TArray<FString>& OutLog)
{
	if (!Schema)
	{
		OutLog.Add(TEXT("Error: no schema given"));
		return -1;
	}
	const TArray<UAnimSequence*> Sequences = FindAnimsInFolder(AnimFolder);
	if (Sequences.IsEmpty())
	{
		OutLog.Add(FString::Printf(TEXT("%s has no animations: no database built (placeholder stays)"), *AnimFolder));
		return 0;
	}
	return BuildDatabaseFromAnims(DatabasePath, Schema, Sequences, OutLog);
}

int32 UMD_PoseSearchLibrary::BuildDatabaseFromAnims(const FString& DatabasePath, const UPoseSearchSchema* Schema, const TArray<UAnimSequence*>& Sequences, TArray<FString>& OutLog)
{
	TArray<UAnimationAsset*> Assets;
	Assets.Reserve(Sequences.Num());
	for (UAnimSequence* Sequence : Sequences)
	{
		Assets.Add(Sequence);
	}
	return BuildDatabaseFromAssets(DatabasePath, Schema, Assets, OutLog);
}

int32 UMD_PoseSearchLibrary::BuildDatabaseFromAssets(const FString& DatabasePath, const UPoseSearchSchema* Schema, const TArray<UAnimationAsset*>& Sequences, TArray<FString>& OutLog)
{
	if (!Schema)
	{
		OutLog.Add(TEXT("Error: no schema given"));
		return -1;
	}
	if (Sequences.IsEmpty())
	{
		OutLog.Add(FString::Printf(TEXT("%s: no animations given, no database built"), *DatabasePath));
		return 0;
	}

	const FString ShortName = FPackageName::GetShortName(DatabasePath);
	UPackage* Package = CreatePackage(*DatabasePath);
	UPoseSearchDatabase* Database = FindObject<UPoseSearchDatabase>(Package, *ShortName);
	if (!Database)
	{
		Database = LoadObject<UPoseSearchDatabase>(nullptr, *FString::Printf(TEXT("%s.%s"), *DatabasePath, *ShortName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	const bool bNew = Database == nullptr;
	if (!Database)
	{
		Database = NewObject<UPoseSearchDatabase>(Package, *ShortName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Database);
	}

	Database->Modify();
	Database->Schema = Schema;
	while (Database->GetNumAnimationAssets() > 0)
	{
		Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets() - 1);
	}
	int32 Added = 0;
	for (UAnimationAsset* Sequence : Sequences)
	{
		if (!Sequence)
		{
			continue;
		}
		FPoseSearchDatabaseAnimationAsset Entry;
		Entry.AnimAsset = Sequence;
		if (!Entry.IsSkeletonCompatible(Schema))
		{
			OutLog.Add(FString::Printf(TEXT("Skipped %s: skeleton does not match the schema"), *Sequence->GetName()));
			continue;
		}
		Database->AddAnimationAsset(Entry);
		++Added;
	}
	if (Added == 0)
	{
		OutLog.Add(TEXT("Error: no animation in the folder is compatible with the schema"));
		return -1;
	}

	const UE::PoseSearch::EAsyncBuildIndexResult Index = UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
		Database, UE::PoseSearch::ERequestAsyncBuildFlag::NewRequest | UE::PoseSearch::ERequestAsyncBuildFlag::WaitForCompletion);
	if (Index != UE::PoseSearch::EAsyncBuildIndexResult::Success)
	{
		OutLog.Add(TEXT("Error: building the search index failed"));
		return -1;
	}

	Database->MarkPackageDirty();
	const FString FileName = FPackageName::LongPackageNameToFilename(DatabasePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Database, *FileName, SaveArgs))
	{
		OutLog.Add(TEXT("Error: saving the database failed"));
		return -1;
	}
	OutLog.Add(FString::Printf(TEXT("%s %s: %d animations, index built"), bNew ? TEXT("Created") : TEXT("Refreshed"), *DatabasePath, Added));
	return Added;
}
