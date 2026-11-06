// StateTreeCondition_DynamicLogic.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "GameplayTagContainer.h"
#include "StateTreeCondition_DynamicLogic.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FStateTreeCondition_DynamicLogic_InstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	AActor* SourceActor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName ConditionTreeName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(BlueprintType)
struct GDB_API FStateTreeCondition_DynamicLogic : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_DynamicLogic_InstanceData FInstanceDataType;

	FStateTreeCondition_DynamicLogic() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};