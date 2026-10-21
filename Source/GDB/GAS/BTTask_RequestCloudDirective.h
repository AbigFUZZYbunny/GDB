// BTTask_RequestCloudDirective.h
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "DynamicGASTypes.h"
#include "BTTask_RequestCloudDirective.generated.h"

class UDynamicCloudAISubsystem;

struct FBTRequestCloudDirectiveMemory
{
	FGuid ActiveRequestGuid;
	TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp;
};

UCLASS()
class GDB_API UBTTask_RequestCloudDirective : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RequestCloudDirective();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTRequestCloudDirectiveMemory); }
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "Cloud AI")
	ECloudAIRequestType RequestType = ECloudAIRequestType::ComplexDecision;

	UPROPERTY(EditAnywhere, Category = "Cloud AI")
	float RequestTimeoutSeconds = 8.0f;

	// Blackboard Key to store the resolved action tag (e.g. Action.Converse.Greet or Schedule.Activity.Work)
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ActionTagKey;

	// Blackboard Key to store the target entity or landmark role tag
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetPlaceRoleKey;

	// Blackboard Key to store generated utterance dialogue text
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector UtteranceTextKey;

protected:
	UFUNCTION()
	void HandleCloudAIResponse(const FGuid& ResponseGuid, const TArray<FCloudAICommandOutput>& Commands);

private:
	TWeakObjectPtr<UDynamicCloudAISubsystem> CachedCloudSubsystem;
};