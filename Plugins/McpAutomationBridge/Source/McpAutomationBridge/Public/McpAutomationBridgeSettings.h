#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "McpAutomationBridgeSettings.generated.h"

UCLASS(config=Game, defaultconfig, meta=(DisplayName="MCP Automation Bridge (Read Only)"))
class MCPAUTOMATIONBRIDGE_API UMcpAutomationBridgeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UMcpAutomationBridgeSettings();

    UPROPERTY(config, EditAnywhere, Category="Native MCP")
    bool bEnableNativeMCP = true;

    UPROPERTY(config, EditAnywhere, Category="Native MCP", meta=(ClampMin="1024", ClampMax="65535"))
    int32 NativeMCPPort = 3000;

    UPROPERTY(config, EditAnywhere, Category="Native MCP")
    FString ListenHost = TEXT("127.0.0.1");

    UPROPERTY(config, EditAnywhere, Category="Native MCP")
    bool bLoadAllToolsOnStart = true;

    UPROPERTY(config, EditAnywhere, Category="Native MCP", meta=(MultiLine="true"))
    FString NativeMCPInstructions;

    UPROPERTY(config, EditAnywhere, Category="Security")
    bool bRequireCapabilityToken = false;

    UPROPERTY(config, EditAnywhere, Category="Security")
    FString CapabilityToken;

    UPROPERTY(config, EditAnywhere, Category="Security")
    bool bAllowNonLoopback = false;

    virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
    virtual FText GetSectionText() const override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
