// DynamicCloudAISubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "DynamicCloudAISubsystem.generated.h"

class IHttpResponse;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCloudAIResponseReceivedSignature, const FGuid&, RequestGuid, const TArray<FCloudAICommandOutput>&, Commands);
DECLARE_DELEGATE_OneParam(FOnCloudAIResponseDirectCallback, const TArray<FCloudAICommandOutput>&);

UCLASS()
class GDB_API UDynamicCloudAISubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Cloud AI")
    FOnCloudAIResponseReceivedSignature OnRemoteAIResponseReceived;

    // Active execution strategy
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI|Config")
    ECloudAIExecutionBackend ExecutionBackend = ECloudAIExecutionBackend::LocalRuleBasedFallback;

    // Remote MMO Cloud / Miner node dispatch endpoint
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI|Config")
    FString RemoteCloudEndpointUrl = TEXT("https://cloud.mmo-backend.internal/api/v1/ai/dispatch");

    // Local host or LAN node endpoint (Ollama, llama.cpp, or local dedicated miner process)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI|Config")
    FString LocalNodeEndpointUrl = TEXT("http://127.0.0.1:11434/api/generate");

    // Optional authentication bearer token for miner networks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI|Config")
    FString CloudAuthBearerToken = TEXT("");

    // Fallback switch: Automatically generate rule-based decisions if HTTP requests drop or timeout
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI|Config")
    bool bFallbackToLocalHeuristicOnHttpFailure = true;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    void SetExecutionBackend(ECloudAIExecutionBackend NewBackend) { ExecutionBackend = NewBackend; }

    // Blueprint dispatch entry point
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    FGuid DispatchRemoteAIRequest(
        AActor* RequestingAgent,
        ECloudAIRequestType RequestType,
        const FString& SerializedAgentContextJson,
        float RequestTimeoutSeconds = 8.0f
    );

    // Native C++ dispatch entry point with direct O(1) callback registration
    FGuid DispatchRemoteAIRequestNative(
        AActor* RequestingAgent,
        ECloudAIRequestType RequestType,
        const FString& SerializedAgentContextJson,
        float RequestTimeoutSeconds,
        FOnCloudAIResponseDirectCallback DirectCallback
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    bool CancelRemoteAIRequest(const FGuid& RequestGuid);

    // Ingests raw JSON returned by external LLM, strips Markdown fences, sanitizes action space, and routes to agent
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    void InjectRemoteAIResponse(const FGuid& RequestGuid, const FString& ResponseJson);

    // Strips ```json ... ``` markdown delimiters and trailing chat wraps
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI|Sanitization")
    static FString CleanMarkdownJsonString(const FString& InRawJson);

    // Validates a raw command against the strict GameplayTag dictionary and authorized categories
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI")
    bool ValidateActionSpaceCommand(const FCloudAICommandOutput& InCommand, FCloudAICommandOutput& OutSanitizedCommand) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    bool InjectDynamicConditionTreeFromJson(FName TreeName, const FString& ConditionTreeJson);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI")
    bool IsRequestPending(const FGuid& RequestGuid) const;

private:
    struct FPendingRemoteRequest
    {
        FGuid RequestGuid;
        TWeakObjectPtr<AActor> RequesterActor;
        ECloudAIRequestType RequestType;
        double DispatchWorldTime = 0.0;
        float TimeoutDuration = 8.0f;
        TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
        FOnCloudAIResponseDirectCallback DirectCallback;
    };

    TMap<FGuid, FPendingRemoteRequest> PendingRequests;
    FTimerHandle TimerHandle_RequestWatchdog;

    void CheckPendingRequestTimeouts();
    bool ValidateConditionNodeTreeAcyclic(const FDynamicConditionTreeContainer& Tree) const;

    void HandleHttpResponse(const FGuid& RequestGuid, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request, TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bConnectedSuccessfully);
    void ExecuteLocalHeuristicDecision(const FGuid& RequestGuid, AActor* RequesterActor, ECloudAIRequestType RequestType, const FString& SerializedContextJson);
};