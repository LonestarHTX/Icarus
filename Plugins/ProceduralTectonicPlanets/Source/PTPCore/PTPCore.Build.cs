using UnrealBuildTool;

public class PTPCore : ModuleRules
{
    public PTPCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "GeometryCore"
        });
    }
}
