using UnrealBuildTool;

public class PTPRuntime : ModuleRules
{
    public PTPRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ImageWrapper",
            "PTPCore",
            "PTPSimulation",
            "RealtimeMeshComponent",
            "GeometryCore"
        });
    }
}
