// DynamicGASManagerComponent.cpp
#include "DynamicGASManagerComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayAbility.h"
#include "DynamicAttributeSet.h"
#include "DynamicEffectFactory.h"
#include "DynamicLogicEvaluator.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

UDynamicGASManagerComponent::UDynamicGASManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedProgression.OwningManagerComp = this;
}

void UDynamicGASManagerComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedProgression.OwningManagerComp = this;
    RebuildProgressionCache();
}

void UDynamicGASManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicGASManagerComponent, ReplicatedProgression);
}

void FSkillProgressionItem::PostReplicatedAdd(const FFastSkillProgressionArray& InArrayOwner)
{
    if (InArrayOwner.OwningManagerComp)
    {
        InArrayOwner.OwningManagerComp->HandleProgressionReplicated(*this);
    }
}

void FSkillProgressionItem::PostReplicatedChange(const FFastSkillProgressionArray& InArrayOwner)
{
    if (InArrayOwner.OwningManagerComp)
    {
        InArrayOwner.OwningManagerComp->HandleProgressionReplicated(*this);
    }
}

void UDynamicGASManagerComponent::OnRep_ReplicatedProgression()
{
    RebuildProgressionCache();
}

void UDynamicGASManagerComponent::HandleProgressionReplicated(const FSkillProgressionItem& Item)
{
    LocalProgressionCache.Add(Item.NodeTag, Item.UnlockedTiers);
    OnSkillNodeUnlocked.Broadcast(FGameplayTag(), Item.NodeTag, Item.UnlockedTiers);
}

void UDynamicGASManagerComponent::RebuildProgressionCache()
{
    LocalProgressionCache.Empty(ReplicatedProgression.Items.Num());
    for (const FSkillProgressionItem& Item : ReplicatedProgression.Items)
    {
        if (Item.NodeTag.IsValid())
        {
            LocalProgressionCache.Add(Item.NodeTag, Item.UnlockedTiers);
        }
    }
}

int32 UDynamicGASManagerComponent::GetSkillNodeInvestedTiers(FGameplayTag NodeTag) const
{
    if (const int32* Found = LocalProgressionCache.Find(NodeTag))
    {
        return *Found;
    }
    return 0;
}

bool UDynamicGASManagerComponent::IsSkillNodeMaxed(FGameplayTag TreeTag, FGameplayTag NodeTag) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FSkillTreeDef* TreeDef = GS->SkillTreeMasterRegistry.Find(TreeTag);
    if (!TreeDef) return false;

    const FSkillTreeNodeDef* NodeDef = TreeDef->FindNode(NodeTag);
    if (!NodeDef) return false;

    return GetSkillNodeInvestedTiers(NodeTag) >= NodeDef->MaxInvestableTiers;
}

bool UDynamicGASManagerComponent::CanUnlockSkillNode(FGameplayTag TreeTag, FGameplayTag NodeTag) const
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !TreeTag.IsValid() || !NodeTag.IsValid()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FSkillTreeDef* TreeDef = GS->SkillTreeMasterRegistry.Find(TreeTag);
    if (!TreeDef) return false;

    const FSkillTreeNodeDef* NodeDef = TreeDef->FindNode(NodeTag);
    if (!NodeDef) return false;

    const int32 CurrentTiers = GetSkillNodeInvestedTiers(NodeTag);
    if (CurrentTiers >= NodeDef->MaxInvestableTiers) return false;

    UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
    const UDynamicAttributeSet* AttrSet = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
    if (!AttrSet) return false;

    // 1. Currency / Point Balance Check
    if (NodeDef->CurrencyAttributeTag.IsValid())
    {
        const float AvailableCurrency = AttrSet->GetDynamicAttributeValue(NodeDef->CurrencyAttributeTag, 0);
        if (AvailableCurrency < NodeDef->PointCostPerTier)
        {
            return false;
        }
    }

    // 2. Prerequisite Node Unlocks
    for (const FGameplayTag& PrereqTag : NodeDef->PrerequisiteNodeTags)
    {
        if (GetSkillNodeInvestedTiers(PrereqTag) <= 0)
        {
            return false;
        }
    }

    // 3. Dynamic Condition Tree Gating
    if (!NodeDef->PrerequisiteConditionTreeName.IsNone())
    {
        const FDynamicConditionTreeContainer* ConditionTree = GS->SharedConditionTrees.Find(NodeDef->PrerequisiteConditionTreeName);
        if (ConditionTree)
        {
            FGameplayTagContainer SourceTags;
            ASC->GetOwnedGameplayTags(SourceTags);
            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, ASC, ASC, SourceTags, SourceTags))
            {
                return false;
            }
        }
    }

    return true;
}

bool UDynamicGASManagerComponent::ServerUnlockSkillNode(FGameplayTag TreeTag, FGameplayTag NodeTag)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority() || !CanUnlockSkillNode(TreeTag, NodeTag)) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FSkillTreeDef* TreeDef = GS->SkillTreeMasterRegistry.Find(TreeTag);
    if (!TreeDef) return false;

    const FSkillTreeNodeDef* NodeDef = TreeDef->FindNode(NodeTag);
    if (!NodeDef) return false;

    UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
    UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
    if (!AttrSet) return false;

    // 1. Calculate Target Tier
    const int32 CurrentTiers = GetSkillNodeInvestedTiers(NodeTag);
    const int32 NewTier = CurrentTiers + 1;

    // 2. Deduct Currency/Point Cost
    if (NodeDef->CurrencyAttributeTag.IsValid() && NodeDef->PointCostPerTier > 0.0f)
    {
        const float CurrentBalance = AttrSet->GetDynamicAttributeValue(NodeDef->CurrencyAttributeTag, 0);
        AttrSet->SetDynamicAttributeValue(NodeDef->CurrencyAttributeTag, CurrentBalance - NodeDef->PointCostPerTier, 0);
    }

    // 3. Apply Multi-Tier Upgrades (Ability Level Up or Effect Stacking)
    switch (NodeDef->UnlockType)
    {
        case ESkillNodeUnlockType::GrantAbility:
        {
            if (NodeDef->GrantedAbilityTag.IsValid())
            {
                FGameplayAbilitySpec* ExistingSpec = nullptr;
                for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
                {
                    if (Spec.GetDynamicSpecSourceTags().HasTagExact(NodeDef->GrantedAbilityTag))
                    {
                        ExistingSpec = &Spec;
                        break;
                    }
                }

                if (ExistingSpec)
                {
                    // Increment Ability Spec Level
                    ExistingSpec->Level = NewTier;
                    ASC->MarkAbilitySpecDirty(*ExistingSpec);
                }
                else
                {
                    // First Unlock: Grant at initial level
                    TryLearnAndGrantAbility(ASC, NodeDef->GrantedAbilityTag, FGameplayTag::EmptyTag);
                }
            }
            break;
        }
        case ESkillNodeUnlockType::ApplyEffect:
        {
            // Apply effect at the newly unlocked tier level to leverage duration/stacking aggregation
            for (const FGameplayTag& EffectTag : NodeDef->GrantedEffectTags)
            {
                FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(ASC, EffectTag, NewTier);
                if (SpecHandle.IsValid())
                {
                    ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
                }
            }
            break;
        }
    }

    // 4. Update Replicated FastArray Progression
    int32 FoundIdx = ReplicatedProgression.Items.IndexOfByPredicate([NodeTag](const FSkillProgressionItem& Item) {
        return Item.NodeTag == NodeTag;
    });

    if (FoundIdx != INDEX_NONE)
    {
        ReplicatedProgression.Items[FoundIdx].UnlockedTiers = NewTier;
        ReplicatedProgression.MarkItemDirty(ReplicatedProgression.Items[FoundIdx]);
    }
    else
    {
        FSkillProgressionItem NewItem;
        NewItem.NodeTag = NodeTag;
        NewItem.UnlockedTiers = NewTier;
        FSkillProgressionItem& Added = ReplicatedProgression.Items.Add_GetRef(NewItem);
        ReplicatedProgression.MarkItemDirty(Added);
    }

    RebuildProgressionCache();
    OnSkillNodeUnlocked.Broadcast(TreeTag, NodeTag, NewTier);
    return true;
}

bool UDynamicGASManagerComponent::ResolveSkillNodeDisplayData(
    FGameplayTag TreeTag, 
    FGameplayTag NodeTag, 
    FString& OutName, 
    FString& OutDescription, 
    TSoftObjectPtr<UTexture2D>& OutActiveIcon) const
{
    OutName.Empty();
    OutDescription.Empty();
    OutActiveIcon = nullptr;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FSkillTreeDef* TreeDef = GS->SkillTreeMasterRegistry.Find(TreeTag);
    if (!TreeDef) return false;

    const FSkillTreeNodeDef* NodeDef = TreeDef->FindNode(NodeTag);
    if (!NodeDef) return false;

    const int32 InvestedTiers = GetSkillNodeInvestedTiers(NodeTag);
    const bool bIsLocked = !CanUnlockSkillNode(TreeTag, NodeTag) && (InvestedTiers <= 0);

    // 1. Resolve State Icon
    if (InvestedTiers >= NodeDef->MaxInvestableTiers)
    {
        OutActiveIcon = NodeDef->IconTextures.ObtainedIcon;
    }
    else if (InvestedTiers > 0)
    {
        OutActiveIcon = NodeDef->IconTextures.SelectedIcon;
    }
    else if (bIsLocked)
    {
        OutActiveIcon = NodeDef->IconTextures.LockedIcon;
    }
    else
    {
        OutActiveIcon = NodeDef->IconTextures.UnselectedIcon;
    }

    static const FGameplayTag LocNameTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Name"), false);
    static const FGameplayTag LocDescTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Description"), false);

    // 2. Resolve Text via Granted Ability
    if (NodeDef->UnlockType == ESkillNodeUnlockType::GrantAbility && NodeDef->GrantedAbilityTag.IsValid())
    {
        if (const FDynamicAbilityDef* AbilityDef = GS->AbilityMasterRegistry.Find(NodeDef->GrantedAbilityTag))
        {
            OutName = AbilityDef->Localization.GetLocalizedText(LocNameTag);
            OutDescription = AbilityDef->Localization.GetLocalizedText(LocDescTag);
            return true;
        }
    }

    // 3. Resolve Text via Granted Effect
    if (NodeDef->UnlockType == ESkillNodeUnlockType::ApplyEffect && NodeDef->GrantedEffectTags.Num() > 0)
    {
        if (const FDynamicEffectDef* EffectDef = GS->MasterEffectRegistry.Find(NodeDef->GrantedEffectTags[0]))
        {
            OutName = EffectDef->Localization.GetLocalizedText(LocNameTag);
            OutDescription = EffectDef->Localization.GetLocalizedText(LocDescTag);
            return true;
        }
    }

    OutName = NodeDef->NodeTag.ToString();
    return true;
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

void UDynamicGASManagerComponent::ProcessActiveAbilityInputSequence(FGameplayTag ActionTag, bool bIsPressed)
{
    AActor* OwnerActor = GetOwner();
    UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    if (!ASC) return;

    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (UDynamicGameplayAbility* ActiveInstance = Cast<UDynamicGameplayAbility>(Spec.GetPrimaryInstance()))
        {
            if (ActiveInstance->IsActive())
            {
                ActiveInstance->HandleInputStep(ActionTag, bIsPressed);
            }
        }
    }
}