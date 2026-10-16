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

    UWorld* World = TargetASC->GetWorld();
    ADynamicGameState* DynamicGS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!DynamicGS) return;

    // 1. Flattened Dynamic Condition Tree Evaluation
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

    UDynamicAttributeSet* TargetAttributes = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())));
    if (!TargetAttributes) return;

    float EvaluatedHealthDamage = 0.0f;

    // 2. Authoritative Kinetic Physics Damage Calculation
    if (DynContext->GetKineticImpulse() > 0.0f)
    {
        const FPhysicsCalculationSettings& CalcSettings = DynamicGS->GetCalculationSettings();
        const FPhysicsLimbConfig* LimbData = DynamicGS->GetLimbData(DynContext->GetTargetLimbId());

        const float LimbDamageMult = LimbData ? LimbData->DamageMultiplier : 1.0f;
        const float BaseKineticDamage = DynContext->GetKineticImpulse() * CalcSettings.ImpulseToDamageScalar * DynContext->GetMassRatioFactor();
        EvaluatedHealthDamage = BaseKineticDamage * LimbDamageMult;

        FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag("Attribute.Stat.Health", false);
        if (HealthTag.IsValid() && EvaluatedHealthDamage > 0.0f)
        {
            float CurrentHealth = TargetAttributes->GetDynamicAttributeValue(HealthTag);
            TargetAttributes->SetDynamicAttributeValue(HealthTag, CurrentHealth - EvaluatedHealthDamage);
        }
    }

    // 3. Dynamic Modifiers Evaluation from Effect Definition
    if (DynContext->ContextPayloadTag.IsValid())
    {
        if (const FDynamicEffectDef* EffectDef = DynamicGS->MasterEffectRegistry.Find(DynContext->ContextPayloadTag))
        {
            for (const FDynamicModifierDef& ModDef : EffectDef->Modifiers)
            {
                if (!ModDef.TargetAttributeTag.IsValid()) continue;

                float CurrentVal = TargetAttributes->GetDynamicAttributeValue(ModDef.TargetAttributeTag);
                float CalculatedVal = CurrentVal;

                switch (ModDef.ModifierOp)
                {
                    case EEffectModifierOp::Additive:
                        CalculatedVal = CurrentVal + ModDef.BaseMagnitude;
                        break;
                    case EEffectModifierOp::Multiplicative:
                        CalculatedVal = CurrentVal * (1.0f + ModDef.BaseMagnitude);
                        break;
                    case EEffectModifierOp::Override:
                        CalculatedVal = ModDef.BaseMagnitude;
                        break;
                }

                if (ModDef.TargetAttributeTag == FGameplayTag::RequestGameplayTag("Attribute.Stat.Health", false) && ModDef.ModifierOp == EEffectModifierOp::Additive && ModDef.BaseMagnitude < 0.0f)
                {
                    EvaluatedHealthDamage += FMath::Abs(ModDef.BaseMagnitude);
                }

                TargetAttributes->SetDynamicAttributeValue(ModDef.TargetAttributeTag, CalculatedVal);
            }
        }
    }

    // 4. Source Pass-Back Execution (XP / Skill XP on Damage Dealt)
    if (EvaluatedHealthDamage > 0.0f && SourceASC && DynContext->SourcePassBackRules.Num() > 0)
    {
        if (UDynamicAttributeSet* SourceAttributes = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(SourceASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
        {
            for (const FSourcePassBackRule& Rule : DynContext->SourcePassBackRules)
            {
                if (Rule.TriggerEvent == EPassBackTriggerEvent::OnDamageDealt && Rule.SourceAttributeTag.IsValid())
                {
                    const float GrantAmount = Rule.BaseGrantAmount + (Rule.bScaleWithDamageDealt ? (EvaluatedHealthDamage * Rule.DamageRatioScalar) : 0.0f);
                    const float OldSourceVal = SourceAttributes->GetDynamicAttributeValue(Rule.SourceAttributeTag);
                    SourceAttributes->SetDynamicAttributeValue(Rule.SourceAttributeTag, OldSourceVal + GrantAmount);
                }
            }
        }
    }
}