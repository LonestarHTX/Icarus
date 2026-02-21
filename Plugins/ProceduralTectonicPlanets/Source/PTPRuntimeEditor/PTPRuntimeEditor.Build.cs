using UnrealBuildTool;

public class PTPRuntimeEditor : ModuleRules
{
    public PTPRuntimeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "EditorStyle",
            "ToolMenus",
            "LevelEditor",
            "PTPCore",
            "PTPRuntime"
        });
    }
}
