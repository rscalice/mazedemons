// R0: editor-only pose-search database builder for the per-stance ranged databases (ranged-design.md section 7.1).
// Python (commandlet, editor closed), see Scripts/Python/stancechoosers.py:
//   n, log = unreal.MD_PoseSearchLibrary.build_stance_database("/Game/Animations/MotionMatchingData/Ranged/PSD_Ranged_Pistol",
//                schema, "/Game/Animations/Ranged/Pistol")

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_PoseSearchLibrary.generated.h"

class UAnimationAsset;
class UAnimSequence;
class UPoseSearchSchema;

UCLASS()
class MAZEDEMONSEDITOR_API UMD_PoseSearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Anim sequences found directly in the content folder (not subfolders), sorted by name. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|PoseSearch")
	static TArray<UAnimSequence*> FindAnimsInFolder(const FString& FolderPath);

	/**
	 * Creates or refreshes the pose-search database at DatabasePath (e.g. "/Game/Animations/MotionMatchingData/Ranged/PSD_Ranged_Pistol"):
	 * sets Schema, replaces its animation list with every anim sequence in AnimFolder that is compatible with the schema's skeleton,
	 * validates (schema set, at least one animation) and builds the search index (waits for it). Idempotent: running it again on an
	 * unchanged folder yields an identical database. Saves the database. Returns the number of animations in the database, or -1
	 * on failure; a missing / empty folder returns 0 and does NOT create or touch a database (the caller keeps a placeholder).
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|PoseSearch")
	static int32 BuildStanceDatabase(const FString& DatabasePath, const UPoseSearchSchema* Schema, const FString& AnimFolder, TArray<FString>& OutLog);

	/**
	 * Same as BuildStanceDatabase but from an explicit list of anim sequences (the caller picks them, for example by name from an
	 * asset set that is split across subfolders and must not be moved). Skeleton-incompatible sequences are skipped and logged.
	 * Returns the number of animations in the database, 0 for an empty list (nothing built), -1 on failure. Idempotent.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|PoseSearch")
	static int32 BuildDatabaseFromAnims(const FString& DatabasePath, const UPoseSearchSchema* Schema, const TArray<UAnimSequence*>& Sequences, TArray<FString>& OutLog);

	/**
	 * Same as BuildDatabaseFromAnims but accepts any animation asset the database supports (sequences, montages, composites,
	 * blend spaces), e.g. the GASP-style traversal database, which holds montages. Same validation, index build and save.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|PoseSearch")
	static int32 BuildDatabaseFromAssets(const FString& DatabasePath, const UPoseSearchSchema* Schema, const TArray<UAnimationAsset*>& Assets, TArray<FString>& OutLog);
};
