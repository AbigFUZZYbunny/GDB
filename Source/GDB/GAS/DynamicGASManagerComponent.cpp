// DynamicGASManagerComponent.cpp
#include "DynamicGASManagerComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayAbility.h"
#include "DynamicAttributeSet.h"
#include "AbilitySystemComponent.h"

UDynamicGASManagerComponent::UDynamicGASManagerComponent() {}

float UDynamicGASManagerComponent::ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const
{
    float CombinedValue = 0.0f;
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicDeliveryArchetype* BaseArch = GS->DeliveryArchetypes.Find(ConfigProfileTag))
            {
                CombinedValue = BaseArch->Metrics.LocalLookupMap.FindRef(MetricTag);
            }
        }
    }
    if (const float* ModifierVal = Modifiers.LocalLookupMap.Find(MetricTag))
    {
        CombinedValue += *ModifierVal;
    }
    return CombinedValue;
}

const FDynamicConditionTreeContainer* UDynamicGASManagerComponent::GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName)
{
    if (!WorldContextObject || ConditionName.IsNone()) return nullptr;
    if (UWorld* World = WorldContextObject->GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            return GS->SharedConditionTrees.Find(ConditionName);
        }
    }
    return nullptr;
}

bool UDynamicGASManagerComponent::CanLearnAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const
{
    if (!TargetASC || !AbilityTag.IsValid()) return false;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicAbilityDef* AbilityDef = GS->AbilityMasterRegistry.Find(AbilityTag);
    if (!AbilityDef || !GS->GetComputedAbilityScore(AbilityTag, OutScoreInfo))
    {
        return false;
    }

    const UDynamicAttributeSet* AttrSet = Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!AttrSet) return false;

    if (AbilityDef->RequiredMasteryAttributeTag.IsValid())
    {
        const float CurrentMastery = AttrSet->GetDynamicAttributeValue(AbilityDef->RequiredMasteryAttributeTag, 0);
        if (CurrentMastery < OutScoreInfo.MinimumMasteryRequired)
        {
            return false;
        }
    }

    if (AbilityDef->RequiredMasteryPointsAttributeTag.IsValid())
    {
        const float AvailablePoints = AttrSet->GetDynamicAttributeValue(AbilityDef->RequiredMasteryPointsAttributeTag, 0);
        if (AvailablePoints < OutScoreInfo.LearnPointCost)
        {
            return false;
        }
    }

    return true;
}

FGameplayAbilitySpecHandle UDynamicGASManagerComponent::TryLearnAndGrantAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FGameplayTag InputSlotTag)
{
    if (!TargetASC || !TargetASC->IsOwnerActorAuthoritative())
    {
        return FGameplayAbilitySpecHandle();
    }

    FComputedAbilityScoreInfo ScoreInfo;
    if (!CanLearnAbility(TargetASC, AbilityTag, ScoreInfo))
    {
        return FGameplayAbilitySpecHandle();
    }

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    const FDynamicAbilityDef* AbilityDef = GS->AbilityMasterRegistry.Find(AbilityTag);

    if (AbilityDef->RequiredMasteryPointsAttributeTag.IsValid())
    {
        if (UDynamicAttributeSet* MutableAttr = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
        {
            const float CurPoints = MutableAttr->GetDynamicAttributeValue(AbilityDef->RequiredMasteryPointsAttributeTag, 0);
            MutableAttr->SetDynamicAttributeValue(AbilityDef->RequiredMasteryPointsAttributeTag, CurPoints - ScoreInfo.LearnPointCost, 0);
        }
    }

    FGameplayAbilitySpec Spec(UDynamicGameplayAbility::StaticClass(), ScoreInfo.ComputedSpellLevel, INDEX_NONE, this);
    
    if (InputSlotTag.IsValid())
    {
        Spec.GetDynamicSpecSourceTags().AddTag(InputSlotTag);
    }
    Spec.GetDynamicSpecSourceTags().AddTag(AbilityTag);

    FGameplayAbilitySpecHandle SpecHandle = TargetASC->GiveAbility(Spec);
    if (FGameplayAbilitySpec* GrantedSpec = TargetASC->FindAbilitySpecFromHandle(SpecHandle))
    {
        if (UDynamicGameplayAbility* InstancedAbility = Cast<UDynamicGameplayAbility>(GrantedSpec->GetPrimaryInstance()))
        {
            InstancedAbility->ActiveAbilityRegistryTag = AbilityTag;
            InstancedAbility->AbilityConfig = *AbilityDef;
        }
    }

    return SpecHandle;
}

bool UDynamicGASManagerComponent::TriggerAbilityOrCombo(UAbilitySystemComponent* TargetASC, FGameplayTag RequestedAbilityTag)
{
    if (!TargetASC || !RequestedAbilityTag.IsValid()) return false;

    for (const FGameplayAbilitySpec& Spec : TargetASC->GetActivatableAbilities())
    {
        if (UDynamicGameplayAbility* ActiveInstance = Cast<UDynamicGameplayAbility>(Spec.GetPrimaryInstance()))
        {
            if (ActiveInstance->IsActive() && ActiveInstance->IsComboWindowOpenForAbility(RequestedAbilityTag))
            {
                return ActiveInstance->TryTriggerComboTransition(RequestedAbilityTag);
            }
        }
    }

    for (const FGameplayAbilitySpec& Spec : TargetASC->GetActivatableAbilities())
    {
        if (Spec.GetDynamicSpecSourceTags().HasTagExact(RequestedAbilityTag))
        {
            return TargetASC->TryActivateAbility(Spec.Handle);
        }
    }

    return false;
}