// MISHPACHA - game target.
using UnrealBuildTool;
using System.Collections.Generic;

public class MishpachaTarget : TargetRules
{
	public MishpachaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Mishpacha");
	}
}
