#include "DynamicQuestComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicSocialComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

UDynamicQuestComponent::UDynamicQuestComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedActiveQuests.OwningQuestComp = this;
}

void UDynamicQuestComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedActiveQuests.OwningQuestComp = this;
}

void UDynamicQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicQuestComponent, ReplicatedActiveQuests);
    DOREPLIFETIME(UDynamicQuestComponent, CompletedQuestTags);
}

void FDynamicActiveQuestItem::PreReplicatedRemove(const FFastActiveQuestArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleQuestReplicated(*this, true);
}

void FDynamicActiveQuestItem::PostReplicatedAdd(const FFastActiveQuestArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleQuestReplicated(*this, false);
}

void FDynamicActiveQuestItem::PostReplicatedChange(const FFastActiveQuestArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleQuestReplicated(*this, false);
}

void UDynamicQuestComponent::HandleQuestReplicated(const FDynamicActiveQuestItem& Item, bool bRemoved)
{
    OnQuestStateChanged.Broadcast(Item.QuestInstanceGuid, Item.QuestDefTag, bRemoved ? EQuestStepState::Abandoned : Item.StepState);
}

void UDynamicQuestComponent::OnRep_ActiveQuests()
{
    for (const FDynamicActiveQuestItem& Item : ReplicatedActiveQuests.Items)
    {
        OnQuestStateChanged.Broadcast(Item.QuestInstanceGuid, Item.QuestDefTag, Item.StepState);
    }
}

bool UDynamicQuestComponent::GetActiveQuestData(const FGuid& QuestGuid, FDynamicActiveQuestItem& OutQuestItem) const
{
    for (const FDynamicActiveQuestItem& Item : ReplicatedActiveQuests.Items)
    {
        if (Item.QuestInstanceGuid == QuestGuid)
        {
            OutQuestItem = Item;
            return true;
        }
    }
    return false;
}

bool UDynamicQuestComponent::CanAcceptQuest(FGameplayTag QuestDefTag, const AActor* GiverActor) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !QuestDefTag.IsValid()) return false;

    const FQuestDef* QuestDef = GS->QuestMasterRegistry.Find(QuestDefTag);
    if (!QuestDef) return false;

    // 1. Check if already active or completed
    for (const FDynamicActiveQuestItem& ActiveItem : ReplicatedActiveQuests.Items)
    {
        if (ActiveItem.QuestDefTag == QuestDefTag) return false;
    }
    if (CompletedQuestTags.HasTagExact(QuestDefTag)) return false;

    // 2. Tag requirements
    UAbilitySystemComponent* ASC = GetOwner() ? GetOwner()->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    if (!ASC) return false;

    FGameplayTagContainer OwnedTags;
    ASC->GetOwnedGameplayTags(OwnedTags);

    if (QuestDef->RequiredPlayerTags.Num() > 0 && !OwnedTags.HasAll(QuestDef->RequiredPlayerTags))
    {
        return false;
    }
    if (QuestDef->ExcludedPlayerTags.Num() > 0 && OwnedTags.HasAny(QuestDef->ExcludedPlayerTags))
    {
        return false;
    }

    // 3. Social Relationship gating
    if (GiverActor)
    {
        if (const UDynamicSocialComponent* SocialComp = GetOwner()->FindComponentByClass<UDynamicSocialComponent>())
        {
            if (const UDynamicSocialComponent* GiverSocial = GiverActor->FindComponentByClass<UDynamicSocialComponent>())
            {
                float Affinity = 0.0f, Trust = 0.0f, Fam = 0.0f, Debt = 0.0f;
                if (GiverSocial && SocialComp->GetDecayedRelationship(GiverSocial->CharacterPersistentGuid, Affinity, Trust, Fam, Debt))
                {
                    if (Affinity < QuestDef->MinAffinityRequired || Trust < QuestDef->MinTrustRequired)
                    {
                        return false;
                    }
                }
                else if (QuestDef->MinAffinityRequired > 0.0f || QuestDef->MinTrustRequired > 0.0f)
                {
                    // If no relationship exists yet, only neutral/open quests can be taken
                    return false;
                }
            }
        }
    }

    // 4. Logic evaluator condition tree
    if (!QuestDef->PrerequisiteConditionTreeName.IsNone())
    {
        const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(GetOwner(), QuestDef->PrerequisiteConditionTreeName);
        if (Tree)
        {
            FGameplayTagContainer SourceTags, TargetTags;
            ASC->GetOwnedGameplayTags(SourceTags);
            const UAbilitySystemComponent* GiverASC = GiverActor ? GiverActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
            if (GiverASC) GiverASC->GetOwnedGameplayTags(TargetTags);

            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, GiverASC, SourceTags, TargetTags))
            {
                return false;
            }
        }
    }

    return true;
}

bool UDynamicQuestComponent::ServerAcceptQuest(FGameplayTag QuestDefTag, const FGuid& QuestGiverGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FQuestDef* QuestDef = GS->QuestMasterRegistry.Find(QuestDefTag);
    if (!QuestDef) return false;

    // Find initial step
    const FQuestStepDef* InitialStep = nullptr;
    for (const FQuestStepDef& Step : QuestDef->Steps)
    {
        if (Step.StepTag == QuestDef->InitialStepTag)
        {
            InitialStep = &Step;
            break;
        }
    }
    if (!InitialStep) return false;

    FDynamicActiveQuestItem NewQuest;
    NewQuest.QuestInstanceGuid = FGuid::NewGuid();
    NewQuest.QuestDefTag = QuestDefTag;
    NewQuest.QuestGiverGuid = QuestGiverGuid;
    NewQuest.CurrentStepTag = InitialStep->StepTag;
    NewQuest.StepState = EQuestStepState::Active;
    NewQuest.StepStartWorldTime = GetWorld()->GetTimeSeconds();

    for (const FQuestObjectiveDef& ObjDef : InitialStep->Objectives)
    {
        FDynamicActiveQuestObjectiveProgress ObjProgress;
        ObjProgress.ObjectiveTag = ObjDef.ObjectiveTag;
        ObjProgress.CurrentProgress = 0;
        ObjProgress.RequiredCount = ObjDef.RequiredCount;
        ObjProgress.bIsComplete = false;
        NewQuest.ObjectivesProgress.Add(ObjProgress);
    }

    FDynamicActiveQuestItem& Added = ReplicatedActiveQuests.Items.Add_GetRef(NewQuest);
    ReplicatedActiveQuests.MarkItemDirty(Added);

    OnQuestStateChanged.Broadcast(Added.QuestInstanceGuid, Added.QuestDefTag, EQuestStepState::Active);
    return true;
}

bool UDynamicQuestComponent::ServerProgressObjective(const FGuid& QuestGuid, FGameplayTag ObjectiveTag, int32 ProgressAmount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    int32 FoundQuestIndex = INDEX_NONE;
    for (int32 i = 0; i < ReplicatedActiveQuests.Items.Num(); ++i)
    {
        if (ReplicatedActiveQuests.Items[i].QuestInstanceGuid == QuestGuid && ReplicatedActiveQuests.Items[i].StepState == EQuestStepState::Active)
        {
            FoundQuestIndex = i;
            break;
        }
    }
    if (FoundQuestIndex == INDEX_NONE) return false;

    FDynamicActiveQuestItem& QuestItem = ReplicatedActiveQuests.Items[FoundQuestIndex];
    const FQuestDef* QuestDef = GS->QuestMasterRegistry.Find(QuestItem.QuestDefTag);
    if (!QuestDef) return false;

    const FQuestStepDef* CurrentStep = nullptr;
    for (const FQuestStepDef& Step : QuestDef->Steps)
    {
        if (Step.StepTag == QuestItem.CurrentStepTag)
        {
            CurrentStep = &Step;
            break;
        }
    }
    if (!CurrentStep) return false;

    bool bAllObjectivesComplete = true;
    bool bUpdated = false;

    for (FDynamicActiveQuestObjectiveProgress& Obj : QuestItem.ObjectivesProgress)
    {
        if (Obj.ObjectiveTag == ObjectiveTag && !Obj.bIsComplete)
        {
            Obj.CurrentProgress = FMath::Clamp(Obj.CurrentProgress + ProgressAmount, 0, Obj.RequiredCount);
            if (Obj.CurrentProgress >= Obj.RequiredCount)
            {
                Obj.bIsComplete = true;
            }
            bUpdated = true;
            OnQuestObjectiveUpdated.Broadcast(QuestGuid, QuestItem.CurrentStepTag, Obj.ObjectiveTag, Obj.CurrentProgress);
        }
        if (!Obj.bIsComplete)
        {
            bAllObjectivesComplete = false;
        }
    }

    if (bUpdated)
    {
        ReplicatedActiveQuests.MarkItemDirty(QuestItem);

        // Auto-advance if configured
        if (bAllObjectivesComplete && CurrentStep->bAutoAdvanceOnObjectivesCompleted && CurrentStep->StepBranches.Num() > 0)
        {
            ServerAdvanceStepBranch(QuestGuid, CurrentStep->StepBranches[0].BranchTag);
        }
        return true;
    }

    return false;
}

bool UDynamicQuestComponent::ServerAdvanceStepBranch(const FGuid& QuestGuid, FGameplayTag ChosenBranchTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    int32 FoundQuestIndex = INDEX_NONE;
    for (int32 i = 0; i < ReplicatedActiveQuests.Items.Num(); ++i)
    {
        if (ReplicatedActiveQuests.Items[i].QuestInstanceGuid == QuestGuid && ReplicatedActiveQuests.Items[i].StepState == EQuestStepState::Active)
        {
            FoundQuestIndex = i;
            break;
        }
    }
    if (FoundQuestIndex == INDEX_NONE) return false;

    FDynamicActiveQuestItem& QuestItem = ReplicatedActiveQuests.Items[FoundQuestIndex];
    const FQuestDef* QuestDef = GS->QuestMasterRegistry.Find(QuestItem.QuestDefTag);
    if (!QuestDef) return false;

    const FQuestStepDef* CurrentStep = nullptr;
    for (const FQuestStepDef& Step : QuestDef->Steps)
    {
        if (Step.StepTag == QuestItem.CurrentStepTag)
        {
            CurrentStep = &Step;
            break;
        }
    }
    if (!CurrentStep) return false;

    const FQuestStepBranchDef* SelectedBranch = nullptr;
    for (const FQuestStepBranchDef& Branch : CurrentStep->StepBranches)
    {
        if (Branch.BranchTag == ChosenBranchTag)
        {
            SelectedBranch = &Branch;
            break;
        }
    }
    if (!SelectedBranch) return false;

    // Apply immediate branch rewards / social affinity changes
    ApplyRewardConsequences(SelectedBranch->BranchConsequences, QuestItem.QuestGiverGuid);

    // Branch concludes the quest
    if (SelectedBranch->bConcludesQuest || !SelectedBranch->NextStepTag.IsValid())
    {
        CompleteQuestInternal(FoundQuestIndex, SelectedBranch->bMarkedAsSuccess);
        return true;
    }

    // Advance to next step
    const FQuestStepDef* NextStep = nullptr;
    for (const FQuestStepDef& Step : QuestDef->Steps)
    {
        if (Step.StepTag == SelectedBranch->NextStepTag)
        {
            NextStep = &Step;
            break;
        }
    }
    if (!NextStep) return false;

    QuestItem.CurrentStepTag = NextStep->StepTag;
    QuestItem.StepStartWorldTime = GetWorld()->GetTimeSeconds();
    QuestItem.ObjectivesProgress.Empty();

    for (const FQuestObjectiveDef& ObjDef : NextStep->Objectives)
    {
        FDynamicActiveQuestObjectiveProgress ObjProgress;
        ObjProgress.ObjectiveTag = ObjDef.ObjectiveTag;
        ObjProgress.CurrentProgress = 0;
        ObjProgress.RequiredCount = ObjDef.RequiredCount;
        ObjProgress.bIsComplete = false;
        QuestItem.ObjectivesProgress.Add(ObjProgress);
    }

    ReplicatedActiveQuests.MarkItemDirty(QuestItem);
    OnQuestStateChanged.Broadcast(QuestItem.QuestInstanceGuid, QuestItem.QuestDefTag, EQuestStepState::Active);
    return true;
}

void UDynamicQuestComponent::CompleteQuestInternal(int32 ItemIndex, bool bSuccess)
{
    if (!ReplicatedActiveQuests.Items.IsValidIndex(ItemIndex)) return;

    FDynamicActiveQuestItem CompletedItem = ReplicatedActiveQuests.Items[ItemIndex];
    CompletedItem.StepState = bSuccess ? EQuestStepState::Completed : EQuestStepState::Failed;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS && bSuccess)
    {
        if (const FQuestDef* QuestDef = GS->QuestMasterRegistry.Find(CompletedItem.QuestDefTag))
        {
            ApplyRewardConsequences(QuestDef->FinalRewards, CompletedItem.QuestGiverGuid);
        }
        CompletedQuestTags.AddTag(CompletedItem.QuestDefTag);
    }

    OnQuestStateChanged.Broadcast(CompletedItem.QuestInstanceGuid, CompletedItem.QuestDefTag, CompletedItem.StepState);
    ReplicatedActiveQuests.Items.RemoveAt(ItemIndex);
    ReplicatedActiveQuests.MarkArrayDirty();
}

void UDynamicQuestComponent::ApplyRewardConsequences(const FQuestRewardConsequence& Consequences, const FGuid& GiverGuid)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority()) return;

    // 1. Attribute Modifiers (Retrieved via AbilitySystemComponent since UAttributeSet is not a UActorComponent)
    if (UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
        {
            for (const FGenderAttributeModifier& Mod : Consequences.AttributeModifiers)
            {
                if (Mod.TargetAttributeTag.IsValid())
                {
                    const float Cur = AttrSet->GetDynamicAttributeValue(Mod.TargetAttributeTag, 0);
                    const float Final = (Mod.ModifierOp == EEffectModifierOp::Additive) ? (Cur + Mod.Magnitude) : (Cur * (1.0f + Mod.Magnitude));
                    AttrSet->SetDynamicAttributeValue(Mod.TargetAttributeTag, Final, 0);
                }
            }
        }
    }

    // 2. Physical Item Rewards into Inventory Container
    if (UDynamicContainerComponent* Container = OwnerActor->FindComponentByClass<UDynamicContainerComponent>())
    {
        for (const FRecipeIngredient& ItemReward : Consequences.ItemRewards)
        {
            if (ItemReward.ArchetypeTag.IsValid() && ItemReward.RequiredQuantity > 0)
            {
                Container->ServerAddActorByTag(ItemReward.ArchetypeTag, ItemReward.RequiredQuantity, {});
            }
        }
    }

    // 3. Social Relationship Updates with Quest Giver
    if (GiverGuid.IsValid() && (!FMath::IsNearlyZero(Consequences.AffinityDelta) || !FMath::IsNearlyZero(Consequences.TrustDelta) || !FMath::IsNearlyZero(Consequences.DebtDelta)))
    {
        if (UDynamicSocialComponent* SocialComp = OwnerActor->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerRecordSocialInteraction(
                GiverGuid,
                nullptr,
                ERelationshipTargetKind::Character,
                FGameplayTag::RequestGameplayTag(TEXT("Relation.Social.QuestCompletion")),
                Consequences.AffinityDelta,
                Consequences.TrustDelta,
                0.15f,
                Consequences.DebtDelta,
                {}
            );
        }
    }

    // 4. Granted/Removed Tags on ASC
    if (UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        ASC->AddLooseGameplayTags(Consequences.GrantedTags);
        ASC->RemoveLooseGameplayTags(Consequences.RemovedTags);
    }
}

bool UDynamicQuestComponent::ServerAbandonQuest(const FGuid& QuestGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return false;

    int32 FoundIdx = ReplicatedActiveQuests.Items.IndexOfByPredicate([QuestGuid](const FDynamicActiveQuestItem& Item) {
        return Item.QuestInstanceGuid == QuestGuid;
    });

    if (FoundIdx != INDEX_NONE)
    {
        CompleteQuestInternal(FoundIdx, false);
        return true;
    }
    return false;
}

// --- Procedural Generation Logic ---

bool UDynamicQuestComponent::ServerGenerateEconomicSupplyQuest(AActor* GiverActor, FGameplayTag NeededMaterialTag, int32 QuantityNeeded, float UrgencyMultiplier)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !GiverActor || !NeededMaterialTag.IsValid()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    UDynamicSocialComponent* GiverSocial = GiverActor->FindComponentByClass<UDynamicSocialComponent>();
    const FGuid GiverGuid = GiverSocial ? GiverSocial->CharacterPersistentGuid : FGuid::NewGuid();

    // Procedurally create and register a dynamic supply quest
    FQuestDef DynamicQuest;
    const FString UniqueQuestName = FString::Printf(TEXT("Quest.Dynamic.Economic.Supply.%s"), *NeededMaterialTag.ToString());
    DynamicQuest.QuestTag = FGameplayTag::RequestGameplayTag(FName(*UniqueQuestName), false);
    DynamicQuest.Category = EQuestCategory::DynamicEconomic;
    DynamicQuest.InitialStepTag = FGameplayTag::RequestGameplayTag(TEXT("QuestStep.DeliverMaterials"));

    // Step Definition
    FQuestStepDef DeliverStep;
    DeliverStep.StepTag = DynamicQuest.InitialStepTag;

    FQuestObjectiveDef Objective;
    Objective.ObjectiveTag = FGameplayTag::RequestGameplayTag(TEXT("Objective.GatherItem"));
    Objective.ObjectiveType = EQuestObjectiveType::FetchAndDeliver;
    Objective.TargetEntityOrArchetypeTag = NeededMaterialTag;
    Objective.RequiredCount = QuantityNeeded;
    DeliverStep.Objectives.Add(Objective);

    // Completion Branch
    FQuestStepBranchDef CompleteBranch;
    CompleteBranch.BranchTag = FGameplayTag::RequestGameplayTag(TEXT("Branch.FulfillOrder"));
    CompleteBranch.bConcludesQuest = true;
    CompleteBranch.bMarkedAsSuccess = true;
    CompleteBranch.BranchConsequences.CurrencyReward = (QuantityNeeded * 10.0f) * UrgencyMultiplier;
    CompleteBranch.BranchConsequences.AffinityDelta = 0.15f * UrgencyMultiplier;
    CompleteBranch.BranchConsequences.TrustDelta = 0.20f * UrgencyMultiplier;
    DeliverStep.StepBranches.Add(CompleteBranch);

    DynamicQuest.Steps.Add(DeliverStep);

    // Register into GameState Master Registry
    GS->QuestMasterRegistry.Add(DynamicQuest.QuestTag, DynamicQuest);

    // Directly assign to player
    return ServerAcceptQuest(DynamicQuest.QuestTag, GiverGuid);
}

bool UDynamicQuestComponent::ServerGenerateBountyDefenseQuest(AActor* GiverActor, FGameplayTag HostileFactionOrCreatureTag, int32 KillCount, FGameplayTag AreaLandmarkTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !GiverActor || !HostileFactionOrCreatureTag.IsValid()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    UDynamicSocialComponent* GiverSocial = GiverActor->FindComponentByClass<UDynamicSocialComponent>();
    const FGuid GiverGuid = GiverSocial ? GiverSocial->CharacterPersistentGuid : FGuid::NewGuid();

    FQuestDef DynamicBounty;
    const FString UniqueBountyName = FString::Printf(TEXT("Quest.Dynamic.Bounty.%s"), *HostileFactionOrCreatureTag.ToString());
    DynamicBounty.QuestTag = FGameplayTag::RequestGameplayTag(FName(*UniqueBountyName), false);
    DynamicBounty.Category = EQuestCategory::DynamicBounty;
    DynamicBounty.InitialStepTag = FGameplayTag::RequestGameplayTag(TEXT("QuestStep.EliminateThreat"));

    FQuestStepDef SlayStep;
    SlayStep.StepTag = DynamicBounty.InitialStepTag;

    FQuestObjectiveDef Objective;
    Objective.ObjectiveTag = FGameplayTag::RequestGameplayTag(TEXT("Objective.SlayTarget"));
    Objective.ObjectiveType = EQuestObjectiveType::CullTargets;
    Objective.TargetEntityOrArchetypeTag = HostileFactionOrCreatureTag;
    Objective.TargetLocationOrLandmarkTag = AreaLandmarkTag;
    Objective.RequiredCount = KillCount;
    SlayStep.Objectives.Add(Objective);

    FQuestStepBranchDef VictoryBranch;
    VictoryBranch.BranchTag = FGameplayTag::RequestGameplayTag(TEXT("Branch.ThreatEliminated"));
    VictoryBranch.bConcludesQuest = true;
    VictoryBranch.bMarkedAsSuccess = true;
    VictoryBranch.BranchConsequences.CurrencyReward = (KillCount * 25.0f);
    VictoryBranch.BranchConsequences.AffinityDelta = 0.25f;
    VictoryBranch.BranchConsequences.TrustDelta = 0.35f;

    FGenderAttributeModifier XPReward;
    XPReward.TargetAttributeTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Experience"));
    XPReward.ModifierOp = EEffectModifierOp::Additive;
    XPReward.Magnitude = KillCount * 40.0f;
    VictoryBranch.BranchConsequences.AttributeModifiers.Add(XPReward);

    SlayStep.StepBranches.Add(VictoryBranch);
    DynamicBounty.Steps.Add(SlayStep);

    GS->QuestMasterRegistry.Add(DynamicBounty.QuestTag, DynamicBounty);
    return ServerAcceptQuest(DynamicBounty.QuestTag, GiverGuid);
}