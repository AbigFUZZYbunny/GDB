#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "DynamicGASTypes.h"
#include "StateTreeTask_ExecuteDynamicBehavior.generated.h"

struct FStateTreeTransitionResult;
struct FStateTreeExecutionContext;
class ADynamicCharacter;

USTRUCT(BlueprintType)
struct FStateTreeTask_ExecuteDynamicBehavior_InstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	ADynamicCharacter* Character = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	FGameplayTag BehaviorTag;

	UPROPERTY(EditAnywhere, Category = "Context")
	AActor* EvaluatedTarget = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	FVector EvaluatedLocation = FVector::ZeroVector;
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
};