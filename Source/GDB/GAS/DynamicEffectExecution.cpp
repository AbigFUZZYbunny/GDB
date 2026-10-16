// DynamicEffectExecution.cpp
#include "DynamicEffectExecution.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicAttributeSet.h"
#include "DynamicGameState.h"

UDynamicEffectExecution::UDynamicEffectExecution() {}

void UDynamicEffectExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& ExecutionOutput) const
{
    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    FDynamicGameplayEffectContext* DynContext = static_cast<FDynamicGameplayEffectContext*>(Spec.GetContext().Get());
    if (!DynContext) return;

    UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
    UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
    if (!TargetASC) return;
    AActor* TargetActor = TargetASC->GetOwner();
    if (!TargetActor) return;

    // Flattened Dynamic Condition Tree Validations
    if (!DynContext->CustomConditionTreeName.IsNone())
    {
        const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(TargetActor, DynContext->CustomConditionTreeName);
        if (ConditionTree)
        {
            FGameplayTagContainer SourceTags;
            if (const FGameplayTagContainer* SourceTagsPtr = Spec.CapturedSourceTags.GetAggregatedTags()) SourceTags = *SourceTagsPtr;
            
            FGameplayTagContainer TargetTags;
            if (const FGameplayTagContainer* TargetTagsPtr = Spec.CapturedTargetTags.GetAggregatedTags()) TargetTags = *TargetTagsPtr;
            
            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, SourceASC, TargetASC, SourceTags, TargetTags)) return; 
        }
    }

    const UDynamicAttributeSet* TargetAttributes = Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!TargetAttributes) return;

    // Apply Authoritative Kinematic Physics Damage Calculation
    if (DynContext->GetKineticImpulse() > 0.0f)
    {
        UWorld* World = TargetASC->GetWorld();
        if (ADynamicGameState* DynamicGS = World ? World->GetGameState<ADynamicGameState>() : nullptr)
        {
            const FPhysicsCalculationSettings& CalcSettings = DynamicGS->GetCalculationSettings();
            const FPhysicsLimbConfig* LimbData = DynamicGS->GetLimbData(DynContext->GetTargetLimbId());

            const float LimbDamageMult = LimbData ? LimbData->DamageMultiplier : 1.0f;
            const float BaseKineticDamage = DynContext->GetKineticImpulse() * CalcSettings.ImpulseToDamageScalar * DynContext->GetMassRatioFactor();
            const float FinalHealthDamage = BaseKineticDamage * LimbDamageMult;

            FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag("Attribute.Stat.Health", false);
            if (HealthTag.IsValid() && FinalHealthDamage > 0.0f)
            {
                // Utilize your custom tag-bound map cache
                float CurrentHealth = TargetAttributes->GetDynamicAttributeValue(HealthTag);
                const_cast<UDynamicAttributeSet*>(TargetAttributes)->SetDynamicAttributeValue(HealthTag, CurrentHealth - FinalHealthDamage);
            }
        }
    }
}