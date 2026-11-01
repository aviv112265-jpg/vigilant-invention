// MISHPACHA - main game module.
using UnrealBuildTool;

public class Mishpacha : ModuleRules
{
	public Mishpacha(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ChaosVehicles",
			"PhysicsCore",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			"UMG",
			"NetCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"SlateCore" // FSlateFontInfo in MishpachaHUD.h
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"RenderCore" // GWhiteTexture for the canvas HUD
		});
	}
}
