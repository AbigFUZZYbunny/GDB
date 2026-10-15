// DynamicEffectExecution.cpp
#include "DynamicEffectExecution.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"

UDynamicEffectExecution::UDynamicEffectExecution() {}

void UDynamicEffectExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& ExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    
	FDynamicGameplayEffectContext* DynContext = static_cast<FDynamicGameplayEffectContext*>(Spec.GetContext().Get());
	if (!DynContext || DynContext->CustomConditionTreeName.IsNone()) return;

	const FDynamicConditionTreeContainer* RuntimeConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(
		ExecutionParams.GetTargetAbilitySystemComponent()->GetOwner(), 
		DynContext->CustomConditionTreeName
	);
	if (!RuntimeConditionTree) return;

	UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();

	// FIX: GetAggregatedTags() returns a 'const FGameplayTagContainer*'. 
	// We safely dereference it if valid, or fall back to an empty container.
	const FGameplayTagContainer* SourceTagsPtr = Spec.CapturedSourceTags.GetAggregatedTags();
	FGameplayTagContainer SourceTags = SourceTagsPtr ? *SourceTagsPtr : FGameplayTagContainer();
    
	const FGameplayTagContainer* TargetTagsPtr = Spec.CapturedTargetTags.GetAggregatedTags();
	FGameplayTagContainer TargetTags = TargetTagsPtr ? *TargetTagsPtr : FGameplayTagContainer();

	// Evaluate the index-based logical tree with the structural container directly
	if (!UDynamicLogicEvaluator::EvaluateConditionTree(*RuntimeConditionTree, SourceASC, TargetASC, SourceTags, TargetTags))
	{
		return; // Logic failed: Abort execution calculation without applying modifiers
	}
    
	// Custom dynamic math operations proceed safely here...
}