#include "McpAutomationBridgeSubsystem.h"

#include "MCP/McpNativeTransport.h"
#include "McpAutomationBridgeSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogMcpAutomationBridgeSubsystem);

void UMcpAutomationBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (IsRunningCommandlet())
    {
        return;
    }

    const UMcpAutomationBridgeSettings* Settings = GetDefault<UMcpAutomationBridgeSettings>();
    if (Settings && Settings->bEnableNativeMCP)
    {
        FString PluginDirectory;
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("McpAutomationBridge")))
        {
            PluginDirectory = Plugin->GetBaseDir();
        }

        NativeTransport = MakeShared<FMcpNativeTransport>(this);
        if (!NativeTransport->Start(
                Settings->NativeMCPPort,
                PluginDirectory,
                Settings->bLoadAllToolsOnStart,
                Settings->NativeMCPInstructions,
                Settings->ListenHost,
                Settings->bAllowNonLoopback))
        {
            UE_LOG(LogMcpAutomationBridgeSubsystem, Error,
                TEXT("Failed to start read-only MCP server on %s:%d"),
                *Settings->ListenHost, Settings->NativeMCPPort);
            NativeTransport.Reset();
        }
    }

    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMcpAutomationBridgeSubsystem::Tick));
}

void UMcpAutomationBridgeSubsystem::Deinitialize()
{
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }

    if (NativeTransport)
    {
        NativeTransport->Shutdown();
        NativeTransport.Reset();
    }

    Super::Deinitialize();
}

void UMcpAutomationBridgeSubsystem::QueueAutomationRequest(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload)
{
    FScopeLock Lock(&PendingRequestsMutex);
    PendingRequests.Add({RequestId, Action, Payload});
}

bool UMcpAutomationBridgeSubsystem::Tick(float DeltaTime)
{
    ProcessPendingRequests();
    if (NativeTransport)
    {
        NativeTransport->CleanupStaleRequests();
    }
    return true;
}

void UMcpAutomationBridgeSubsystem::ProcessPendingRequests()
{
    TArray<FPendingRequest> LocalRequests;
    {
        FScopeLock Lock(&PendingRequestsMutex);
        Swap(LocalRequests, PendingRequests);
    }

    for (const FPendingRequest& Request : LocalRequests)
    {
        ProcessAutomationRequest(Request.RequestId, Request.Action, Request.Payload);
    }
}

void UMcpAutomationBridgeSubsystem::ProcessAutomationRequest(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload)
{
    if (!Payload.IsValid())
    {
        SendAutomationError(RequestId, TEXT("Request payload is missing"), TEXT("INVALID_PAYLOAD"));
        return;
    }

    if (Action.Equals(TEXT("manage_asset"), ESearchCase::IgnoreCase))
    {
        HandleManageAsset(RequestId, Payload);
        return;
    }

    if (Action.Equals(TEXT("manage_blueprint"), ESearchCase::IgnoreCase))
    {
        HandleManageBlueprint(RequestId, Payload);
        return;
    }

    SendAutomationError(
        RequestId,
        FString::Printf(TEXT("Unsupported read-only action: %s"), *Action),
        TEXT("ACTION_NOT_SUPPORTED"));
}

void UMcpAutomationBridgeSubsystem::SendAutomationResponse(
    const FString& RequestId,
    bool bSuccess,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Result,
    const FString& ErrorCode)
{
    if (!NativeTransport ||
        !NativeTransport->CompletePendingRequest(RequestId, bSuccess, Message, Result, ErrorCode))
    {
        UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
            TEXT("No pending native MCP request for response %s"), *RequestId);
    }
}

void UMcpAutomationBridgeSubsystem::SendAutomationError(
    const FString& RequestId,
    const FString& Message,
    const FString& ErrorCode)
{
    UE_LOG(LogMcpAutomationBridgeSubsystem, Warning, TEXT("%s: %s"), *ErrorCode, *Message);
    SendAutomationResponse(RequestId, false, Message, nullptr, ErrorCode);
}
