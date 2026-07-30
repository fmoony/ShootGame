#include "McpAutomationBridgeSettings.h"

#include "UObject/UnrealType.h"

UMcpAutomationBridgeSettings::UMcpAutomationBridgeSettings()
{
    NativeMCPInstructions = TEXT(
        "Read-only Unreal project access. Use manage_asset for asset names and folders, "
        "and manage_blueprint for Blueprint graphs, nodes, pins, and links.");
}

FText UMcpAutomationBridgeSettings::GetSectionText() const
{
    return NSLOCTEXT("McpAutomationBridge", "SettingsSection", "MCP Automation Bridge (Read Only)");
}

#if WITH_EDITOR
void UMcpAutomationBridgeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    SaveConfig();
}
#endif
