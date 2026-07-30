using UnrealBuildTool;

public class McpAutomationBridge : ModuleRules
{
    public McpAutomationBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = true;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "DeveloperSettings",
            "Json",
            "Projects",
            "Sockets"
        });
    }
}
