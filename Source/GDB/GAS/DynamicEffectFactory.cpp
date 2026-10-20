// DynamicEffectFactory.cpp
#include "DynamicEffectFactory.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicEffectExecution.h"
#include "GameplayEffectComponent.h"

UGameplayEffect* UDynamicEffectFactory::ConstructGameplayEffectObject(UObject* Outer, const FDynamicEffectDef& FoundDef, FGameplayTag EffectTag)
{
    UGameplayEffect* NewEffect = NewObject<UGameplayEffect>(Outer, FName(*EffectTag.ToString().Replace(TEXT("."), TEXT("_"))));

    // 1. Core Timers and Lifecycle
    switch (FoundDef.DurationPolicy)
    {
        case EEffectDurationPolicy::Instant:     NewEffect->DurationPolicy = EGameplayEffectDurationType::Instant; break;
        case EEffectDurationPolicy::Infinite:    NewEffect->DurationPolicy = EGameplayEffectDurationType::Infinite; break;
        case EEffectDurationPolicy::HasDuration: NewEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration; break;
    }
    NewEffect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(FoundDef.DurationMagnitude));
    NewEffect->Period = FScalableFloat(FoundDef.Period);

    // 2. Reflected Stacking Component Setup
    if (FoundDef.StackingPolicy != EGameplayEffectStackingPolicy::None)
    {
        UClass* StackingCompClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.TargetingOrSourceEffectsStackingGEComponent"));
        if (!StackingCompClass)
        {
            StackingCompClass = FindFirstObject<UClass>(TEXT("TargetingOrSourceEffectsStackingGEComponent"), EFindFirstObjectOptions::None);
        }

        if (StackingCompClass)
        {
            UGameplayEffectComponent& GenericCompRef = NewEffect->FindOrAddComponent<UGameplayEffectComponent>();
            UGameplayEffectComponent* StackingComponent = &GenericCompRef;

            EGameplayEffectStackingType TargetStackingType = (FoundDef.StackingPolicy == EGameplayEffectStackingPolicy::AggregateBySource)
                ? EGameplayEffectStackingType::AggregateBySource
                : EGameplayEffectStackingType::AggregateByTarget;

            if (FByteProperty* StackingTypeProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackingType"))))
            {
                StackingTypeProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(TargetStackingType));
            }
            if (FIntProperty* StackLimitProp = CastField<FIntProperty>(StackingCompClass->FindPropertyByName(TEXT("StackLimitCount"))))
            {
                StackLimitProp->SetPropertyValue_InContainer(StackingComponent, FoundDef.MaxStackCount);
            }
            if (FByteProperty* ExpirationProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackExpirationPolicy"))))
            {
                ExpirationProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(FoundDef.StackExpirationPolicy));
            }
        }
    }

    // 3. Native Execution Bridge
    FGameplayEffectExecutionDefinition ExecDef;
    ExecDef.CalculationClass = UDynamicEffectExecution::StaticClass();
    NewEffect->Executions.Add(ExecDef);

    // 4. Visual Cue Assignment
    for (const FGameplayTag& CueTag : FoundDef.GameplayCues)
    {
        FGameplayEffectCue CueInfo;
        CueInfo.GameplayCueTags.AddTag(CueTag);
        NewEffect->GameplayCues.Add(CueInfo);
    }

    return NewEffect;
}

FGameplayEffectSpecHandle UDynamicEffectFactory::CreateDynamicEffectSpec(UAbilitySystemComponent* SourceASC, FGameplayTag EffectDataTag, int32 Level)
{
    if (!SourceASC || !EffectDataTag.IsValid()) return FGameplayEffectSpecHandle();

    UWorld* World = SourceASC->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayEffectSpecHandle();

    // O(1) Cache Lookup - Prevents Allocating UGameplayEffect UObjects per cast/tick
    UGameplayEffect* BaseEffect = GS->GetOrCreateCachedEffectObject(EffectDataTag);
    if (!BaseEffect) return FGameplayEffectSpecHandle();

    const FDynamicEffectDef* FoundDef = GS->MasterEffectRegistry.Find(EffectDataTag);
    if (!FoundDef) return FGameplayEffectSpecHandle();

    FDynamicGameplayEffectContext* CustomContext = new FDynamicGameplayEffectContext();
    CustomContext->CustomConditionTreeName = FoundDef->ConditionTreeLinkName;
    CustomContext->ContextPayloadTag = EffectDataTag;
    CustomContext->SourcePassBackRules = FoundDef->SourcePassBackRules;
    CustomContext->AddInstigator(SourceASC->GetOwner(), SourceASC->GetOwner());

    FGameplayEffectContextHandle ContextHandle(CustomContext);
    FGameplayEffectSpec* NewRawSpec = new FGameplayEffectSpec(BaseEffect, ContextHandle, Level);
    return FGameplayEffectSpecHandle(NewRawSpec);
}