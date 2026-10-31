// MazeDemons - editor-only helper that authors the enemy StateTree asset from C++ (V1 WP1).
// The StateTree editor has no scripting surface for nodes (FInstancedStruct tasks are not reachable from Python and the
// MCP StateTree toolset only inspects), so ST_Enemy is built once here with the StateTree Builder API, compiled and saved.
// The result is an ordinary asset Robert can open and tweak in the StateTree editor; re-running the build overwrites it.
// Python (commandlet, editor closed):  ok, log = unreal.MD_AILibrary.build_enemy_state_tree("/Game/AI/ST_Enemy")

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_AILibrary.generated.h"

class UBlendSpace;
class UPoseSearchDatabase;
class USkeletalMesh;
class USkeleton;

UCLASS()
class MAZEDEMONSEDITOR_API UMD_AILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds (or rebuilds) the enemy executor StateTree at PackagePath (e.g. "/Game/AI/ST_Enemy"):
	 *   schema  StateTree AI Component (AMDEnemyController / AMDEnemyCharacter)
	 *   states  Root -> Idle, Chase, HoldThreat, Retreat, Regroup, Attack, Stagger, Dead, one MD Enemy task each (Idle is selected first)
	 *   events  Root transitions On Event AI.Intent.<X> -> that state (the brain sends them); every leaf also goes back to
	 *           Idle when its state completes (the tasks never complete, this is a safety net).
	 * Compiles and saves the asset. OutLog gets the compiler messages. Returns true when compiled and saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static bool BuildEnemyStateTree(const FString& PackagePath, TArray<FString>& OutLog);

	/** Human-readable summary of a StateTree asset's editor data: states, tasks, transitions. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static TArray<FString> DescribeStateTree(const FString& PackagePath);

	/**
	 * The editor's "Assign Skeleton" for scripts (USkeletalMesh::Skeleton is read-only from Python): merges the mesh's bones into
	 * Skeleton's bone tree when they are missing (a no-op for the Ghoul: its 79 bones are all in SK_Mannequin) and points the
	 * mesh at it. Marks the mesh (and the skeleton, if it changed) dirty; nothing is saved. Returns false with OutError when
	 * the hierarchies are incompatible.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static bool AssignSkeleton(USkeletalMesh* Mesh, USkeleton* Skeleton, FString& OutError);

	/**
	 * Builds the enemy anim Blueprint at PackagePath (e.g. "/Game/Animations/Enemies/Ghoul/ABP_Ghoul"): parent class
	 * UMDEnemyAnimInstance (it computes Speed etc. in C++), TargetSkeleton = Skeleton, and an AnimGraph of four nodes laid out
	 * left to right: Motion Matching (Database) -> Pose History (trajectory generated from the character movement) -> Slot "DefaultSlot" -> Inertialization -> Output Pose.
	 * Compiles and saves. Re-running rebuilds the graph. OutLog gets the compiler messages.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static bool BuildEnemyAnimBlueprint(const FString& PackagePath, USkeleton* Skeleton, UPoseSearchDatabase* Database, TArray<FString>& OutLog);

	/**
	 * A blend space whose samples were set from a script has no triangulation/segment data until it is re-sampled (the editor
	 * does this from PostEditChangeProperty): without it the player evaluates nothing but the first sample. Validates the
	 * samples, rebuilds the blend data and reports what X (and Y) evaluate to; nothing is saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static TArray<FString> FinalizeBlendSpace(UBlendSpace* BlendSpace);

	/** Data check for the enemy animation setup: the ABP's graph nodes and links, the blend space's samples and evaluation. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static TArray<FString> DescribeEnemyAnimation(const FString& AnimBlueprintPath, UBlendSpace* BlendSpace);

	/**
	 * Makes sure the current editor world has a NavMeshBoundsVolume enclosing Box (spawns one, labelled Label, when none
	 * encloses it; volumes cannot be built from Python) and then builds navigation synchronously, so the baked RecastNavMesh
	 * is saved with the map. Returns a log whose last line starts with "OK" on success and "Error" otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static TArray<FString> EnsureNavMeshCovers(const FBox& Box, const FString& Label);
};
