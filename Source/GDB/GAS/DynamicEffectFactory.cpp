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

    // OPTIMIZATION: Resolves zero-allocation generic pre-warmed effects based purely on structural duration policy
    UGameplayEffect* BaseEffect = nullptr;
    switch (FoundDef->DurationPolicy)
    {
    case EEffectDurationPolicy::Instant:     BaseEffect = GS->GenericInstantEffect; break;
    case EEffectDurationPolicy::HasDuration: BaseEffect = GS->GenericDurationEffect; break;
    case EEffectDurationPolicy::Infinite:    BaseEffect = GS->GenericInfiniteEffect; break;
    }
    if (!BaseEffect) return FGameplayEffectSpecHandle();

    FDynamicGameplayEffectContext* CustomContext = new FDynamicGameplayEffectContext();
    CustomContext->CustomConditionTreeName = FoundDef->ConditionTreeLinkName;
    CustomContext->ContextPayloadTag = EffectDataTag;
    CustomContext->SourcePassBackRules = FoundDef->SourcePassBackRules;
    CustomContext->AddInstigator(SourceASC->GetOwner(), SourceASC->GetOwner());

    FGameplayEffectContextHandle ContextHandle(CustomContext);
    FGameplayEffectSpec* NewRawSpec = new FGameplayEffectSpec(BaseEffect, ContextHandle, Level);
    
    // Map magnitudes dynamically onto the spec
    if (FoundDef->DurationPolicy == EEffectDurationPolicy::HasDuration)
    {
        NewRawSpec->SetDuration(FoundDef->DurationMagnitude, true);
    }
    if (FoundDef->Period > 0.0f)
    {
        NewRawSpec->Period = FoundDef->Period;
    }
    return FGameplayEffectSpecHandle(NewRawSpec);
}