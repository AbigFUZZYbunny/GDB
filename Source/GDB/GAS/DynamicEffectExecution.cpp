// DynamicEffectExecution.cpp
#include "DynamicEffectExecution.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicCharacter.h"
#include "DynamicForceComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGameState.h"
#include "DynamicCustomizationComponent.h"

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

    const FPhysicsCalculationSettings& CalcSettings = DynamicGS->GetCalculationSettings();
    const uint8 HitLimbId = DynContext->GetTargetLimbId();
    const FPhysicsLimbConfig* LimbData = DynamicGS->GetLimbData(HitLimbId);
    
    const FGameplayTag DamageTargetTag = (LimbData && LimbData->IntegrityAttributeTag.IsValid())
        ? LimbData->IntegrityAttributeTag
        : CalcSettings.PrimaryDamageTargetAttributeTag;

    bool bInvertHealing = false;
    if (const UDynamicCustomizationComponent* TargetCust = TargetActor->FindComponentByClass<UDynamicCustomizationComponent>())
    {
        bInvertHealing = TargetCust->DoesCharacterInvertHealingToDamage();
    }

    float EvaluatedDamage = 0.0f;

    // 2. Authoritative Kinetic & Environmental Physics Calculation
    if (DynContext->GetKineticImpulse() > 0.0f)
    {
        if (const FDynamicImpactReactionRule* ImpactRule = CalcSettings.ImpactReactionRules.Find(DynContext->ContextPayloadTag))
        {
            // --- ENVIRONMENTAL IMPACT PIPELINE ---
            float TotalVelocityDamping = 0.0f;
            for (const auto& Pair : ImpactRule->VelocityMitigationScalars.LocalLookupMap)
            {
                if (Pair.Key.IsValid())
                {
                    TotalVelocityDamping += TargetAttributes->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                    if (HitLimbId > 0) TotalVelocityDamping += TargetAttributes->GetDynamicAttributeValue(Pair.Key, HitLimbId) * Pair.Value;
                }
            }

            const float EffectiveVelocity = FMath::Max(0.0f, DynContext->GetKineticImpulse() - TotalVelocityDamping);
            const float ExcessVelocity = EffectiveVelocity - (ImpactRule->BaseImpactVelocityThreshold / FMath::Max(1.0f, DynContext->GetMassRatioFactor()));

            if (ExcessVelocity > 0.0f)
            {
                float TotalDamageAbsorption = 0.0f;
                for (const auto& Pair : ImpactRule->DamageMitigationScalars.LocalLookupMap)
                {
                    if (Pair.Key.IsValid())
                    {
                        TotalDamageAbsorption += TargetAttributes->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                        if (HitLimbId > 0) TotalDamageAbsorption += TargetAttributes->GetDynamicAttributeValue(Pair.Key, HitLimbId) * Pair.Value;
                    }
                }

                const float RawUnmitigatedDamage = ExcessVelocity * DynContext->GetMassRatioFactor();
                const float MitigatedDamage = RawUnmitigatedDamage * FMath::Clamp(1.0f - TotalDamageAbsorption, 0.05f, 5.0f);
                EvaluatedDamage = MitigatedDamage;

                // Apply Modular Consequences
                for (const FDynamicImpactConsequence& Conseq : ImpactRule->Consequences)
                {
                    if (!Conseq.TargetAttributeTag.IsValid()) continue;
                    
                    float AppliedDamage = MitigatedDamage * Conseq.DamageScalar;
                    const uint8 TargetLimb = Conseq.bIsLimbSpecific ? HitLimbId : 0;
                    const float CurVal = TargetAttributes->GetDynamicAttributeValue(Conseq.TargetAttributeTag, TargetLimb);
                    TargetAttributes->SetDynamicAttributeValue(Conseq.TargetAttributeTag, FMath::Max(0.0f, CurVal - AppliedDamage), TargetLimb);

                    if (AppliedDamage >= Conseq.StatusEffectThreshold)
                    {
                        for (const FGameplayTag& EffectTag : Conseq.StatusEffectsToApply)
                        {
                            TargetASC->AddLooseGameplayTags(FGameplayTagContainer(EffectTag));
                        }
                    }
                }

                // Dynamic Stability/Grip Retention
                float StabilityBonus = 0.0f;
                for (const auto& Pair : ImpactRule->StabilityRetentionScalars.LocalLookupMap)
                {
                    if (Pair.Key.IsValid())
                    {
                        StabilityBonus += TargetAttributes->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                        if (HitLimbId > 0) StabilityBonus += TargetAttributes->GetDynamicAttributeValue(Pair.Key, HitLimbId) * Pair.Value;
                    }
                }

                const float EffectiveStabilityThreshold = (ImpactRule->BaseStabilityLossThreshold + StabilityBonus) / DynContext->GetMassRatioFactor();
                if (MitigatedDamage > EffectiveStabilityThreshold || (LimbData && MitigatedDamage >= LimbData->BreakRagdollThreshold))
                {
                    if (ImpactRule->bTriggersRagdollOnInstability)
                    {
                        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(TargetActor))
                        {
                            DynChar->StartRagdoll();
                            if (UDynamicForceComponent* ForceComp = DynChar->FindComponentByClass<UDynamicForceComponent>())
                            {
                                const FVector ReboundDirection = (DynContext->QuantizedImpactNormal + FVector::UpVector * 0.35f).GetSafeNormal();
                                ForceComp->QueueInstantMovementImpulse(ReboundDirection, MitigatedDamage * 25.0f);
                            }
                        }
                    }
                }
            }
        }
        else if (DamageTargetTag.IsValid())
        {
            // --- STANDARD KINETIC WEAPON PIPELINE ---
            const float LimbDamageMult = LimbData ? LimbData->DamageMultiplier : 1.0f;
            const float BaseKineticDamage = DynContext->GetKineticImpulse() * CalcSettings.ImpulseToDamageScalar * DynContext->GetMassRatioFactor();
            EvaluatedDamage = BaseKineticDamage * LimbDamageMult;

            if (EvaluatedDamage > 0.0f)
            {
                if (HitLimbId > 0)
                {
                    const float CurLimbVal = TargetAttributes->GetDynamicAttributeValue(DamageTargetTag, HitLimbId);
                    TargetAttributes->SetDynamicAttributeValue(DamageTargetTag, FMath::Max(0.0f, CurLimbVal - EvaluatedDamage), HitLimbId);
                }
                const float CurGlobalVal = TargetAttributes->GetDynamicAttributeValue(DamageTargetTag, 0);
                TargetAttributes->SetDynamicAttributeValue(DamageTargetTag, FMath::Max(0.0f, CurGlobalVal - EvaluatedDamage), 0);
            }
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

                float EffectiveBaseMag = ModDef.BaseMagnitude;
                
                if (bInvertHealing && DamageTargetTag.IsValid() && ModDef.TargetAttributeTag == DamageTargetTag && 
                    ModDef.ModifierOp == EEffectModifierOp::Additive && EffectiveBaseMag > 0.0f)
                {
                    EffectiveBaseMag = -EffectiveBaseMag;
                }

                const float CurrentVal = TargetAttributes->GetDynamicAttributeValue(ModDef.TargetAttributeTag, 0);
                float CalculatedVal = CurrentVal;

                switch (ModDef.ModifierOp)
                {
                    case EEffectModifierOp::Additive:
                        CalculatedVal = CurrentVal + EffectiveBaseMag;
                        break;
                    case EEffectModifierOp::Multiplicative:
                        CalculatedVal = CurrentVal * (1.0f + EffectiveBaseMag);
                        break;
                    case EEffectModifierOp::Override:
                        CalculatedVal = EffectiveBaseMag;
                        break;
                }

                if (DamageTargetTag.IsValid() && ModDef.TargetAttributeTag == DamageTargetTag && ModDef.ModifierOp == EEffectModifierOp::Additive && EffectiveBaseMag < 0.0f)
                {
                    const float ModDamage = FMath::Abs(EffectiveBaseMag);
                    EvaluatedDamage += ModDamage;
                    
                    if (HitLimbId > 0)
                    {
                        const float CurLimbVal = TargetAttributes->GetDynamicAttributeValue(DamageTargetTag, HitLimbId);
                        TargetAttributes->SetDynamicAttributeValue(DamageTargetTag, FMath::Max(0.0f, CurLimbVal - ModDamage), HitLimbId);
                    }
                }

                TargetAttributes->SetDynamicAttributeValue(ModDef.TargetAttributeTag, CalculatedVal, 0);
            }
        }
    }

    // 4. Source Pass-Back Execution (XP / Skill XP on Damage Dealt)
    if (EvaluatedDamage > 0.0f && SourceASC && DynContext->SourcePassBackRules.Num() > 0)
    {
        if (UDynamicAttributeSet* SourceAttributes = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(SourceASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
        {
            for (const FSourcePassBackRule& Rule : DynContext->SourcePassBackRules)
            {
                if (Rule.TriggerEvent == EPassBackTriggerEvent::OnDamageDealt && Rule.SourceAttributeTag.IsValid())
                {
                    const float GrantAmount = Rule.BaseGrantAmount + (Rule.bScaleWithDamageDealt ? (EvaluatedDamage * Rule.DamageRatioScalar) : 0.0f);
                    const float OldSourceVal = SourceAttributes->GetDynamicAttributeValue(Rule.SourceAttributeTag, 0);
                    SourceAttributes->SetDynamicAttributeValue(Rule.SourceAttributeTag, OldSourceVal + GrantAmount, 0);
                }
            }
        }
    }
}