// DynamicLogicEvaluator.cpp
#include "DynamicLogicEvaluator.h"
#include "DynamicAttributeSet.h"

bool UDynamicLogicEvaluator::EvaluateConditionTree(const FDynamicConditionTreeContainer& TreeContainer, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& TargetTags)
{
    if (TreeContainer.FlatNodes.Num() == 0) return true;
    return EvaluateNodeInternal(0, TreeContainer, SourceASC, TargetASC, SourceTags, TargetTags);
}

bool UDynamicLogicEvaluator::EvaluateNodeInternal(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& STags, const FGameplayTagContainer& TTags)
{
    if (!TreeContainer.FlatNodes.IsValidIndex(NodeIndex)) return false;
    const FDynamicConditionNode& CurrentNode = TreeContainer.FlatNodes[NodeIndex];

    if (CurrentNode.ConditionType != EConditionType::LogicalGateOnly) return EvaluateLeafNode(CurrentNode, SourceASC, TargetASC, STags, TTags);
    if (CurrentNode.ChildIndices.Num() == 0) return true;

    TArray<bool> ChildResults;
    for (int32 ChildIdx : CurrentNode.ChildIndices)
    {
        ChildResults.Add(EvaluateNodeInternal(ChildIdx, TreeContainer, SourceASC, TargetASC, STags, TTags));
    }

    switch (CurrentNode.LogicOperator)
    {
        case ELogicOperator::And:  return !ChildResults.Contains(false);
        case ELogicOperator::Or:   return ChildResults.Contains(true);
        case ELogicOperator::Nand: return ChildResults.Contains(false);
        case ELogicOperator::Nor:  return !ChildResults.Contains(true);
    }
    return false;
}

bool UDynamicLogicEvaluator::EvaluateLeafNode(const FDynamicConditionNode& Node, const UAbilitySystemComponent* SourceASC, const UAbilitySystemComponent* TargetASC, const FGameplayTagContainer& STags, const FGameplayTagContainer& TTags)
{
    if (Node.ConditionType == EConditionType::TagExists)
    {
        FGameplayTag LooseTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
        return LooseTag.IsValid() && (Node.bCheckSource ? STags.HasTag(LooseTag) : TTags.HasTag(LooseTag));
    }
    if (Node.ConditionType == EConditionType::AttributeValue)
    {
        const UAbilitySystemComponent* ChosenASC = Node.bCheckSource ? SourceASC : TargetASC;
        const UDynamicAttributeSet* DynSet = ChosenASC ? Cast<UDynamicAttributeSet>(ChosenASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
        FGameplayTag AttributeTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
        if (!DynSet || !AttributeTag.IsValid()) return false;

        return ExecuteMathComparison(DynSet->GetDynamicAttributeValue(AttributeTag), Node.ComparisonValue, Node.ComparisonOperator);
    }
    return false;
}

bool UDynamicLogicEvaluator::ExecuteMathComparison(float A, float B, EComparisonOp Op)
{
    switch (Op)
    {
        case EComparisonOp::LessThan:           return A < B;
        case EComparisonOp::LessThanOrEqual:    return A <= B;
        case EComparisonOp::Equal:              return FMath::IsNearlyEqual(A, B);
        case EComparisonOp::NotEqual:           return !FMath::IsNearlyEqual(A, B);
        case EComparisonOp::GreaterThanOrEqual: return A >= B;
        case EComparisonOp::GreaterThan:        return A > B;
    }
    return false;
}