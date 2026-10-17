using UnrealBuildTool;

public class MazeDemons : ModuleRules
{
	public MazeDemons(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"NetCore",
				"AIModule",
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",
				"UMG",
				"Slate",
				"SlateCore",
				"DeveloperSettings",
				"InputCore",
				"EnhancedInput"
			});
	}
}
