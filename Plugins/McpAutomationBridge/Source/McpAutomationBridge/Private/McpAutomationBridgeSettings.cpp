#include "McpAutomationBridgeSettings.h"

#include "UObject/UnrealType.h"

UMcpAutomationBridgeSettings::UMcpAutomationBridgeSettings()
{
    NativeMCPInstructions = TEXT(
        "Unreal editor access. Use manage_asset for asset discovery, manage_editor_settings for effective configuration, "
        "and manage_blueprint to inspect/edit Blueprints, compile/save and capture graph screenshots. "
        "Inspect existing graphs before changes. Run editor mutations sequentially. Graph edits remain unsaved until save_blueprint.");
}

FText UMcpAutomationBridgeSettings::GetSectionText() const
{
    return NSLOCTEXT("McpAutomationBridge", "SettingsSection", "MCP Automation Bridge");
}

#if WITH_EDITOR
void UMcpAutomationBridgeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    SaveConfig();
}
#endif
