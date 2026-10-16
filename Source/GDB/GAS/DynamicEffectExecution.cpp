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

    float FinalHealthDamage = 0.0f;

    if (DynContext->GetKineticImpulse() > 0.0f)
    {
        UWorld* World = TargetASC->GetWorld();
        if (ADynamicGameState* DynamicGS = World ? World->GetGameState<ADynamicGameState>() : nullptr)
        {
            const FPhysicsCalculationSettings& CalcSettings = DynamicGS->GetCalculationSettings();
            const FPhysicsLimbConfig* LimbData = DynamicGS->GetLimbData(DynContext->GetTargetLimbId());

            const float LimbDamageMult = LimbData ? LimbData->DamageMultiplier : 1.0f;
            const float BaseKineticDamage = DynContext->GetKineticImpulse() * CalcSettings.ImpulseToDamageScalar * DynContext->GetMassRatioFactor();
            FinalHealthDamage = BaseKineticDamage * LimbDamageMult;

            FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag("Attribute.Stat.Health", false);
            if (HealthTag.IsValid() && FinalHealthDamage > 0.0f)
            {
                float CurrentHealth = TargetAttributes->GetDynamicAttributeValue(HealthTag);
                const_cast<UDynamicAttributeSet*>(TargetAttributes)->SetDynamicAttributeValue(HealthTag, CurrentHealth - FinalHealthDamage);
            }
        }
    }

    // --- SOURCE PASS-BACK EXECUTION (XP / Skill XP / Mastery XP on Damage Dealt) ---
    if (FinalHealthDamage > 0.0f && SourceASC && DynContext->SourcePassBackRules.Num() > 0)
    {
        if (const UDynamicAttributeSet* SourceAttributes = Cast<UDynamicAttributeSet>(SourceASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())))
        {
            for (const FSourcePassBackRule& Rule : DynContext->SourcePassBackRules)
            {
                if (Rule.TriggerEvent == EPassBackTriggerEvent::OnDamageDealt && Rule.SourceAttributeTag.IsValid())
                {
                    const float GrantAmount = Rule.BaseGrantAmount + (Rule.bScaleWithDamageDealt ? (FinalHealthDamage * Rule.DamageRatioScalar) : 0.0f);
                    const float OldSourceVal = SourceAttributes->GetDynamicAttributeValue(Rule.SourceAttributeTag);
                    // SetDynamicAttributeValue automatically triggers your Threshold & Level-Up Breakthrough rules!
                    const_cast<UDynamicAttributeSet*>(SourceAttributes)->SetDynamicAttributeValue(Rule.SourceAttributeTag, OldSourceVal + GrantAmount);
                }
            }
        }
    }
}