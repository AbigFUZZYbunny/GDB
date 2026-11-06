// DynamicLogicEvaluator.cpp
#include "DynamicLogicEvaluator.h"
#include "DynamicAttributeSet.h"
#include "DynamicSocialComponent.h"
#include "Engine/World.h"

bool UDynamicLogicEvaluator::EvaluateConditionTree(
    const FDynamicConditionTreeContainer& TreeContainer,
    const UAbilitySystemComponent* SourceASC,
    const UAbilitySystemComponent* TargetASC,
    const FGameplayTagContainer& SourceTags,
    const FGameplayTagContainer& TargetTags)
{
    if (TreeContainer.FlatNodes.Num() == 0) return true;
    
    // Use an iterative stack or direct evaluation index for Root (Node 0)
    return EvaluateNodeInternal(0, TreeContainer, SourceASC, TargetASC, SourceTags, TargetTags);
}

bool UDynamicLogicEvaluator::EvaluateNodeInternal(
    int32 NodeIndex,
    const FDynamicConditionTreeContainer& TreeContainer,
    const UAbilitySystemComponent* SourceASC,
    const UAbilitySystemComponent* TargetASC,
    const FGameplayTagContainer& STags,
    const FGameplayTagContainer& TTags)
{
    if (!TreeContainer.FlatNodes.IsValidIndex(NodeIndex)) return false;

    const FDynamicConditionNode& CurrentNode = TreeContainer.FlatNodes[NodeIndex];
    if (CurrentNode.ConditionType != EConditionType::LogicalGateOnly)
    {
        return EvaluateLeafNode(CurrentNode, SourceASC, TargetASC, STags, TTags);
    }

    if (CurrentNode.ChildIndices.Num() == 0) return true;

    // Optimized Short-Circuit Evaluation Loop
    switch (CurrentNode.LogicOperator)
    {
        case ELogicOperator::And:
        {
            for (int32 ChildIdx : CurrentNode.ChildIndices)
            {
                if (!EvaluateNodeInternal(ChildIdx, TreeContainer, SourceASC, TargetASC, STags, TTags))
                {
                    return false; // Short-circuit on first false
                }
            }
            return true;
        }
        case ELogicOperator::Or:
        {
            for (int32 ChildIdx : CurrentNode.ChildIndices)
            {
                if (EvaluateNodeInternal(ChildIdx, TreeContainer, SourceASC, TargetASC, STags, TTags))
                {
                    return true; // Short-circuit on first true
                }
            }
            return false;
        }
        case ELogicOperator::Nand:
        {
            bool bAllTrue = true;
            for (int32 ChildIdx : CurrentNode.ChildIndices)
            {
                if (!EvaluateNodeInternal(ChildIdx, TreeContainer, SourceASC, TargetASC, STags, TTags))
                {
                    bAllTrue = false;
                    break;
                }
            }
            return !bAllTrue;
        }
        case ELogicOperator::Nor:
        {
            for (int32 ChildIdx : CurrentNode.ChildIndices)
            {
                if (EvaluateNodeInternal(ChildIdx, TreeContainer, SourceASC, TargetASC, STags, TTags))
                {
                    return false; // Short-circuit on first true (which fails NOR)
                }
            }
            return true;
        }
    }
    return false;
}

bool UDynamicLogicEvaluator::EvaluateLeafNode(
    const FDynamicConditionNode& Node,
    const UAbilitySystemComponent* SourceASC,
    const UAbilitySystemComponent* TargetASC,
    const FGameplayTagContainer& STags,
    const FGameplayTagContainer& TTags)
{
    switch (Node.ConditionType)
    {
        case EConditionType::TagExists:
        {
            const FGameplayTag LooseTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            return LooseTag.IsValid() && (Node.bCheckSource ? STags.HasTag(LooseTag) : TTags.HasTag(LooseTag));
        }
        case EConditionType::AttributeValue:
        {
            const UAbilitySystemComponent* ChosenASC = Node.bCheckSource ? SourceASC : TargetASC;
            const UDynamicAttributeSet* DynSet = ChosenASC ? Cast<UDynamicAttributeSet>(ChosenASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
            const FGameplayTag AttributeTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            if (!DynSet || !AttributeTag.IsValid()) return false;
            
            // Direct FastCompositeLookupMap lookup optimization
            return ExecuteMathComparison(DynSet->GetDynamicAttributeValue(AttributeTag, 0), Node.ComparisonValue, Node.ComparisonOperator);
        }
        case EConditionType::AttributeComparedToWorldTime:
        {
            const UAbilitySystemComponent* ChosenASC = Node.bCheckSource ? SourceASC : TargetASC;
            const UDynamicAttributeSet* DynSet = ChosenASC ? Cast<UDynamicAttributeSet>(ChosenASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
            const FGameplayTag AttributeTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            if (!DynSet || !AttributeTag.IsValid()) return false;
            const UWorld* World = ChosenASC ? ChosenASC->GetWorld() : nullptr;
            const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;
            return ExecuteMathComparison(DynSet->GetDynamicAttributeValue(AttributeTag, 0), CurrentTime + Node.ComparisonValue, Node.ComparisonOperator);
        }
        case EConditionType::RelationshipMetricCheck:
        {
            const AActor* SourceActor = SourceASC ? SourceASC->GetOwner() : nullptr;
            const AActor* TargetActor = TargetASC ? TargetASC->GetOwner() : nullptr;
            if (!SourceActor || !TargetActor) return false;

            const UDynamicSocialComponent* SourceSocial = SourceActor->FindComponentByClass<UDynamicSocialComponent>();
            const UDynamicSocialComponent* TargetSocial = TargetActor->FindComponentByClass<UDynamicSocialComponent>();
            if (!SourceSocial || !TargetSocial) return false;

            FGameplayTag MetricTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            if (!MetricTag.IsValid())
            {
                const FString PrefixedKey = FString::Printf(TEXT("Relationship.%s"), *Node.UserDefinedKey.ToString());
                MetricTag = FGameplayTag::RequestGameplayTag(FName(*PrefixedKey), false);
            }
            if (!MetricTag.IsValid()) return false;

            const float MetricVal = SourceSocial->GetDecayedRelationshipMetric(TargetSocial->CharacterPersistentGuid, MetricTag);
            return ExecuteMathComparison(MetricVal, Node.ComparisonValue, Node.ComparisonOperator);
        }
        case EConditionType::NeedValue:
        {
            const UAbilitySystemComponent* ChosenASC = Node.bCheckSource ? SourceASC : TargetASC;
            const AActor* ChosenActor = ChosenASC ? ChosenASC->GetOwner() : nullptr;
            const UDynamicSocialComponent* SocialComp = ChosenActor ? ChosenActor->FindComponentByClass<UDynamicSocialComponent>() : nullptr;
            const FGameplayTag NeedTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            if (!SocialComp || !NeedTag.IsValid()) return false;

            float NeedVal = 0.0f;
            SocialComp->GetNeedValue(NeedTag, NeedVal);
            return ExecuteMathComparison(NeedVal, Node.ComparisonValue, Node.ComparisonOperator);
        }
        case EConditionType::PerceivedBountyCheck:
        {
            const AActor* SourceActor = SourceASC ? SourceASC->GetOwner() : nullptr;
            const AActor* TargetActor = TargetASC ? TargetASC->GetOwner() : nullptr;
            if (!SourceActor || !TargetActor) return false;

            const UDynamicSocialComponent* SourceSocial = SourceActor->FindComponentByClass<UDynamicSocialComponent>();
            const UDynamicSocialComponent* TargetSocial = TargetActor->FindComponentByClass<UDynamicSocialComponent>();
            if (!SourceSocial || !TargetSocial) return false;

            FGameplayTag JurisdictionTag = FGameplayTag::RequestGameplayTag(Node.UserDefinedKey, false);
            if (!JurisdictionTag.IsValid()) return false;

            const float PerceivedBounty = SourceSocial->GetPerceivedBounty(TargetSocial->CharacterPersistentGuid, JurisdictionTag);
            return ExecuteMathComparison(PerceivedBounty, Node.ComparisonValue, Node.ComparisonOperator);
        }
        default:
            return false;
    }
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