// DynamicAttributeSet.cpp
#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicEffectFactory.h"
#include "DynamicCharacter.h"

UDynamicAttributeSet::UDynamicAttributeSet()
{
    FastAttributes.OwningAttributeSet = this;
}

void UDynamicAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicAttributeSet, FastAttributes);
}

void UDynamicAttributeSet::IndexAttributeTagIntoHierarchyCache(FGameplayTag LeafTag)
{
    if (!LeafTag.IsValid()) return;

    FGameplayTag ImmediateParent = LeafTag.RequestDirectParent();
    if (ImmediateParent.IsValid())
    {
        DirectChildrenTagCache.FindOrAdd(ImmediateParent).AddTag(LeafTag);
    }

    FGameplayTag CurrentAncestor = ImmediateParent;
    while (CurrentAncestor.IsValid())
    {
        AllDescendantsTagCache.FindOrAdd(CurrentAncestor).AddTag(LeafTag);
        CurrentAncestor = CurrentAncestor.RequestDirectParent();
    }
}

void UDynamicAttributeSet::HandleSingleAttributeReplicated(const FDynamicAttributeItem& Item)
{
    if (!Item.AttributeTag.IsValid()) return;

    const uint64 Key = MakeCompositeKey(Item.LimbId, Item.AttributeTag);
    if (!FastCompositeLookupMap.Contains(Key))
    {
        IndexAttributeTagIntoHierarchyCache(Item.AttributeTag);
    }

    FastCompositeLookupMap.Add(Key, Item.Value);

    if (Item.LimbId == 0)
    {
        OnDynamicAttributeChanged.Broadcast(Item.AttributeTag, Item.Value);
    }
    OnDynamicLimbAttributeChanged.Broadcast(Item.LimbId, Item.AttributeTag, Item.Value);

    if (Item.LimbId > 0)
    {
        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(GetOwningActor()))
        {
            DynChar->RecalculateLimbPhysicsMotors(Item.LimbId);
        }
    }
}

void UDynamicAttributeSet::OnRep_FastAttributes()
{
    FastCompositeLookupMap.Empty(FastAttributes.Items.Num());
    DirectChildrenTagCache.Empty();
    AllDescendantsTagCache.Empty();

    for (const FDynamicAttributeItem& Item : FastAttributes.Items)
    {
        HandleSingleAttributeReplicated(Item);
    }
}

float UDynamicAttributeSet::GetDynamicAttributeValue(FGameplayTag AttributeTag, uint8 LimbId) const
{
    if (!AttributeTag.IsValid()) return 0.0f;
    return FastCompositeLookupMap.FindRef(MakeCompositeKey(LimbId, AttributeTag));
}

TArray<FTagFloatPair> UDynamicAttributeSet::GetAttributesByParentTag(FGameplayTag ParentTag, bool bIncludeAllSubAttributes, uint8 LimbId) const
{
    TArray<FTagFloatPair> OutResults;
    if (!ParentTag.IsValid()) return OutResults;

    const TMap<FGameplayTag, FGameplayTagContainer>& TargetCache = bIncludeAllSubAttributes ? AllDescendantsTagCache : DirectChildrenTagCache;
    if (const FGameplayTagContainer* CachedChildren = TargetCache.Find(ParentTag))
    {
        OutResults.Reserve(CachedChildren->Num());
        for (const FGameplayTag& ChildTag : *CachedChildren)
        {
            const uint64 Key = MakeCompositeKey(LimbId, ChildTag);
            if (const float* FoundVal = FastCompositeLookupMap.Find(Key))
            {
                FTagFloatPair Pair;
                Pair.KeyTag = ChildTag;
                Pair.Value = *FoundVal;
                OutResults.Add(Pair);
            }
        }
    }
    return OutResults;
}

void UDynamicAttributeSet::InitializeLimbAttributesFromConfig(const TMap<uint8, FPhysicsLimbConfig>& LimbConfigs)
{
    for (const auto& LimbPair : LimbConfigs)
    {
        const uint8 LimbId = LimbPair.Key;
        for (const FTagFloatPair& AttrPair : LimbPair.Value.DefaultLimbAttributes.ReplicatedEntries)
        {
            if (AttrPair.KeyTag.IsValid())
            {
                SetDynamicAttributeValue(AttrPair.KeyTag, AttrPair.Value, LimbId);
            }
        }
    }
}

void FDynamicAttributeItem::PreReplicatedRemove(const FFastAttributeArray& InArrayOwner) {}

void FDynamicAttributeItem::PostReplicatedAdd(const FFastAttributeArray& InArrayOwner)
{
    if (InArrayOwner.OwningAttributeSet)
    {
        InArrayOwner.OwningAttributeSet->HandleSingleAttributeReplicated(*this);
    }
}

void FDynamicAttributeItem::PostReplicatedChange(const FFastAttributeArray& InArrayOwner)
{
    if (InArrayOwner.OwningAttributeSet)
    {
        InArrayOwner.OwningAttributeSet->HandleSingleAttributeReplicated(*this);
    }
}

void UDynamicAttributeSet::SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal, uint8 LimbId)
{
    SetDynamicAttributeValueInternal(AttributeTag, NewVal, LimbId, ActiveThresholdEvaluationKeys);
}

void UDynamicAttributeSet::SetDynamicAttributeValueInternal(FGameplayTag AttributeTag, float NewVal, uint8 LimbId, TSet<uint64>& InOutEvaluationStack)
{
    if (!AttributeTag.IsValid()) return;

    const uint64 CompositeKey = MakeCompositeKey(LimbId, AttributeTag);

    // Guaranteed RAII Scope Tracker
    FScopedAttributeEvaluationGuard CycleGuard(InOutEvaluationStack, CompositeKey);
    if (CycleGuard.IsCycleDetected())
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicAttributeSet: Aborted recursive cycle on Tag %s (Limb %d)"), *AttributeTag.ToString(), LimbId);
        return;
    }

    AActor* OwningActor = GetOwningActor();
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    const bool bIsNewAttribute = !FastCompositeLookupMap.Contains(CompositeKey);
    const float OldVal = FastCompositeLookupMap.FindRef(CompositeKey);
    float Delta = NewVal - OldVal;

    // 1. Threshold rules modifier check
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
                        Delta *= (1.0f + GetDynamicAttributeValue(Rule.GainModifierAttributeTag, LimbId));
                    }
                    else if (Delta < 0.0f && Rule.LossModifierAttributeTag.IsValid())
                    {
                        Delta *= (1.0f + GetDynamicAttributeValue(Rule.LossModifierAttributeTag, LimbId));
                    }
                    NewVal = OldVal + Delta;
                    break;
                }
            }
        }
    }

    if (FMath::IsNearlyEqual(OldVal, NewVal) && !bIsNewAttribute) return;

    FastCompositeLookupMap.Add(CompositeKey, NewVal);
    if (bIsNewAttribute)
    {
        IndexAttributeTagIntoHierarchyCache(AttributeTag);
    }

    int32 FoundIdx = FastAttributes.Items.IndexOfByPredicate([LimbId, AttributeTag](const FDynamicAttributeItem& Element) {
        return Element.LimbId == LimbId && Element.AttributeTag == AttributeTag;
    });

    if (FoundIdx != INDEX_NONE)
    {
        FastAttributes.Items[FoundIdx].Value = NewVal;
        FastAttributes.MarkItemDirty(FastAttributes.Items[FoundIdx]);
    }
    else
    {
        FDynamicAttributeItem NewItem;
        NewItem.LimbId = LimbId;
        NewItem.AttributeTag = AttributeTag;
        NewItem.Value = NewVal;
        FastAttributes.Items.Add(NewItem);
        FastAttributes.MarkArrayDirty();
    }

    if (LimbId == 0)
    {
        OnDynamicAttributeChanged.Broadcast(AttributeTag, NewVal);
    }
    OnDynamicLimbAttributeChanged.Broadcast(LimbId, AttributeTag, NewVal);

    if (LimbId > 0)
    {
        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(OwningActor))
        {
            DynChar->RecalculateLimbPhysicsMotors(LimbId);
        }
    }

    // 2. High-Performance Milestone Evaluation
    if (OwningActor && GS && GS->MasterMilestoneRegistry.Contains(AttributeTag))
    {
        const int32 OldInt = FMath::FloorToInt(OldVal);
        const int32 NewInt = FMath::FloorToInt(NewVal);
        if (OldInt != NewInt)
        {
            EvaluateMilestoneChanges(AttributeTag, OldVal, NewVal, OwningActor, LimbId);
        }
    }

    // 3. Threshold Progression Escalation (Level-up wrap-around)
    if (GS)
    {
        if (FDynamicThresholdContainer* Container = GS->MasterThresholdRegistry.Find(AttributeTag))
        {
            for (const FDynamicThresholdRule& Rule : Container->Rules)
            {
                const float MaxCapValue = GetDynamicAttributeValue(Rule.MaxThresholdAttributeTag, LimbId);
                if (NewVal >= MaxCapValue && MaxCapValue > 0.0f)
                {
                    if (Rule.TriggerBehavior == EThresholdTriggerType::LevelUpAttribute)
                    {
                        const float RemainingProgress = NewVal - MaxCapValue;
                        SetDynamicAttributeValueInternal(Rule.MonitoredAttributeTag, RemainingProgress, LimbId, InOutEvaluationStack);
                        SetDynamicAttributeValueInternal(Rule.TargetEvolvedAttributeTag, GetDynamicAttributeValue(Rule.TargetEvolvedAttributeTag, LimbId) + 1.0f, LimbId, InOutEvaluationStack);

                        if (Rule.BroadcastEventTag.IsValid())
                        {
                            if (UAbilitySystemComponent* ASC = OwningActor ? OwningActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr)
                            {
                                FGameplayEventData Payload;
                                Payload.EventTag = Rule.TargetEvolvedAttributeTag;
                                Payload.EventMagnitude = static_cast<float>(LimbId);
                                ASC->HandleGameplayEvent(Rule.BroadcastEventTag, &Payload);
                            }
                        }
                    }
                }
            }
        }
    }
}

void UDynamicAttributeSet::EvaluateMilestoneChanges(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor, uint8 LimbId)
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    UAbilitySystemComponent* TargetASC = TargetActor ? TargetActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    if (!GS || !TargetASC) return;

    const int32 OldInt = FMath::FloorToInt(OldValue);
    const int32 NewInt = FMath::FloorToInt(NewValue);

    if (FDynamicMilestoneContainer* Container = GS->MasterMilestoneRegistry.Find(AttributeTag))
    {
        FGameplayTagContainer SourceTags;
        FGameplayTagContainer TargetTags;
        TargetASC->GetOwnedGameplayTags(TargetTags);

        for (const FDynamicMilestoneAction& Action : Container->MilestoneActions)
        {
            const int32 IntervalKey = FMath::Max(1, Action.TriggerLevelOrInterval);

            if (Action.ExecutionTriggerConditionTag.IsValid())
            {
                const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(TargetActor, Action.ExecutionTriggerConditionTag.GetTagName());
                if (ConditionTree && !UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, TargetASC, TargetASC, SourceTags, TargetTags))
                {
                    continue;
                }
            }

            if (NewInt > OldInt)
            {
                for (int32 CheckedLevel = OldInt + 1; CheckedLevel <= NewInt; ++CheckedLevel)
                {
                    const bool bShouldTrigger = Action.bIsIntervalRepeating ? (CheckedLevel % IntervalKey == 0) : (CheckedLevel == IntervalKey);
                    if (bShouldTrigger)
                    {
                        ApplyOrUndoMilestoneStep(Action, false, TargetASC, LimbId);
                    }
                }
            }
            else if (NewInt < OldInt && Action.bUndoOnLevelDown)
            {
                for (int32 CheckedLevel = OldInt; CheckedLevel > NewInt; --CheckedLevel)
                {
                    const bool bWasTriggeredAtLevel = Action.bIsIntervalRepeating ? (CheckedLevel % IntervalKey == 0) : (CheckedLevel == IntervalKey);
                    if (bWasTriggeredAtLevel)
                    {
                        ApplyOrUndoMilestoneStep(Action, true, TargetASC, LimbId);
                    }
                }
            }
        }
    }
}

void UDynamicAttributeSet::ApplyOrUndoMilestoneStep(const FDynamicMilestoneAction& Action, bool bIsUndo, UAbilitySystemComponent* TargetASC, uint8 LimbId)
{
    for (const FDynamicModifierDef& ModDef : Action.ModifiersToApply)
    {
        if (!ModDef.TargetAttributeTag.IsValid()) continue;

        const float CurrentVal = GetDynamicAttributeValue(ModDef.TargetAttributeTag, LimbId);
        float UpdatedVal = CurrentVal;

        switch (ModDef.ModifierOp)
        {
            case EEffectModifierOp::Additive:
                UpdatedVal = bIsUndo ? (CurrentVal - ModDef.BaseMagnitude) : (CurrentVal + ModDef.BaseMagnitude);
                break;
            case EEffectModifierOp::Multiplicative:
            {
                const float Factor = 1.0f + ModDef.BaseMagnitude;
                if (!FMath::IsNearlyZero(Factor))
                {
                    UpdatedVal = bIsUndo ? (CurrentVal / Factor) : (CurrentVal * Factor);
                }
                break;
            }
            case EEffectModifierOp::Override:
                if (!bIsUndo)
                {
                    UpdatedVal = ModDef.BaseMagnitude;
                }
                break;
        }

        SetDynamicAttributeValue(ModDef.TargetAttributeTag, UpdatedVal, LimbId);
    }

    if (!TargetASC) return;
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    for (const FGameplayTag& EffectTag : Action.EffectTagsToApply)
    {
        if (!EffectTag.IsValid()) continue;

        if (!bIsUndo)
        {
            FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(TargetASC, EffectTag, 1);
            if (SpecHandle.IsValid())
            {
                TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
            }
        }
        else if (GS)
        {
            if (const FDynamicEffectDef* EffectDef = GS->MasterEffectRegistry.Find(EffectTag))
            {
                if (EffectDef->DurationPolicy == EEffectDurationPolicy::Instant)
                {
                    for (const FDynamicModifierDef& ModDef : EffectDef->Modifiers)
                    {
                        if (!ModDef.TargetAttributeTag.IsValid()) continue;
                        const float Cur = GetDynamicAttributeValue(ModDef.TargetAttributeTag, LimbId);
                        if (ModDef.ModifierOp == EEffectModifierOp::Additive)
                        {
                            SetDynamicAttributeValue(ModDef.TargetAttributeTag, Cur - ModDef.BaseMagnitude, LimbId);
                        }
                        else if (ModDef.ModifierOp == EEffectModifierOp::Multiplicative && !FMath::IsNearlyZero(1.0f + ModDef.BaseMagnitude))
                        {
                            SetDynamicAttributeValue(ModDef.TargetAttributeTag, Cur / (1.0f + ModDef.BaseMagnitude), LimbId);
                        }
                    }
                }
                else if (EffectDef->GrantedTags.Num() > 0)
                {
                    TargetASC->RemoveActiveEffectsWithGrantedTags(EffectDef->GrantedTags);
                }
            }
        }
    }
}