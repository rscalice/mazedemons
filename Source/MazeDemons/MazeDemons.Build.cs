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
				"EnhancedInput",
				"PhysicsCore",
				"Niagara",
				"MotionWarping",
				"StateTreeModule",         // V1 enemy executor tasks (AI/StateTree)
				"GameplayStateTreeModule", // UStateTreeAIComponent on AMDEnemyController
				"NavigationSystem",
				"AnimGraphRuntime",        // R0 support-hand IK anim node (FAnimNode_TwoBoneIK)
				"AnimationCore"
			});

		PrivateDependencyModuleNames.Add("AssetRegistry"); // combo set validation test
	}
}
