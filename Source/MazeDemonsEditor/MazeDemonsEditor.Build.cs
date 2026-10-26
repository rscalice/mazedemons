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
		});
	}
}
