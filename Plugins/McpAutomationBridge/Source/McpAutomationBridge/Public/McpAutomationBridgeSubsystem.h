#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "Subsystems/EngineSubsystem.h"
#include "McpAutomationBridgeSubsystem.generated.h"

class FMcpNativeTransport;

DECLARE_LOG_CATEGORY_EXTERN(LogMcpAutomationBridgeSubsystem, Log, All);

UCLASS()
class MCPAUTOMATIONBRIDGE_API UMcpAutomationBridgeSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void QueueAutomationRequest(
        const FString& RequestId,
        const FString& Action,
        const TSharedPtr<FJsonObject>& Payload);

    void SendAutomationResponse(
        const FString& RequestId,
        bool bSuccess,
        const FString& Message,
        const TSharedPtr<FJsonObject>& Result = nullptr,
        const FString& ErrorCode = FString());

    void SendAutomationError(
        const FString& RequestId,
        const FString& Message,
        const FString& ErrorCode);

private:
    struct FPendingRequest
    {
        FString RequestId;
        FString Action;
        TSharedPtr<FJsonObject> Payload;
    };

    bool Tick(float DeltaTime);
    void ProcessPendingRequests();
    void ProcessAutomationRequest(
        const FString& RequestId,
        const FString& Action,
        const TSharedPtr<FJsonObject>& Payload);

    bool HandleManageAsset(
        const FString& RequestId,
        const TSharedPtr<FJsonObject>& Payload);

    bool HandleManageBlueprint(
        const FString& RequestId,
        const TSharedPtr<FJsonObject>& Payload);

    TArray<FPendingRequest> PendingRequests;
    FCriticalSection PendingRequestsMutex;
    FTSTicker::FDelegateHandle TickHandle;
    TSharedPtr<FMcpNativeTransport> NativeTransport;
};
