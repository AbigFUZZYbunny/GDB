// BTTask_RequestCloudDirective.h
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "DynamicGASTypes.h"
#include "BTTask_RequestCloudDirective.generated.h"

class UDynamicCloudAISubsystem;

UCLASS()
class GDB_API UBTTask_RequestCloudDirective : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_RequestCloudDirective();
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

    UPROPERTY(EditAnywhere, Category = "Cloud AI")
    ECloudAIRequestType RequestType = ECloudAIRequestType::ComplexDecision;
    
    UPROPERTY(EditAnywhere, Category = "Cloud AI")
    ECloudAITaskPriority TaskPriority = ECloudAITaskPriority::Medium;

    UPROPERTY(EditAnywhere, Category = "Cloud AI")
    float BaseRequestTimeoutSeconds = 8.0f;

    UPROPERTY(EditAnywhere, Category = "Cloud AI|Vamping")
    FCloudAIVampingConfig VampingConfig;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector ActionTagKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector TargetPlaceRoleKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector UtteranceTextKey;

protected:
    void HandleTaskAssigned(float EstimatedLatencySeconds);
    void HandleTokenStream(const FString& TokenChunk, TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp);
    void HandleDirectCloudResponse(TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp, const TArray<FCloudAICommandOutput>& Commands);
    void StartVampingBehavior(APawn* ControlledPawn, UBehaviorTreeComponent& OwnerComp);
    void StopVampingBehavior(APawn* ControlledPawn);

private:
    FGuid ActiveRequestGuid;
    TWeakObjectPtr<UDynamicCloudAISubsystem> CachedCloudSubsystem;
    
    float ElapsedTaskTime = 0.0f;
    float DynamicTimeoutLimit = 8.0f;
    
    bool bVampingStarted = false;
    bool bUtteranceStarted = false;
    FString LiveUtteranceBuffer;

    TWeakObjectPtr<UAnimMontage> ActiveVampingMontage;
};