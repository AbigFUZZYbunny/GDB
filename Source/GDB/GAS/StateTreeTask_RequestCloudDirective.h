#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "DynamicGASTypes.h"
#include "Containers/Queue.h"
#include "StateTreeTask_RequestCloudDirective.generated.h"

class ADynamicCharacter;
class UAnimMontage;

struct FCloudAIStateTreeResultBucket
{
    TArray<FCloudAICommandOutput> Commands;
    TQueue<FString, EQueueMode::Mpsc> StreamingTokenQueue;
    float EstimatedLatency = 0.0f;
    bool bAssigned = false;
    bool bCompleted = false;
    bool bFailed = false;
};

USTRUCT(BlueprintType)
struct GDB_API FStateTreeTask_RequestCloudDirective_InstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Context")
    TObjectPtr<ADynamicCharacter> Character = nullptr;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    ECloudAIRequestType RequestType = ECloudAIRequestType::ComplexDecision;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    ECloudAITaskPriority TaskPriority = ECloudAITaskPriority::Medium;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    float BaseRequestTimeoutSeconds = 8.0f;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    FCloudAIVampingConfig VampingConfig;

    UPROPERTY(EditAnywhere, Category = "Context")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(Transient)
    FGuid ActiveRequestGuid;

    UPROPERTY(Transient)
    float ElapsedTaskTime = 0.0f;

    UPROPERTY(Transient)
    float DynamicTimeoutLimit = 8.0f;

    UPROPERTY(Transient)
    bool bVampingStarted = false;

    UPROPERTY(Transient)
    bool bUtteranceStarted = false;

    UPROPERTY(Transient)
    FString LiveUtteranceBuffer;

    UPROPERTY(Transient)
    TWeakObjectPtr<UAnimMontage> ActiveVampingMontage = nullptr;

    TSharedPtr<FCloudAIStateTreeResultBucket> ResultBucket = nullptr;
};

USTRUCT(BlueprintType)
struct GDB_API FStateTreeTask_RequestCloudDirective : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    typedef FStateTreeTask_RequestCloudDirective_InstanceData FInstanceDataType;

    FStateTreeTask_RequestCloudDirective();

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
    virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
    void StartVampingBehavior(ADynamicCharacter* Character, FInstanceDataType& InstanceData) const;
    void StopVampingBehavior(ADynamicCharacter* Character, FInstanceDataType& InstanceData) const;
    void ProcessCompletedDirectives(ADynamicCharacter* Character, FInstanceDataType& InstanceData, const TArray<FCloudAICommandOutput>& Commands) const;
};