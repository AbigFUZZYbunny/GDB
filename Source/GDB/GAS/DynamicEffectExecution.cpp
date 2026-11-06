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

void UDynamicEffectExecution::Execute_Implementation(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams, 
    FGameplayEffectCustomExecutionOutput& ExecutionOutput) const
{
    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    FDynamicGameplayEffectContext* DynContext = static_cast<FDynamicGameplayEffectContext*>(Spec.GetContext().Get());
    if (!DynContext) return;

    UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
    UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
    if (!TargetASC) return;

    AActor* TargetActor = TargetASC->GetOwner();
    if (!TargetActor) return;

    AActor* InstigatorActor = DynContext->GetInstigator();

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
    float HighestTargetImpulse = 0.0f;
    float HighestRecoilImpulse = 0.0f;

    // 2. Authoritative Kinetic & Environmental Physics Calculation
    if (DynContext->GetKineticImpulse() > 0.0f && !DynContext->ContextPayloadTags.IsEmpty())
    {
        float HighestMitigatedDamage = 0.0f;
        const float WeaponMomentum = DynContext->GetKineticImpulse() * DynContext->GetMassRatioFactor();

        for (const FGameplayTag& ImpactTag : DynContext->ContextPayloadTags)
        {
            if (const FDynamicImpactReactionRule* ImpactRule = CalcSettings.ImpactReactionRules.Find(ImpactTag))
            {
                // Velocity Mitigations (Armor, Agility, Resistance)
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
                    // Damage Mitigations
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
                    HighestMitigatedDamage = FMath::Max(HighestMitigatedDamage, MitigatedDamage);

                    // Apply Data-Driven Status Consequences
                    for (const FDynamicImpactConsequence& Conseq : ImpactRule->Consequences)
                    {
                        if (!Conseq.TargetAttributeTag.IsValid()) continue;
                        
                        const float AppliedDamage = MitigatedDamage * Conseq.DamageScalar;
                        if (AppliedDamage >= Conseq.StatusEffectThreshold)
                        {
                            for (const FGameplayTag& EffectTag : Conseq.StatusEffectsToApply)
                            {
                                TargetASC->AddLooseGameplayTags(FGameplayTagContainer(EffectTag));
                            }
                        }
                    }

                    // Stability Evaluation
                    float StabilityBonus = 0.0f;
                    for (const auto& Pair : ImpactRule->StabilityRetentionScalars.LocalLookupMap)
                    {
                        if (Pair.Key.IsValid())
                        {
                            StabilityBonus += TargetAttributes->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                            if (HitLimbId > 0) StabilityBonus += TargetAttributes->GetDynamicAttributeValue(Pair.Key, HitLimbId) * Pair.Value;
                        }
                    }

                    const float StaggerThreshold = (ImpactRule->BaseStabilityLossThreshold + StabilityBonus) / FMath::Max(0.01f, DynContext->GetMassRatioFactor());
                    
                    // Stagger / Stumble / Ragdoll Resolution (Data-driven, no redundant threshold floats)
                    if (MitigatedDamage > StaggerThreshold)
                    {
                        bool bOverpoweredToRagdoll = false;

                        // Check Overpower Rules first
                        for (const FDynamicOverpowerRule& OverpowerRule : ImpactRule->OverpowerRules)
                        {
                            float ResistanceAccum = OverpowerRule.BasePenetrationThreshold;
                            for (const auto& Pair : OverpowerRule.ResistanceAttributeScalars.LocalLookupMap)
                            {
                                if (Pair.Key.IsValid())
                                {
                                    ResistanceAccum += TargetAttributes->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                                }
                            }

                            if (WeaponMomentum > ResistanceAccum && OverpowerRule.bForceRagdollOnOverpower)
                            {
                                bOverpoweredToRagdoll = true;
                                break;
                            }
                        }

                        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(TargetActor))
                        {
                            if (bOverpoweredToRagdoll || (LimbData && MitigatedDamage >= LimbData->BreakRagdollThreshold))
                            {
                                if (ImpactRule->bTriggersRagdollOnInstability)
                                {
                                    DynChar->StartRagdoll();
                                    if (UDynamicForceComponent* ForceComp = DynChar->FindComponentByClass<UDynamicForceComponent>())
                                    {
                                        const FVector ReboundDir = (DynContext->QuantizedImpactNormal + FVector::UpVector * 0.35f).GetSafeNormal();
                                        ForceComp->QueueInstantMovementImpulse(ReboundDir, MitigatedDamage * 25.0f);
                                    }
                                }
                            }
                            else
                            {
                                // Directional Stumble: Destabilized without full overpower
                                static const FGameplayTag StumbleTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Stumble"), false);
                                if (StumbleTag.IsValid())
                                {
                                    TargetASC->AddLooseGameplayTags(FGameplayTagContainer(StumbleTag));
                                }

                                if (UDynamicForceComponent* ForceComp = DynChar->FindComponentByClass<UDynamicForceComponent>())
                                {
                                    ForceComp->QueueInstantMovementImpulse(DynContext->QuantizedImpactNormal, MitigatedDamage * 12.0f);
                                }
                            }
                        }
                    }

                    // Newton Exchange Recoil Calculation
                    const float DefenderStaggerResist = TargetAttributes->GetDynamicAttributeValue(FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stat.Strength"), false), 0) * 10.0f;
                    const float SafeTotalPower = FMath::Max(1.0f, WeaponMomentum + DefenderStaggerResist);
                    const float TargetRatio = FMath::Clamp(WeaponMomentum / SafeTotalPower, 0.0f, 1.0f);

                    HighestTargetImpulse = FMath::Max(HighestTargetImpulse, WeaponMomentum * TargetRatio);
                    HighestRecoilImpulse = FMath::Max(HighestRecoilImpulse, (WeaponMomentum * (1.0f - TargetRatio)) * ImpactRule->BaseRecoilMultiplier);
                }
            }
        }

        EvaluatedDamage = HighestMitigatedDamage;
        if (EvaluatedDamage > 0.0f && DamageTargetTag.IsValid())
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
    else if (DynContext->GetKineticImpulse() > 0.0f && DamageTargetTag.IsValid())
    {
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

    // 3. Dynamic Modifiers Evaluation across all payload tags
    for (const FGameplayTag& PayloadTag : DynContext->ContextPayloadTags)
    {
        if (!PayloadTag.IsValid()) continue;

        if (const FDynamicEffectDef* EffectDef = DynamicGS->MasterEffectRegistry.Find(PayloadTag))
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

    // 4. Source Pass-Back Execution
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

    // 5. Dynamic Attacking Bone Physical Recoil
    HighestRecoilImpulse = FMath::Min(HighestRecoilImpulse, CalcSettings.MaxImpulseCap);
    if (HighestRecoilImpulse > CalcSettings.MinImpulseThreshold && InstigatorActor && InstigatorActor != TargetActor)
    {
        if (ADynamicCharacter* AttackerChar = Cast<ADynamicCharacter>(InstigatorActor))
        {
            if (UDynamicForceComponent* AttackerForceComp = AttackerChar->FindComponentByClass<UDynamicForceComponent>())
            {
                FName AttackingBone = DynContext->SourceEffectorBone;

                // Fallback to Character Template Handedness / Dominance profile if none specified
                if (AttackingBone.IsNone())
                {
                    if (const UDynamicCustomizationComponent* Cust = AttackerChar->GetCustomizationComponent())
                    {
                        FCharacterTemplateDef TempDef;
                        if (DynamicGS->GetCharacterTemplate(Cust->ActiveCharacterTemplateTag, TempDef))
                        {
                            AttackingBone = TempDef.DominanceProfile.MainHandBone;
                        }
                    }
                }

                if (AttackingBone.IsNone()) AttackingBone = FName("hand_r");

                const uint8 AttackerLimbId = DynamicGS->GetLimbIdForBone(AttackingBone);
                AttackerForceComp->ServerApplyLimbForce(
                    AttackerLimbId, 
                    AttackingBone, 
                    HighestRecoilImpulse, 
                    AttackerChar->GetActorLocation(), 
                    DynContext->QuantizedImpactNormal
                );
            }
        }
    }
}