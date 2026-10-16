// DynamicEffectFactory.cpp
#include "DynamicEffectFactory.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicEffectExecution.h"
#include "GameplayEffectComponent.h"

FGameplayEffectSpecHandle UDynamicEffectFactory::CreateDynamicEffectSpec(UAbilitySystemComponent* SourceASC, FGameplayTag EffectDataTag, int32 Level)
{
    if (!SourceASC || !EffectDataTag.IsValid()) return FGameplayEffectSpecHandle();
    UWorld* World = SourceASC->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayEffectSpecHandle();

    const FDynamicEffectDef* FoundDef = GS->MasterEffectRegistry.Find(EffectDataTag);
    if (!FoundDef) return FGameplayEffectSpecHandle();

    UGameplayEffect* TransientEffect = NewObject<UGameplayEffect>(GetTransientPackage(), UGameplayEffect::StaticClass());

    // 1. Core Timers and Lifecycle
    switch (FoundDef->DurationPolicy)
    {
        case EEffectDurationPolicy::Instant:     TransientEffect->DurationPolicy = EGameplayEffectDurationType::Instant; break;
        case EEffectDurationPolicy::Infinite:    TransientEffect->DurationPolicy = EGameplayEffectDurationType::Infinite; break;
        case EEffectDurationPolicy::HasDuration: TransientEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration; break;
    }
    TransientEffect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(FoundDef->DurationMagnitude));
    TransientEffect->Period = FScalableFloat(FoundDef->Period);

    // 2. Reflected Stacking Component Allocation
    if (FoundDef->StackingPolicy != EGameplayEffectStackingPolicy::None)
    {
        UClass* StackingCompClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.TargetingOrSourceEffectsStackingGEComponent"));
        if (!StackingCompClass) StackingCompClass = FindFirstObject<UClass>(TEXT("TargetingOrSourceEffectsStackingGEComponent"), EFindFirstObjectOptions::None);

        if (StackingCompClass)
        {
            UGameplayEffectComponent& GenericCompRef = TransientEffect->FindOrAddComponent<UGameplayEffectComponent>();
            UGameplayEffectComponent* StackingComponent = &GenericCompRef;

            EGameplayEffectStackingType TargetStackingType = (FoundDef->StackingPolicy == EGameplayEffectStackingPolicy::AggregateBySource)
                ? EGameplayEffectStackingType::AggregateBySource
                : EGameplayEffectStackingType::AggregateByTarget;

            if (FByteProperty* StackingTypeProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackingType"))))
                StackingTypeProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(TargetStackingType));
            if (FIntProperty* StackLimitProp = CastField<FIntProperty>(StackingCompClass->FindPropertyByName(TEXT("StackLimitCount"))))
                StackLimitProp->SetPropertyValue_InContainer(StackingComponent, FoundDef->MaxStackCount);
            if (FByteProperty* ExpirationProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackExpirationPolicy"))))
                ExpirationProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(FoundDef->StackExpirationPolicy));
        }
    }

    // 3. Execution Calculation Attachment (Bypasses invalid FindFieldChecked crashes on tag attributes)
    FGameplayEffectExecutionDefinition ExecDef;
    ExecDef.CalculationClass = UDynamicEffectExecution::StaticClass();
    TransientEffect->Executions.Add(ExecDef);

    // 4. Visual Cue Tracking
    for (const auto& CueTag : FoundDef->GameplayCues)
    {
        FGameplayEffectCue CueInfo;
        CueInfo.GameplayCueTags.AddTag(CueTag);
        TransientEffect->GameplayCues.Add(CueInfo);
    }

    // 5. Context Setup & Spec Creation
    FDynamicGameplayEffectContext* CustomContext = new FDynamicGameplayEffectContext();
    CustomContext->CustomConditionTreeName = FoundDef->ConditionTreeLinkName;
    CustomContext->ContextPayloadTag = EffectDataTag;
    CustomContext->SourcePassBackRules = FoundDef->SourcePassBackRules;
    CustomContext->AddInstigator(SourceASC->GetOwner(), SourceASC->GetOwner());

    FGameplayEffectContextHandle ContextHandle(CustomContext);
    FGameplayEffectSpec* NewRawSpec = new FGameplayEffectSpec(TransientEffect, ContextHandle, Level);
    return FGameplayEffectSpecHandle(NewRawSpec);
}