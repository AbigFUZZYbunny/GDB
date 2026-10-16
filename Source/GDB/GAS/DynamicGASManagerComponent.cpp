// DynamicGASManagerComponent.cpp
#include "DynamicGASManagerComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayAbility.h"
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

FGameplayAbilitySpecHandle UDynamicGASManagerComponent::GrantChainedAbilitySpec(UAbilitySystemComponent* TargetASC, const FDynamicAbilitySlotChain& SlotChain)
{
    if (!TargetASC || !TargetASC->IsOwnerActorAuthoritative() || SlotChain.ChainedAbilityTags.Num() == 0)
    {
        return FGameplayAbilitySpecHandle();
    }

    FGameplayAbilitySpec Spec(UDynamicGameplayAbility::StaticClass(), 1, INDEX_NONE, this);
    Spec.GetDynamicSpecSourceTags().AddTag(SlotChain.SlotInputTag);

    FGameplayAbilitySpecHandle SpecHandle = TargetASC->GiveAbility(Spec);
    if (FGameplayAbilitySpec* GrantedSpec = TargetASC->FindAbilitySpecFromHandle(SpecHandle))
    {
        if (UDynamicGameplayAbility* InstancedAbility = Cast<UDynamicGameplayAbility>(GrantedSpec->GetPrimaryInstance()))
        {
            InstancedAbility->SetCustomSpecChain(SlotChain.ChainedAbilityTags);
        }
    }
    return SpecHandle;
}