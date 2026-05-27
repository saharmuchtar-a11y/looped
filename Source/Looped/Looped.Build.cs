using UnrealBuildTool;

public class Looped : ModuleRules
{
	public Looped(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"UMG",
			"Slate",
			"SlateCore",
			"Niagara",
			"NavigationSystem",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
