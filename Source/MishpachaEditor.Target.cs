// MISHPACHA - editor target.
using UnrealBuildTool;
using System.Collections.Generic;

public class MishpachaEditorTarget : TargetRules
{
	public MishpachaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Mishpacha");
	}
}
