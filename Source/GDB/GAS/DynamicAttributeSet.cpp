// DynamicAttributeSet.cpp
#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"

UDynamicAttributeSet::UDynamicAttributeSet()
{
    FastAttributes.OwningAttributeSet = this;
}

void UDynamicAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicAttributeSet, FastAttributes);
}

void UDynamicAttributeSet::OnRep_FastAttributes()
{
    FastLookupMap.Empty();
    for (const FDynamicAttributeItem& Item : FastAttributes.Items)
    {
        if (Item.AttributeTag.IsValid())
        {
            FastLookupMap.Add(Item.AttributeTag, Item.Value);
            OnDynamicAttributeChanged.Broadcast(Item.AttributeTag, Item.Value);
        }
    }
}

float UDynamicAttributeSet::GetDynamicAttributeValue(FGameplayTag AttributeTag) const
{
    return FastLookupMap.FindRef(AttributeTag);
}

void FDynamicAttributeItem::PreReplicatedRemove(const FFastAttributeArray& InArrayOwner) {}

void FDynamicAttributeItem::PostReplicatedAdd(const FFastAttributeArray& InArrayOwner)
{
    if (InArrayOwner.OwningAttributeSet)
    {
        const_cast<UDynamicAttributeSet*>(InArrayOwner.OwningAttributeSet)->OnRep_FastAttributes();
    }
}

void FDynamicAttributeItem::PostReplicatedChange(const FFastAttributeArray& InArrayOwner)
{
    if (InArrayOwner.OwningAttributeSet)
    {
        const_cast<UDynamicAttributeSet*>(InArrayOwner.OwningAttributeSet)->OnRep_FastAttributes();
    }
}

void UDynamicAttributeSet::SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal)
{
    AActor* OwningActor = GetOwningActor();
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    float OldVal = FastLookupMap.FindRef(AttributeTag);
    float Delta = NewVal - OldVal;

    // Progression Multipliers
    if (GS && !FMath::IsNearlyZero(Delta))
    {
        if (FDynamicThresholdContainer* Container = GS->MasterThresholdRegistry.Find(AttributeTag))
        {
            for (const FDynamicThresholdRule& Rule : Container->Rules)
            {
                if (Rule.MonitoredAttributeTag == AttributeTag)
                {
                    if (Delta > 0.0f && Rule.GainModifierAttributeTag.IsValid())
                    {
                        Delta *= (1.0f + GetDynamicAttributeValue(Rule.GainModifierAttributeTag));
                    }
                    else if (Delta < 0.0f && Rule.LossModifierAttributeTag.IsValid())
                    {
                        Delta *= (1.0f + GetDynamicAttributeValue(Rule.LossModifierAttributeTag));
                    }
                    NewVal = OldVal + Delta;
                    break;
                }
            }
        }
    }

    FastLookupMap.Add(AttributeTag, NewVal);

    int32 FoundIdx = FastAttributes.Items.IndexOfByPredicate([AttributeTag](const FDynamicAttributeItem& Element) {
        return Element.AttributeTag == AttributeTag;
    });

    if (FoundIdx != INDEX_NONE)
    {
        FastAttributes.Items[FoundIdx].Value = NewVal;
        FastAttributes.MarkItemDirty(FastAttributes.Items[FoundIdx]);
    }
    else
    {
        FDynamicAttributeItem NewItem;
        NewItem.AttributeTag = AttributeTag;
        NewItem.Value = NewVal;
        FastAttributes.Items.Add(NewItem);
        FastAttributes.MarkArrayDirty();
    }

    OnDynamicAttributeChanged.Broadcast(AttributeTag, NewVal);

    if (NewVal > OldVal && OwningActor)
    {
        EvaluateMilestoneIncreases(AttributeTag, OldVal, NewVal, OwningActor);
    }

    // Threshold breakthrough / Level up checks
    if (GS)
    {
        if (FDynamicThresholdContainer* Container = GS->MasterThresholdRegistry.Find(AttributeTag))
        {
            for (const FDynamicThresholdRule& Rule : Container->Rules)
            {
                float MaxCapValue = GetDynamicAttributeValue(Rule.MaxThresholdAttributeTag);
                if (NewVal >= MaxCapValue && MaxCapValue > 0.0f)
                {
                    if (Rule.TriggerBehavior == EThresholdTriggerType::LevelUpAttribute)
                    {
                        SetDynamicAttributeValue(Rule.MonitoredAttributeTag, 0.0f);
                        SetDynamicAttributeValue(Rule.TargetEvolvedAttributeTag, GetDynamicAttributeValue(Rule.TargetEvolvedAttributeTag) + 1.0f);

                        if (UAbilitySystemComponent* ASC = OwningActor ? OwningActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr)
                        {
                            FGameplayEventData Payload;
                            Payload.EventTag = Rule.TargetEvolvedAttributeTag;
                            ASC->HandleGameplayEvent(FGameplayTag::RequestGameplayTag("Event.Trait.Evolved", false), &Payload);
                        }
                    }
                }
            }
        }
    }
}

void UDynamicAttributeSet::EvaluateMilestoneIncreases(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor)
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    UAbilitySystemComponent* TargetASC = TargetActor ? TargetActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    if (!GS || !TargetASC) return;

    int32 OldInt = FMath::FloorToInt(OldValue);
    int32 NewInt = FMath::FloorToInt(NewValue);

    if (FDynamicMilestoneContainer* Container = GS->MasterMilestoneRegistry.Find(AttributeTag))
    {
        FGameplayTagContainer SourceTags;
        FGameplayTagContainer TargetTags;
        TargetASC->GetOwnedGameplayTags(TargetTags);

        for (const FDynamicMilestoneAction& Action : Container->MilestoneActions)
        {
            if (Action.ExecutionTriggerConditionTag.IsValid())
            {
                const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(TargetActor, Action.ExecutionTriggerConditionTag.GetTagName());
                if (ConditionTree && !UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, TargetASC, TargetASC, SourceTags, TargetTags))
                {
                    continue;
                }
            }

            for (int32 CheckedLevel = OldInt + 1; CheckedLevel <= NewInt; ++CheckedLevel)
            {
                int32 TargetMilestoneKey = 5;
                bool bShouldTrigger = Action.bIsIntervalRepeating ? (CheckedLevel % TargetMilestoneKey == 0) : (CheckedLevel == TargetMilestoneKey);
                if (bShouldTrigger)
                {
                    for (const FDynamicModifierDef& ModDef : Action.EffectsToApply)
                    {
                        SetDynamicAttributeValue(ModDef.TargetAttributeTag, GetDynamicAttributeValue(ModDef.TargetAttributeTag) + ModDef.BaseMagnitude);
                    }
                }
            }
        }
    }
}