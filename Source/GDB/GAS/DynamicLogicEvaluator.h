// DynamicLogicEvaluator.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "AbilitySystemComponent.h"
#include "DynamicLogicEvaluator.generated.h"

UCLASS()
class GDB_API UDynamicLogicEvaluator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static bool EvaluateConditionTree(const FDynamicConditionTreeContainer& TreeContainer, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& TargetTags);

private:
	static bool EvaluateNodeInternal(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& STags, const FGameplayTagContainer& TTags);
	static bool EvaluateLeafNode(const FDynamicConditionNode& Node, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& STags, const FGameplayTagContainer& TTags);
	static bool ExecuteMathComparison(float A, float B, EComparisonOp Op);
};