// MISHPACHA - dedicated server target.
// NOTE: Dedicated server targets can only be built with a SOURCE-BUILT engine
// (cloned from Epic's Unreal Engine GitHub repository). The Epic Games Launcher
// build of the engine cannot build this target. Listen servers (a player hosts)
// work with the launcher engine and need nothing from this file.
using UnrealBuildTool;
using System.Collections.Generic;

public class MishpachaServerTarget : TargetRules
{
	public MishpachaServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Mishpacha");
	}
}
