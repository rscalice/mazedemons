// MazeDemons - editor-only helper that authors the enemy StateTree asset from C++ (V1 WP1).
// The StateTree editor has no scripting surface for nodes (FInstancedStruct tasks are not reachable from Python and the
// MCP StateTree toolset only inspects), so ST_Enemy is built once here with the StateTree Builder API, compiled and saved.
// The result is an ordinary asset Robert can open and tweak in the StateTree editor; re-running the build overwrites it.
// Python (commandlet, editor closed):  ok, log = unreal.MD_AILibrary.build_enemy_state_tree("/Game/AI/ST_Enemy")

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_AILibrary.generated.h"

UCLASS()
class MAZEDEMONSEDITOR_API UMD_AILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds (or rebuilds) the enemy executor StateTree at PackagePath (e.g. "/Game/AI/ST_Enemy"):
	 *   schema  StateTree AI Component (AMDEnemyController / AMDEnemyCharacter)
	 *   states  Root -> Idle, Chase, HoldThreat, Attack, Stagger, Dead, one MD Enemy task each (Idle is selected first)
	 *   events  Root transitions On Event AI.Intent.<X> -> that state (the brain sends them); every leaf also goes back to
	 *           Idle when its state completes (the tasks never complete, this is a safety net).
	 * Compiles and saves the asset. OutLog gets the compiler messages. Returns true when compiled and saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static bool BuildEnemyStateTree(const FString& PackagePath, TArray<FString>& OutLog);

	/** Human-readable summary of a StateTree asset's editor data: states, tasks, transitions. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|AI")
	static TArray<FString> DescribeStateTree(const FString& PackagePath);
};
