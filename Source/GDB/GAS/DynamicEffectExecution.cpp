// DynamicEffectExecution.cpp
#include "DynamicEffectExecution.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicAttributeSet.h"

UDynamicEffectExecution::UDynamicEffectExecution()
{
    // If you need to perform default attribute captures via valid GAS definitions, 
    // you would configure your tracking arrays (InvalidationKeys / RelevantAttributesToCapture) here.
}

void UDynamicEffectExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& ExecutionOutput) const
{
    // 1. Capture the primary live spec context structure
    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    
    // 2. Extract our data-driven dynamic layout context wrapper
    FDynamicGameplayEffectContext* DynContext = static_cast<FDynamicGameplayEffectContext*>(Spec.GetContext().Get());
    if (!DynContext) return;

    UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
    UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
    if (!TargetASC) return;

    AActor* TargetActor = TargetASC->GetOwner();
    if (!TargetActor) return;

    // 3. Process Flattened Dynamic Condition Tree Validations if linked
    if (!DynContext->CustomConditionTreeName.IsNone())
    {
        const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(
            TargetActor, 
            DynContext->CustomConditionTreeName
        );

        if (ConditionTree)
        {
            // Reconstruct snapshot containers cleanly from the structural tag aggregators
            FGameplayTagContainer SourceTags;
            if (const FGameplayTagContainer* SourceTagsPtr = Spec.CapturedSourceTags.GetAggregatedTags())
            {
                SourceTags = *SourceTagsPtr;
            }
            
            FGameplayTagContainer TargetTags;
            if (const FGameplayTagContainer* TargetTagsPtr = Spec.CapturedTargetTags.GetAggregatedTags())
            {
                TargetTags = *TargetTagsPtr;
            }

            // Abort computation loop entirely if our data-driven index logic returns false
            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, SourceASC, TargetASC, SourceTags, TargetTags))
            {
                return; 
            }
        }
    }

    // 4. Extract Target Attribute Sets safely to compute custom execution math
    const UDynamicAttributeSet* TargetAttributes = Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!TargetAttributes) return;

    // Example Framework Logic: Here you can safely execute any decoupled custom math equations, 
    // loop through your dynamic modifier definitions, or apply modifications directly 
    // into the ExecutionOutput parameter collection.
    
    // float BaseDamage = Spec.GetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag("Ability.Param.BaseDamage"), false, 0.0f);
    // ExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UDynamicAttributeSet::GetHealthAttribute(), EGameplayModOp::Add, -BaseDamage));
}