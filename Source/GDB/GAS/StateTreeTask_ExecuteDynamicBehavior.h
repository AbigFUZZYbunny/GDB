// StateTreeTask_ExecuteDynamicBehavior.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "DynamicGASTypes.h"
#include "StateTreeTask_ExecuteDynamicBehavior.generated.h"

class ADynamicCharacter;
class AAIController;
class UAnimMontage;

struct FEQSPendingResultBucket
{
    TWeakObjectPtr<AActor> TargetActor = nullptr;
    FVector TargetLocation = FVector::ZeroVector;
    bool bCompleted = false;
    bool bSuccess = false;
};

USTRUCT(BlueprintType)
struct FStateTreeTask_ExecuteDynamicBehavior_InstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Context")
    ADynamicCharacter* Character = nullptr;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    FGameplayTag BehaviorTag;

    UPROPERTY(EditAnywhere, Category = "Output")
    AActor* EvaluatedTarget = nullptr;

    UPROPERTY(EditAnywhere, Category = "Output")
    FVector EvaluatedLocation = FVector::ZeroVector;

    UPROPERTY(Transient)
    FDynamicAIBehaviorDef ActiveBehaviorDef;

    UPROPERTY(Transient)
    float ElapsedExecutionTime = 0.0f;

    UPROPERTY(Transient)
    float TargetExecutionDuration = 1.0f;

    UPROPERTY(Transient)
    bool bWaitingForEQS = false;

    UPROPERTY(Transient)
    bool bHasDispatchedExecution = false;

    TSharedPtr<FEQSPendingResultBucket> ActiveEQSBucket = nullptr;

    TWeakObjectPtr<UAnimMontage> ActiveMontage = nullptr;
};

USTRUCT(BlueprintType)
struct GDB_API FStateTreeTask_ExecuteDynamicBehavior : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    typedef FStateTreeTask_ExecuteDynamicBehavior_InstanceData FInstanceDataType;

    FStateTreeTask_ExecuteDynamicBehavior();

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
    virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
    void ExecuteEQSQuery(FStateTreeExecutionContext& Context, const FDynamicAIBehaviorDef& BehaviorDef, FInstanceDataType& InstanceData) const;
    bool ResolveSpatialFallback(ADynamicCharacter* Character, const FDynamicAIBehaviorDef& BehaviorDef, FVector& OutLocation, AActor*& OutActor) const;
    void DispatchBehaviorAction(FStateTreeExecutionContext& Context, FInstanceDataType& InstanceData) const;
};