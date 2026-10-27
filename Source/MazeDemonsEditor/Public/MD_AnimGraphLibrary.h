// R0: editor-only helper that edits an AnimGraph from C++ (Python cannot create anim graph nodes).
// Python (commandlet, editor closed):
//   ok, log = unreal.MD_AnimGraphLibrary.insert_support_hand_ik("/Game/Blueprints/GASP/RetargetedCharacters/ABP_GenericRetarget")

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_AnimGraphLibrary.generated.h"

UCLASS()
class MAZEDEMONSEDITOR_API UMD_AnimGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Inserts   Source -> LocalToComponent -> MD Support Hand IK -> ComponentToLocal -> Output Pose   into the AnimGraph of the
	 * animation Blueprint at AssetPath, between whatever feeds the Output Pose node and that node. Idempotent: does nothing when
	 * the IK node is already there. Compiles and saves. OutLog gets the steps and compiler messages. Returns true on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool InsertSupportHandIK(const FString& AssetPath, TArray<FString>& OutLog);

	/** Number of MD Support Hand IK nodes in the AnimGraph of AssetPath (-1 when the asset does not load). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static int32 CountSupportHandIKNodes(const FString& AssetPath);
};
