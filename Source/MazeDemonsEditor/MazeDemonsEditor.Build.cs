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
		});
	}
}
