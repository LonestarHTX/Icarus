using UnrealBuildTool;

public class PTPSimulation : ModuleRules
{
    public PTPSimulation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "PTPCore"
        });
    }
}
