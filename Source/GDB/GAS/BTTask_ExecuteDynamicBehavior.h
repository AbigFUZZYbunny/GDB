// BTTask_ExecuteDynamicBehavior.h
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "DynamicGASTypes.h"
#include "BTTask_ExecuteDynamicBehavior.generated.h"

class ADynamicCharacter;

UCLASS()
class GDB_API UBTTask_ExecuteDynamicBehavior : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_ExecuteDynamicBehavior();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

    UPROPERTY(EditAnywhere, Category = "Dynamic Behavior")
    bool bAutoSelectHighestPriorityBehavior = true;

    UPROPERTY(EditAnywhere, Category = "Dynamic Behavior")
    FGameplayTag ExplicitBehaviorTag;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BehaviorTagKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector TargetActorKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector TargetLocationKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector UtteranceTextKey;

protected:
    bool ResolveSpatialTarget(ADynamicCharacter* Character, const FDynamicAIBehaviorDef& BehaviorDef, UBehaviorTreeComponent& OwnerComp, FVector& OutLocation, AActor*& OutActor);
    void FinishBehaviorExecution(UBehaviorTreeComponent& OwnerComp, bool bSuccess);

private:
    float ElapsedExecutionTime = 0.0f;
    float TargetExecutionDuration = 2.0f;
    FDynamicAIBehaviorDef ActiveBehaviorDef;
    TWeakObjectPtr<ADynamicCharacter> CachedCharacter;
};