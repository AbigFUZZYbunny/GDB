// StateTreeCondition_DynamicLogic.cpp
#include "StateTreeCondition_DynamicLogic.h"
#include "StateTreeExecutionContext.h"
#include "AbilitySystemComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "GameFramework/Actor.h"

bool FStateTreeCondition_DynamicLogic::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.SourceActor || InstanceData.ConditionTreeName.IsNone())
	{
		return InstanceData.bInvert;
	}

	const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(InstanceData.SourceActor, InstanceData.ConditionTreeName);
	if (!ConditionTree)
	{
		return InstanceData.bInvert;
	}

	const UAbilitySystemComponent* SourceASC = InstanceData.SourceActor->FindComponentByClass<UAbilitySystemComponent>();
	const UAbilitySystemComponent* TargetASC = InstanceData.TargetActor ? InstanceData.TargetActor->FindComponentByClass<UAbilitySystemComponent>() : SourceASC;

	FGameplayTagContainer SourceTags;
	if (SourceASC) SourceASC->GetOwnedGameplayTags(SourceTags);

	FGameplayTagContainer TargetTags;
	if (TargetASC) TargetASC->GetOwnedGameplayTags(TargetTags);

	const bool bResult = UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, SourceASC, TargetASC, SourceTags, TargetTags);
	return bResult ^ InstanceData.bInvert;
}