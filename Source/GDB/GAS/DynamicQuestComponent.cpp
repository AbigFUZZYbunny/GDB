// DynamicQuestComponent.cpp
#include "DynamicQuestComponent.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "Net/UnrealNetwork.h"

UDynamicQuestComponent::UDynamicQuestComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedActiveQuests.OwningQuestComp = this;
    ReplicatedActiveObjectives.OwningQuestComp = this;
}

void UDynamicQuestComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedActiveQuests.OwningQuestComp = this;
    ReplicatedActiveObjectives.OwningQuestComp = this;
}

void UDynamicQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicQuestComponent, ReplicatedActiveQuests);
    DOREPLIFETIME(UDynamicQuestComponent, ReplicatedActiveObjectives);
    DOREPLIFETIME(UDynamicQuestComponent, CompletedQuestTags);
}

void FDynamicActiveQuestItem::PreReplicatedRemove(const FFastActiveQuestArray& InArrayOwner) {}
void FDynamicActiveQuestItem::PostReplicatedAdd(const FFastActiveQuestArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleSingleQuestReplicated(*this);
}
void FDynamicActiveQuestItem::PostReplicatedChange(const FFastActiveQuestArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleSingleQuestReplicated(*this);
}

void FDynamicActiveQuestObjectiveItem::PostReplicatedAdd(const FFastActiveQuestObjectiveArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleSingleObjectiveReplicated(*this);
}
void FDynamicActiveQuestObjectiveItem::PostReplicatedChange(const FFastActiveQuestObjectiveArray& InArrayOwner)
{
    if (InArrayOwner.OwningQuestComp) InArrayOwner.OwningQuestComp->HandleSingleObjectiveReplicated(*this);
}

void UDynamicQuestComponent::OnRep_ActiveQuests() {}
void UDynamicQuestComponent::OnRep_ActiveObjectives() {}

void UDynamicQuestComponent::HandleSingleQuestReplicated(const FDynamicActiveQuestItem& Item)
{
    OnQuestStateChanged.Broadcast(Item.QuestInstanceGuid, Item.QuestDefTag, Item.StepState);
}

void UDynamicQuestComponent::HandleSingleObjectiveReplicated(const FDynamicActiveQuestObjectiveItem& Item)
{
    OnObjectiveProgressChanged.Broadcast(Item.QuestInstanceGuid, Item.ObjectiveTag, Item.CurrentProgress, Item.RequiredCount);
}

bool UDynamicQuestComponent::IsQuestAvailable(FGameplayTag QuestDefTag, const AActor* QueryingActor, const FGuid& QuestGiverGuid) const
{
    if (!QuestDefTag.IsValid() || CompletedQuestTags.HasTag(QuestDefTag)) return false;

    for (const FDynamicActiveQuestItem& Item : ReplicatedActiveQuests.Items)
    {
        if (Item.QuestDefTag == QuestDefTag) return false;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FQuestDef* Def = GS->QuestMasterRegistry.Find(QuestDefTag);
    if (!Def) return false;

    if (QueryingActor && QuestGiverGuid.IsValid())
    {
        if (const UDynamicSocialComponent* SocialComp = QueryingActor->FindComponentByClass<UDynamicSocialComponent>())
        {
            for (const FTagFloatPair& ReqMetric : Def->MinimumRelationshipMetricRequirements)
            {
                if (ReqMetric.KeyTag.IsValid())
                {
                    const float CurrentVal = SocialComp->GetDecayedRelationshipMetric(QuestGiverGuid, ReqMetric.KeyTag);
                    if (CurrentVal < ReqMetric.Value)
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

void UDynamicQuestComponent::InitializeStepObjectives(const FGuid& QuestGuid, const FQuestStepDef& StepDef)
{
    for (const FQuestObjectiveDef& ObjDef : StepDef.Objectives)
    {
        FDynamicActiveQuestObjectiveItem NewObj;
        NewObj.QuestInstanceGuid = QuestGuid;
        NewObj.ObjectiveTag = ObjDef.ObjectiveTag;
        NewObj.CurrentProgress = 0;
        NewObj.RequiredCount = ObjDef.RequiredCount;
        NewObj.bIsComplete = false;

        FDynamicActiveQuestObjectiveItem& Added = ReplicatedActiveObjectives.Items.Add_GetRef(NewObj);
        ReplicatedActiveObjectives.MarkItemDirty(Added);
    }
}

void UDynamicQuestComponent::CleanUpObjectivesForQuest(const FGuid& QuestGuid)
{
    const int32 Removed = ReplicatedActiveObjectives.Items.RemoveAll([QuestGuid](const FDynamicActiveQuestObjectiveItem& Obj)
    {
        return Obj.QuestInstanceGuid == QuestGuid;
    });

    if (Removed > 0)
    {
        ReplicatedActiveObjectives.MarkArrayDirty();
    }
}

FGuid UDynamicQuestComponent::ServerAcceptQuest(FGameplayTag QuestDefTag, const FGuid& QuestGiverGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !QuestDefTag.IsValid()) return FGuid();

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGuid();

    const FQuestDef* Def = GS->QuestMasterRegistry.Find(QuestDefTag);
    if (!Def) return FGuid();

    const FGuid NewGuid = FGuid::NewGuid();

    FDynamicActiveQuestItem NewQuest;
    NewQuest.QuestInstanceGuid = NewGuid;
    NewQuest.QuestDefTag = QuestDefTag;
    NewQuest.QuestGiverGuid = QuestGiverGuid;
    NewQuest.CurrentStepTag = Def->InitialStepTag;
    NewQuest.StepState = EQuestStepState::Active;
    NewQuest.StepStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    FDynamicActiveQuestItem& Added = ReplicatedActiveQuests.Items.Add_GetRef(NewQuest);
    ReplicatedActiveQuests.MarkItemDirty(Added);

    for (const FQuestStepDef& Step : Def->Steps)
    {
        if (Step.StepTag == Def->InitialStepTag)
        {
            InitializeStepObjectives(NewGuid, Step);
            break;
        }
    }

    OnQuestStateChanged.Broadcast(NewGuid, QuestDefTag, EQuestStepState::Active);
    return NewGuid;
}

void UDynamicQuestComponent::ServerIncrementObjectiveProgress(FGameplayTag ObjectiveTag, int32 IncrementAmount, FGameplayTag TargetEntityOrArchetypeTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ObjectiveTag.IsValid() || IncrementAmount <= 0) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    for (int32 q = 0; q < ReplicatedActiveQuests.Items.Num(); ++q)
    {
        FDynamicActiveQuestItem& QuestItem = ReplicatedActiveQuests.Items[q];
        if (QuestItem.StepState != EQuestStepState::Active) continue;

        const FQuestDef* Def = GS->QuestMasterRegistry.Find(QuestItem.QuestDefTag);
        if (!Def) continue;

        const FQuestStepDef* CurrentStep = Def->Steps.FindByPredicate([&QuestItem](const FQuestStepDef& Step) {
            return Step.StepTag == QuestItem.CurrentStepTag;
        });
        if (!CurrentStep) continue;

        bool bStepCompleted = true;

        for (FDynamicActiveQuestObjectiveItem& ObjItem : ReplicatedActiveObjectives.Items)
        {
            if (ObjItem.QuestInstanceGuid == QuestItem.QuestInstanceGuid)
            {
                if (ObjItem.ObjectiveTag == ObjectiveTag && !ObjItem.bIsComplete)
                {
                    ObjItem.CurrentProgress = FMath::Min(ObjItem.CurrentProgress + IncrementAmount, ObjItem.RequiredCount);
                    if (ObjItem.CurrentProgress >= ObjItem.RequiredCount)
                    {
                        ObjItem.bIsComplete = true;
                    }
                    // ONLY THE SPECIFIC OBJECTIVE IS SERIALIZED ACROSS THE WIRE
                    ReplicatedActiveObjectives.MarkItemDirty(ObjItem);
                    OnObjectiveProgressChanged.Broadcast(QuestItem.QuestInstanceGuid, ObjItem.ObjectiveTag, ObjItem.CurrentProgress, ObjItem.RequiredCount);
                }

                if (!ObjItem.bIsComplete)
                {
                    bStepCompleted = false;
                }
            }
        }

        if (bStepCompleted && CurrentStep->bAutoAdvanceOnObjectivesCompleted)
        {
            if (CurrentStep->StepBranches.Num() > 0)
            {
                const FQuestStepBranchDef& Branch = CurrentStep->StepBranches[0];
                if (Branch.bConcludesQuest)
                {
                    CompleteQuestInternal(q, Branch.bMarkedAsSuccess);
                }
                else
                {
                    AdvanceQuestStep(q, Branch.NextStepTag);
                }
            }
            else
            {
                CompleteQuestInternal(q, true);
            }
            break;
        }
    }
}

void UDynamicQuestComponent::AdvanceQuestStep(int32 QuestIndex, FGameplayTag NextStepTag)
{
    if (!ReplicatedActiveQuests.Items.IsValidIndex(QuestIndex)) return;

    FDynamicActiveQuestItem& QuestItem = ReplicatedActiveQuests.Items[QuestIndex];
    CleanUpObjectivesForQuest(QuestItem.QuestInstanceGuid);

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FQuestDef* Def = GS->QuestMasterRegistry.Find(QuestItem.QuestDefTag);
    if (!Def) return;

    QuestItem.CurrentStepTag = NextStepTag;
    QuestItem.StepStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    ReplicatedActiveQuests.MarkItemDirty(QuestItem);

    for (const FQuestStepDef& Step : Def->Steps)
    {
        if (Step.StepTag == NextStepTag)
        {
            InitializeStepObjectives(QuestItem.QuestInstanceGuid, Step);
            break;
        }
    }

    OnQuestStateChanged.Broadcast(QuestItem.QuestInstanceGuid, QuestItem.QuestDefTag, EQuestStepState::Active);
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

    CleanUpObjectivesForQuest(CompletedItem.QuestInstanceGuid);

    OnQuestStateChanged.Broadcast(CompletedItem.QuestInstanceGuid, CompletedItem.QuestDefTag, CompletedItem.StepState);

    ReplicatedActiveQuests.Items.RemoveAtSwap(ItemIndex);
    ReplicatedActiveQuests.MarkArrayDirty();
}

bool UDynamicQuestComponent::ServerAbandonQuest(const FGuid& QuestInstanceGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !QuestInstanceGuid.IsValid()) return false;

    const int32 FoundIdx = ReplicatedActiveQuests.Items.IndexOfByPredicate([QuestInstanceGuid](const FDynamicActiveQuestItem& Item) {
        return Item.QuestInstanceGuid == QuestInstanceGuid;
    });

    if (FoundIdx != INDEX_NONE)
    {
        CompleteQuestInternal(FoundIdx, false);
        return true;
    }
    return false;
}

bool UDynamicQuestComponent::GetQuestObjectiveProgress(const FGuid& QuestInstanceGuid, FGameplayTag ObjectiveTag, FDynamicActiveQuestObjectiveItem& OutObjective) const
{
    for (const FDynamicActiveQuestObjectiveItem& Obj : ReplicatedActiveObjectives.Items)
    {
        if (Obj.QuestInstanceGuid == QuestInstanceGuid && Obj.ObjectiveTag == ObjectiveTag)
        {
            OutObjective = Obj;
            return true;
        }
    }
    return false;
}

TArray<FDynamicActiveQuestObjectiveItem> UDynamicQuestComponent::GetObjectivesForQuest(const FGuid& QuestInstanceGuid) const
{
    TArray<FDynamicActiveQuestObjectiveItem> OutList;
    for (const FDynamicActiveQuestObjectiveItem& Obj : ReplicatedActiveObjectives.Items)
    {
        if (Obj.QuestInstanceGuid == QuestInstanceGuid)
        {
            OutList.Add(Obj);
        }
    }
    return OutList;
}

void UDynamicQuestComponent::ApplyRewardConsequences(const FQuestRewardConsequence& Consequences, const FGuid& QuestGiverGuid)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) return;

    if (UDynamicSocialComponent* SocialComp = Owner->FindComponentByClass<UDynamicSocialComponent>())
    {
        if (QuestGiverGuid.IsValid())
        {
            TArray<FTagFloatPair> MetricDeltas;
            if (!FMath::IsNearlyZero(Consequences.AffinityDelta))
            {
                FTagFloatPair AffDelta;
                AffDelta.KeyTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Affinity"), false);
                AffDelta.Value = Consequences.AffinityDelta;
                MetricDeltas.Add(AffDelta);
            }
            if (!FMath::IsNearlyZero(Consequences.TrustDelta))
            {
                FTagFloatPair TrustDelta;
                TrustDelta.KeyTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Trust"), false);
                TrustDelta.Value = Consequences.TrustDelta;
                MetricDeltas.Add(TrustDelta);
            }

            if (MetricDeltas.Num() > 0)
            {
                SocialComp->ServerRecordSocialInteraction(
                    QuestGiverGuid,
                    nullptr,
                    ERelationshipTargetKind::Character,
                    FGameplayTag::RequestGameplayTag(TEXT("Relationship.Type.Friendship"), false),
                    MetricDeltas,
                    FGameplayTagContainer()
                );
            }
        }
    }
}