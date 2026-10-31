// DynamicAILinkComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "DynamicAILinkComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicAILinkComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicAILinkComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Miner Node")
    FMinerNodeStats NodeStats;

    // Client-local LLM daemon endpoint (e.g. Ollama / vLLM). Server credentials are never transmitted to clients.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Miner Node|Local Daemon")
    FString LocalDaemonEndpointUrl = TEXT("http://127.0.0.1:11434/v1/chat/completions");

    // Optional client-local model override if the player's local daemon uses a specific model tag
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Miner Node|Local Daemon")
    FString LocalDaemonModelOverride = TEXT("");

    // Optional client-local bearer token for the player's own local daemon (never replicated or sent by server)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Miner Node|Local Daemon")
    FString LocalDaemonAuthToken = TEXT("");

    // HARDENED RPC: Only dispatches TaskId, Prompt, and requested ModelName. Zero server API keys or central endpoints cross the wire.
    UFUNCTION(Client, Reliable)
    void Client_AssignAITask(const FGuid& TaskId, const FString& Prompt, const FString& RequestedModel);

    UFUNCTION(Server, Unreliable, WithValidation)
    void Server_SubmitTokenChunk(const FGuid& TaskId, const FString& TokenChunk);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SubmitFinalResult(const FGuid& TaskId, const FString& FinalJsonResult);

private:
    void OnStreamProgress64(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
    void OnStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

    FGuid ActiveTaskId;
    int32 LastParsedSseIndex = 0;

    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveHttpRequest;
};