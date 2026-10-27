// MazeDemons - editor-only module (Blueprint authoring helpers). Never loaded in packaged games.

using UnrealBuildTool;

public class MazeDemonsEditor : ModuleRules
{
	public MazeDemonsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			"UMG",
			"UMGEditor",
			"Slate",
			"SlateCore",
			"AssetRegistry",
			"Chooser",      // MD_ChooserLibrary: Chooser Table editing from Python
			"GameplayTags", // Chooser public headers include GameplayTagContainer.h
			"MazeDemons",
			"AIModule",
			"StateTreeModule",         // MD_AILibrary: builds ST_Enemy from C++
			"StateTreeEditorModule",
			"GameplayStateTreeModule",
			"PropertyBindingUtils",    // FStateTreeCompilerLog items
			"AnimGraph",               // MD_AILibrary: builds ABP_Ghoul's AnimGraph
			"AnimGraphRuntime",
			"NavigationSystem",        // MD_AILibrary: nav bounds + build for the prototype map
			"BSPUtils",                // FBSPOps::csgPrepMovingBrush for the spawned NavMeshBoundsVolume
		});
	}
}
