// BTTask_ResolveSpatialRoleToLocation.h
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "BTTask_ResolveSpatialRoleToLocation.generated.h"

/**
 * Behavior Tree task that resolves an abstract TargetEntityOrPlaceRoleTag into a concrete world FVector.
 * Queries agent's UDynamicSocialComponent memory first, falling back to an actor proximity sweep.
 */
UCLASS()
class GDB_API UBTTask_ResolveSpatialRoleToLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_ResolveSpatialRoleToLocation();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector RoleTagKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector TargetLocationKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector TargetActorKey;

    UPROPERTY(EditAnywhere, Category = "Resolution")
    bool bCheckSocialKnowledgeFirst = true;

    UPROPERTY(EditAnywhere, Category = "Resolution")
    float MaxSearchRadius = 25000.0f;
};