// DynamicAttributeSet.cpp
#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"

#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"

UDynamicAttributeSet::UDynamicAttributeSet()
{
    // Establish the backward link so item changes can find the lookup cache
    FastAttributes.OwningAttributeSet = this;
}

void UDynamicAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicAttributeSet, FastAttributes);
}

void UDynamicAttributeSet::OnRep_FastAttributes()
{
    // 1. Rebuild the local quick lookup mapping cache
    FastLookupMap.Empty();
    for (const FDynamicAttributeItem& Item : FastAttributes.Items)
    {
        if (Item.AttributeTag.IsValid())
        {
            FastLookupMap.Add(Item.AttributeTag, Item.Value);
            
            // 2. BROADCAST TO CLIENT UI: Notify any listening widgets that this tag has received a network update
            OnDynamicAttributeChanged.Broadcast(Item.AttributeTag, Item.Value);
        }
    }
}

float UDynamicAttributeSet::GetDynamicAttributeValue(FGameplayTag AttributeTag) const
{
    return FastLookupMap.FindRef(AttributeTag);
}

// --- MANDATORY FAST ARRAY CALLBACK IMPLEMENTATIONS ---
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

    // 1. Pull the current baseline value from the fast un-replicated local cache map
    float OldVal = FastLookupMap.FindRef(AttributeTag);
    float Delta = NewVal - OldVal;

    // 2. Process Progression Multipliers (Gain Rate / Loss Rate Modifications)
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

    // 3. Commit the final computed value calculation to your local fast lookup map cache
    FastLookupMap.Add(AttributeTag, NewVal);

    // 4. Synchronize or append the value to the UHT-approved Fast Array Serializer
    int32 FoundIdx = FastAttributes.Items.IndexOfByPredicate([AttributeTag](const FDynamicAttributeItem& Element) {
        return Element.AttributeTag == AttributeTag;
    });

    if (FoundIdx != INDEX_NONE)
    {
        FastAttributes.Items[FoundIdx].Value = NewVal;
        FastAttributes.MarkItemDirty(FastAttributes.Items[FoundIdx]); // Incremental network delta update
    }
    else
    {
        FDynamicAttributeItem NewItem;
        NewItem.AttributeTag = AttributeTag;
        NewItem.Value = NewVal;
        FastAttributes.Items.Add(NewItem);
        FastAttributes.MarkArrayDirty(); // Inform system of structural entry additions
    }
    
    // 5. BROADCAST TO SERVER / LISTEN-HOST UI: Instantly updates local host layouts without waiting for a net packet
    OnDynamicAttributeChanged.Broadcast(AttributeTag, NewVal);
    
    // 6. Evaluate Threshold Breakthrough Gating & Level-Ups
    if (GS)
    {
        if (FDynamicThresholdContainer* Container = GS->MasterThresholdRegistry.Find(AttributeTag))
        {
            for (const FDynamicThresholdRule& Rule : Container->Rules)
            {
                float MaxCapValue = GetDynamicAttributeValue(Rule.MaxThresholdAttributeTag);
                if (NewVal >= MaxCapValue && MaxCapValue > 0.0f)
                {
                    // Enforce structural trait milestone breakthroughs
                    // Uses 'SwapAttributes' matching your core enums setup
                    if (Rule.TriggerBehavior == EThresholdTriggerType::LevelUpAttribute)
                    {
                        // Reset current changing value back down to 0, and level up the target attribute recursively
                        SetDynamicAttributeValue(Rule.MonitoredAttributeTag, 0.0f);
                        SetDynamicAttributeValue(Rule.TargetEvolvedAttributeTag, GetDynamicAttributeValue(Rule.TargetEvolvedAttributeTag) + 1.0f);
                    
                        if (UAbilitySystemComponent* ASC = OwningActor ? OwningActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr)
                        {
                            // Securely send the data payload into active GAS event listeners
                            FGameplayEventData Payload; 
                            Payload.EventTag = Rule.TargetEvolvedAttributeTag;
                            ASC->HandleGameplayEvent(FGameplayTag::RequestGameplayTag("Event.Trait.Evolved"), &Payload);
                        }
                    }
                }
            }
        }
    }
}

void UDynamicAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    // Dynamic processing hooks for handling milestone modifiers
    FGameplayTag ChangedAttributeTag = FGameplayTag::RequestGameplayTag("Attribute.Stat.Strength", false);
    float OldVal = 10.0f; 
    float NewVal = GetDynamicAttributeValue(ChangedAttributeTag);

    if (NewVal > OldVal)
    {
        EvaluateMilestoneIncreases(ChangedAttributeTag, OldVal, NewVal, Data.Target.GetOwnerActor());
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