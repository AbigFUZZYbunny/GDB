// DynamicCloudAISubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicCloudAISubsystem.generated.h"

class ADynamicCharacter;
class UDynamicAILinkComponent;
class FJsonObject;

// 1. Declare Delegates Globally to avoid USTRUCT macro scope resolution failures
DECLARE_DELEGATE_OneParam(FOnCloudAIResponseDirectCallback, const TArray<FCloudAICommandOutput>&);
DECLARE_DELEGATE_OneParam(FOnCloudAITokenStream, const FString&);
DECLARE_DELEGATE_OneParam(FOnCloudAITaskAssigned, float /*EstimatedLatency*/);

USTRUCT(BlueprintType)
struct FCloudAICommand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag CommandActionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag TargetEntityOrPlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FString UtteranceText;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    float PriorityWeight = 0.0f;
};

USTRUCT(BlueprintType)
struct FCloudAIDirectiveResponse
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    TArray<FCloudAICommand> Commands;
};

USTRUCT()
struct GDB_API FCloudAIQueuedTask
{
    GENERATED_BODY()

    UPROPERTY() FGuid TaskId;
    UPROPERTY() ECloudAITaskPriority Priority = ECloudAITaskPriority::Medium;
    UPROPERTY() FString ContextPrompt;
    UPROPERTY() double TimeQueued = 0.0;
    UPROPERTY() float TimeoutSeconds = 8.0f;

    UPROPERTY() TWeakObjectPtr<class UDynamicAILinkComponent> PrimaryMiner;
    UPROPERTY() TWeakObjectPtr<class UDynamicAILinkComponent> ValidatorMiner;

    UPROPERTY() FString PrimaryAccumulatedJson;
    UPROPERTY() FString ValidatorAccumulatedJson;

    UPROPERTY() bool bPrimaryFinished = false;
    UPROPERTY() bool bValidatorFinished = false;

    UPROPERTY() bool bStartedStreamingUtterance = false;
    UPROPERTY() int32 UtteranceStartIndex = 0;
    UPROPERTY() int32 LastBroadcastedLength = 0;

    // Use the globally declared delegates
    FOnCloudAIResponseDirectCallback FinalCompletionDelegate;
    FOnCloudAITokenStream TokenStreamDelegate;
    FOnCloudAITaskAssigned TaskAssignedDelegate;
};

UCLASS()
class GDB_API UDynamicCloudAISubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { return TStatId(); }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString EndpointUrl = TEXT("http://127.0.0.1:8000/v1/chat/completions");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString ApiKey = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString ModelName = TEXT("default-model");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Rewards")
    FGameplayTag MiningRewardAttributeTag;

    void RegisterMinerNode(UDynamicAILinkComponent* Miner);
    void UnregisterMinerNode(UDynamicAILinkComponent* Miner);

    UFUNCTION(BlueprintPure, Category = "Cloud AI")
    static FString SanitizeMarkdownJson(const FString& RawResponse);

    FGuid DispatchDistributedAIRequest(
        APawn* AgentPawn,
        ECloudAIRequestType RequestType,
        ECloudAITaskPriority Priority,
        const FString& ContextJsonPrompt,
        float TimeoutSeconds,
        FOnCloudAITokenStream TokenStreamCallback,
        FOnCloudAIResponseDirectCallback DirectCallback,
        FOnCloudAITaskAssigned AssignedCallback
    );

    UFUNCTION(BlueprintCallable, Category = "Cloud AI")
    void CancelRemoteAIRequest(const FGuid& RequestGuid);

    void HandleMinerTokenChunk(UDynamicAILinkComponent* Miner, const FGuid& TaskId, const FString& Chunk);
    void HandleMinerCompletion(UDynamicAILinkComponent* Miner, const FGuid& TaskId, const FString& ResultStatus);
    void ExecuteRuleBasedFallback(ADynamicCharacter* Character, FCloudAIDirectiveResponse& OutFallbackResponse);

private:
    TArray<TWeakObjectPtr<UDynamicAILinkComponent>> ConnectedMiners;
    TArray<FCloudAIQueuedTask> TaskQueue;
    TArray<FCloudAIQueuedTask> ActiveTasks;

    void ProcessTaskQueue();
    void EvaluateConsensusAndReward(FCloudAIQueuedTask& Task);
    FString ParseSseDeltaToJsonString(const FString& SsePayload);
};