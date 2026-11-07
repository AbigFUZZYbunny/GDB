// DynamicStateTreeComponent.cpp
#include "DynamicStateTreeComponent.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "DynamicLanguageComponent.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicInteractableComponent.h"
#include "Engine/AssetManager.h"
#include "DynamicCloudAISubsystem.h"
#include "AbilitySystemComponent.h"
#include "DynamicAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "DynamicLogicEvaluator.h"
#include "Net/UnrealNetwork.h"
#include "Runtime/Engine/Classes/Engine/StreamableManager.h"
#include "Runtime/Engine/Public/Net/UnrealNetwork.h"

UDynamicStateTreeComponent::UDynamicStateTreeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickInterval = 0.05f; // 20 Hz tick
}

void UDynamicStateTreeComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* OwnerActor = GetOwner();
    if (OwnerActor && OwnerActor->HasAuthority())
    {
        CachedCharacter = Cast<ADynamicCharacter>(OwnerActor);
        if (CachedCharacter.IsValid())
        {
            CachedAIController = Cast<AAIController>(CachedCharacter->GetController());
        }
        else
        {
            CachedAIController = Cast<AAIController>(OwnerActor);
            if (CachedAIController.IsValid())
            {
                CachedCharacter = Cast<ADynamicCharacter>(CachedAIController->GetPawn());
            }
        }

        CachedGameState = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

        if (ActiveStateTreeTag.IsValid())
        {
            SwitchToStateTree(ActiveStateTreeTag);
        }
    }
}

void UDynamicStateTreeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopActiveTasks();
    Super::EndPlay(EndPlayReason);
}

void UDynamicStateTreeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        EvaluateTreeTicking(DeltaTime);
    }
}

bool UDynamicStateTreeComponent::SwitchToStateTree(FGameplayTag NewTreeTag)
{
    if (!CachedGameState.IsValid() || !NewTreeTag.IsValid()) return false;

    const FDynamicStateTreeDef* FoundTree = CachedGameState->StateTreeMasterRegistry.Find(NewTreeTag);
    if (!FoundTree || FoundTree->Nodes.Num() == 0) return false;

    ExitCurrentNode();
    StopActiveTasks();

    ActiveStateTreeTag = NewTreeTag;
    ActiveTreeDefinition = *FoundTree;
    bRunningFallbackTree = FoundTree->bIsLLMFallbackTree;

    const int32 InitialNode = SelectBestChildNode(ActiveTreeDefinition.RootNodeIndex);
    EnterNode(InitialNode != INDEX_NONE ? InitialNode : ActiveTreeDefinition.RootNodeIndex);

    return true;
}

void UDynamicStateTreeComponent::TriggerLLMTimeoutFallback()
{
    if (!FallbackStateTreeTag.IsValid() || bRunningFallbackTree) return;

    if (EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger::OnLLMTimeout))
    {
        return;
    }

    SwitchToStateTree(FallbackStateTreeTag);
}

void UDynamicStateTreeComponent::EvaluateTreeTicking(float DeltaTime)
{
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(CurrentNodeIndex)) return;

    TickActiveTasks(DeltaTime);
    EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger::OnTick);
}

bool UDynamicStateTreeComponent::EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger TriggerEvent, const FDynamicStateTreeEvent* EventData)
{
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(CurrentNodeIndex) || !CachedCharacter.IsValid()) return false;

    const FDynamicStateTreeNodeDef& CurrentNode = ActiveTreeDefinition.Nodes[CurrentNodeIndex];
    UAbilitySystemComponent* ASC = CachedCharacter->GetAbilitySystemComponent();

    for (const FDynamicStateTreeTransitionDef& Trans : CurrentNode.Transitions)
    {
        if (Trans.Trigger != TriggerEvent) continue;

        if (TriggerEvent == EDynamicStateTransitionTrigger::OnSensoryEvent)
        {
            if (Trans.EventTagTrigger.IsValid())
            {
                if (!EventData || !EventData->EventTag.MatchesTag(Trans.EventTagTrigger))
                {
                    continue;
                }
            }
        }

        bool bConditionMet = true;
        if (!Trans.ConditionTreeName.IsNone() && CachedGameState.IsValid())
        {
            const FDynamicConditionTreeContainer* ConditionTree = CachedGameState->SharedConditionTrees.Find(Trans.ConditionTreeName);
            if (ConditionTree)
            {
                FGameplayTagContainer SourceTags;
                if (ASC) ASC->GetOwnedGameplayTags(SourceTags);
                bConditionMet = UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, ASC, ASC, SourceTags, SourceTags);
            }
        }

        if (bConditionMet)
        {
            if (Trans.bPopStateFromStack)
            {
                if (PopStateFromStack()) return true;
            }

            int32 DestinationIndex = Trans.TargetNodeIndex;
            if (DestinationIndex == INDEX_NONE && Trans.TargetStateTag.IsValid())
            {
                DestinationIndex = ActiveTreeDefinition.Nodes.IndexOfByPredicate([&Trans](const FDynamicStateTreeNodeDef& Node) {
                    return Node.StateTag == Trans.TargetStateTag;
                });
            }

            if (ActiveTreeDefinition.Nodes.IsValidIndex(DestinationIndex))
            {
                TransitionToState(DestinationIndex, Trans.bPushCurrentStateToStack);
                return true;
            }
        }
    }

    return false;
}

int32 UDynamicStateTreeComponent::SelectBestChildNode(int32 ParentNodeIndex) const
{
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(ParentNodeIndex)) return INDEX_NONE;

    const FDynamicStateTreeNodeDef& ParentNode = ActiveTreeDefinition.Nodes[ParentNodeIndex];
    if (ParentNode.ChildNodeIndices.Num() == 0) return ParentNodeIndex;

    UAbilitySystemComponent* ASC = CachedCharacter.IsValid() ? CachedCharacter->GetAbilitySystemComponent() : nullptr;
    FGameplayTagContainer SourceTags;
    if (ASC) ASC->GetOwnedGameplayTags(SourceTags);

    TArray<int32> ValidChildren;
    for (int32 ChildIdx : ParentNode.ChildNodeIndices)
    {
        if (ActiveTreeDefinition.Nodes.IsValidIndex(ChildIdx))
        {
            if (IsNodeOnCooldown(ChildIdx)) continue; 

            const FDynamicStateTreeNodeDef& ChildNode = ActiveTreeDefinition.Nodes[ChildIdx];
            bool bPasses = true;

            if (!ChildNode.EnterConditionTreeName.IsNone() && CachedGameState.IsValid())
            {
                if (const FDynamicConditionTreeContainer* Tree = CachedGameState->SharedConditionTrees.Find(ChildNode.EnterConditionTreeName))
                {
                    bPasses = UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, SourceTags, SourceTags);
                }
            }

            if (bPasses)
            {
                if (ParentNode.SelectionMode == EDynamicStateSelectionMode::SelectFirstValidChild)
                {
                    return ChildIdx;
                }
                ValidChildren.Add(ChildIdx);
            }
        }
    }

    if (ValidChildren.Num() == 0) return INDEX_NONE;

    if (ParentNode.SelectionMode == EDynamicStateSelectionMode::SelectRandomChild)
    {
        return ValidChildren[FMath::RandRange(0, ValidChildren.Num() - 1)];
    }

    int32 BestIdx = ValidChildren[0];
    float BestPrio = -MAX_flt;

    for (int32 ChildIdx : ValidChildren)
    {
        const float Prio = ActiveTreeDefinition.Nodes[ChildIdx].NodeBasePriority;
        if (Prio > BestPrio)
        {
            BestPrio = Prio;
            BestIdx = ChildIdx;
        }
    }

    return BestIdx;
}

bool UDynamicStateTreeComponent::TransitionToState(int32 TargetNodeIndex, bool bPushToStack)
{
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(TargetNodeIndex)) return false;

    if (bPushToStack && CurrentNodeIndex != INDEX_NONE)
    {
        StateNodeStack.Push(CurrentNodeIndex);
    }

    ExitCurrentNode();
    StopActiveTasks();

    const int32 ResolvedNode = SelectBestChildNode(TargetNodeIndex);
    EnterNode(ResolvedNode != INDEX_NONE ? ResolvedNode : TargetNodeIndex);

    return true;
}


void UDynamicStateTreeComponent::EnterNode(int32 NodeIndex)
{
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(NodeIndex)) return;

    CurrentNodeIndex = NodeIndex;
    const FDynamicStateTreeNodeDef& Node = ActiveTreeDefinition.Nodes[CurrentNodeIndex];

    const FGameplayTag OldTag = CurrentStateTag;
    CurrentStateTag = Node.StateTag;
    OnStateChanged.Broadcast(OldTag, CurrentStateTag);

    if (CachedCharacter.IsValid() && CurrentStateTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode"), false)))
    {
        CachedCharacter->ServerSetMovementState(CurrentStateTag);
    }

    for (const FDynamicStateTreeTaskDef& TaskDef : Node.Tasks)
    {
        StartTask(TaskDef);
    }
}

void UDynamicStateTreeComponent::ExitCurrentNode()
{
    if (CurrentNodeIndex != INDEX_NONE && ActiveTreeDefinition.Nodes.IsValidIndex(CurrentNodeIndex))
    {
        NodeLastExecutionTimes.Add(CurrentNodeIndex, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
    }
    StopActiveTasks();
}

void UDynamicStateTreeComponent::StartTask(const FDynamicStateTreeTaskDef& TaskDef)
{
    if (!CachedCharacter.IsValid()) return;

    FActiveStateTreeTaskState NewTask;
    NewTask.TaskDef = TaskDef;
    NewTask.ElapsedTaskTime = 0.0f;
    NewTask.TargetDuration = 1.0f;
    NewTask.bIsRunning = true;
    NewTask.bWaitingForEQS = false;
    NewTask.ActiveCloudRequestGuid.Invalidate();

    static const FGameplayTag DurationTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.Duration"), false);
    if (const float* DurVal = TaskDef.TaskParameters.LocalLookupMap.Find(DurationTag))
    {
        NewTask.TargetDuration = FMath::Max(0.1f, *DurVal);
    }

    // 1. Task.AI.RequestCloudDirective
    static const FGameplayTag TaskCloudTag = FGameplayTag::RequestGameplayTag(TEXT("Task.AI.RequestCloudDirective"), false);
    if (TaskDef.TaskTypeTag.MatchesTag(TaskCloudTag))
    {
        UDynamicSocialComponent* SocialComp = CachedCharacter->FindComponentByClass<UDynamicSocialComponent>();
        UDynamicCloudAISubsystem* CloudSubsystem = CachedCharacter->GetWorld() ? CachedCharacter->GetWorld()->GetSubsystem<UDynamicCloudAISubsystem>() : nullptr;

        if (SocialComp && CloudSubsystem)
        {
            SocialComp->ServerSetCloudAISuspension(true);
            SocialComp->ServerAcquireConversationalLock(nullptr, true);

            const FString ContextJson = SocialComp->SerializeAgentContextForLLM(ECloudAIRequestType::ComplexDecision);
            const int32 TaskIndex = ActiveTaskStates.Add(NewTask);

            TWeakObjectPtr<UDynamicStateTreeComponent> WeakThis(this);
            FOnCloudAIResponseDirectCallback DirectCallback;
            DirectCallback.BindLambda([WeakThis, TaskIndex](const TArray<FCloudAICommandOutput>& Commands)
            {
                if (UDynamicStateTreeComponent* StrongThis = WeakThis.Get())
                {
                    if (StrongThis->ActiveTaskStates.IsValidIndex(TaskIndex))
                    {
                        StrongThis->ActiveTaskStates[TaskIndex].bIsRunning = false;
                    }
                    StrongThis->EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger::OnStateCompleted);
                }
            });

            FGuid RequestGuid = CloudSubsystem->DispatchDistributedAIRequest(
                CachedCharacter.Get(),
                ECloudAIRequestType::ComplexDecision,
                ECloudAITaskPriority::Medium,
                ContextJson,
                NewTask.TargetDuration,
                FOnCloudAITokenStream(),
                DirectCallback,
                FOnCloudAITaskAssigned()
            );

            ActiveTaskStates[TaskIndex].ActiveCloudRequestGuid = RequestGuid;
            return;
        }
    }

    // 2. Dynamic EQS Spatial Target Resolution
    if (TaskDef.TargetEQSTag.IsValid() && CachedGameState.IsValid())
    {
        NewTask.bWaitingForEQS = true;
        const int32 TaskIndex = ActiveTaskStates.Add(NewTask);

        TWeakObjectPtr<UDynamicStateTreeComponent> WeakThis(this);
        CachedGameState->ExecuteDynamicEQSAsync(
            TaskDef.TargetEQSTag,
            CachedCharacter.Get(),
            TMap<FName, float>(),
            [WeakThis, TaskIndex, TaskDef](bool bSuccess, AActor* BestActor, FVector BestLoc)
            {
                if (UDynamicStateTreeComponent* StrongThis = WeakThis.Get())
                {
                    if (StrongThis->ActiveTaskStates.IsValidIndex(TaskIndex))
                    {
                        FActiveStateTreeTaskState& State = StrongThis->ActiveTaskStates[TaskIndex];
                        State.bWaitingForEQS = false;
                        if (bSuccess)
                        {
                            State.EvaluatedActor = BestActor;
                            State.EvaluatedLocation = BestLoc;

                            if (BestActor)
                            {
                                StrongThis->StateContext.NamedActors.Add(TaskDef.TargetEQSTag, BestActor);
                            }
                            if (!BestLoc.IsZero())
                            {
                                StrongThis->StateContext.NamedLocations.Add(TaskDef.TargetEQSTag, BestLoc);
                            }

                            if (StrongThis->CachedAIController.IsValid())
                            {
                                if (BestActor) StrongThis->CachedAIController->MoveToActor(BestActor, 120.0f);
                                else if (!BestLoc.IsZero()) StrongThis->CachedAIController->MoveToLocation(BestLoc, 120.0f);
                            }
                        }
                    }
                }
            }
        );
        return;
    }

    // 3. Direct Ability or Behavior Activation (GAS Payload Injection)
    if (TaskDef.BehaviorTag.IsValid() && CachedGameState.IsValid())
    {
        if (const FDynamicAIBehaviorDef* BehaviorDef = CachedGameState->AIBehaviorMasterRegistry.Find(TaskDef.BehaviorTag))
        {
            if (BehaviorDef->ExecutionType == ENpcBehaviorExecutionType::Ability && BehaviorDef->AbilityTagToTrigger.IsValid())
            {
                if (UAbilitySystemComponent* ASC = CachedCharacter->GetAbilitySystemComponent())
                {
                    FGameplayEventData Payload;
                    Payload.EventTag = BehaviorDef->AbilityTagToTrigger;
                    Payload.Instigator = CachedCharacter.Get();
                    
                    if (TaskDef.TargetEQSTag.IsValid())
                    {
                        if (TObjectPtr<AActor>* FoundActor = StateContext.NamedActors.Find(TaskDef.TargetEQSTag))
                        {
                            Payload.Target = *FoundActor;
                        }
                    }

                    ASC->TriggerAbilityFromGameplayEvent(
                        FGameplayAbilitySpecHandle(), 
                        ASC->AbilityActorInfo.Get(), 
                        BehaviorDef->AbilityTagToTrigger, 
                        &Payload, 
                        *ASC
                    );
                }
            }
            else if (BehaviorDef->ExecutionType == ENpcBehaviorExecutionType::Montage && BehaviorDef->MontageToPlay.IsValid())
            {
                FSoftObjectPath MontagePath = BehaviorDef->MontageToPlay.ToSoftObjectPath();
                TWeakObjectPtr<ADynamicCharacter> WeakChar(CachedCharacter);

                UAssetManager::GetStreamableManager().RequestAsyncLoad(MontagePath, FStreamableDelegate::CreateLambda([WeakChar, MontagePath]()
                {
                    if (ADynamicCharacter* StrongChar = WeakChar.Get())
                    {
                        if (UAnimMontage* Montage = Cast<UAnimMontage>(MontagePath.ResolveObject()))
                        {
                            if (USkeletalMeshComponent* MeshComp = StrongChar->GetMeshComponent())
                            {
                                if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                                {
                                    AnimInst->Montage_Play(Montage, 1.0f);
                                }
                            }
                        }
                    }
                }));
            }
        }
    }

    ActiveTaskStates.Add(NewTask);
}

void UDynamicStateTreeComponent::TickActiveTasks(float DeltaTime)
{
    bool bAnyTaskFinished = false;

    // 1. Localized copy of currently executing task indices
    // This prevents iteration invalidation if EvaluateTransitionsForCurrentNode adds/removes tasks mid-tick
    TArray<int32> EvaluatedTaskIndices;
    EvaluatedTaskIndices.Reserve(ActiveTaskStates.Num());
    for (int32 i = 0; i < ActiveTaskStates.Num(); ++i)
    {
        EvaluatedTaskIndices.Add(i);
    }

    for (int32 i : EvaluatedTaskIndices)
    {
        // Safe check in case a prior loop evaluation already cleared the array
        if (!ActiveTaskStates.IsValidIndex(i)) continue;
        
        FActiveStateTreeTaskState& TaskState = ActiveTaskStates[i];
        if (!TaskState.bIsRunning || TaskState.bWaitingForEQS) continue;

        TaskState.ElapsedTaskTime += DeltaTime;

        // PathFollowing completion check
        if (CachedAIController.IsValid() && TaskState.TaskDef.TaskTypeTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Task.AI.Movement"), false)))
        {
            if (CachedAIController->GetMoveStatus() == EPathFollowingStatus::Idle)
            {
                TaskState.bIsRunning = false;
                bAnyTaskFinished = true;
                continue;
            }
        }

        // Timeout check for Cloud LLM Tasks
        static const FGameplayTag TaskCloudTag = FGameplayTag::RequestGameplayTag(TEXT("Task.AI.RequestCloudDirective"), false);
        if (TaskState.TaskDef.TaskTypeTag.MatchesTag(TaskCloudTag))
        {
            if (TaskState.ElapsedTaskTime >= TaskState.TargetDuration)
            {
                TaskState.bIsRunning = false;
                TriggerLLMTimeoutFallback();
                return;
            }
        }
        else if (TaskState.ElapsedTaskTime >= TaskState.TargetDuration)
        {
            TaskState.bIsRunning = false;
            bAnyTaskFinished = true;
        }
    }

    // 2. Perform transitions only after all active tasks have ticked cleanly
    if (bAnyTaskFinished)
    {
        EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger::OnStateCompleted);
    }
}

void UDynamicStateTreeComponent::StopActiveTasks()
{
    for (FActiveStateTreeTaskState& TaskState : ActiveTaskStates)
    {
        TaskState.bIsRunning = false;
        TaskState.bWaitingForEQS = false;

        if (TaskState.ActiveCloudRequestGuid.IsValid() && CachedCharacter.IsValid())
        {
            if (UWorld* World = CachedCharacter->GetWorld())
            {
                if (UDynamicCloudAISubsystem* CloudSubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
                {
                    CloudSubsystem->CancelRemoteAIRequest(TaskState.ActiveCloudRequestGuid);
                }
            }
        }
    }

    if (CachedAIController.IsValid())
    {
        CachedAIController->StopMovement();
    }

    ActiveTaskStates.Empty();
}

void UDynamicStateTreeComponent::SendStateTreeEvent(const FDynamicStateTreeEvent& EventPayload)
{
    if (!EventPayload.EventTag.IsValid() || !GetOwner() || !GetOwner()->HasAuthority()) return;

    if (EventPayload.EventInstigator)
    {
        StateContext.NamedActors.Add(EventPayload.EventTag, EventPayload.EventInstigator);
    }
    if (!EventPayload.EventLocation.IsZero())
    {
        StateContext.NamedLocations.Add(EventPayload.EventTag, EventPayload.EventLocation);
    }
    if (EventPayload.EventMagnitude != 0.0f)
    {
        StateContext.NamedParameters.Add(EventPayload.EventTag, EventPayload.EventMagnitude);
    }

    EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger::OnSensoryEvent, &EventPayload);
}

bool UDynamicStateTreeComponent::IsNodeOnCooldown(int32 NodeIndex) const
{
    if (const double* LastTime = NodeLastExecutionTimes.Find(NodeIndex))
    {
        if (ActiveTreeDefinition.Nodes.IsValidIndex(NodeIndex))
        {
            const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            return (Now - *LastTime) < ActiveTreeDefinition.Nodes[NodeIndex].NodeCooldownSeconds;
        }
    }
    return false;
}

bool UDynamicStateTreeComponent::PopStateFromStack()
{
    if (StateNodeStack.Num() == 0) return false;

    const int32 PreviousNode = StateNodeStack.Pop();
    if (!ActiveTreeDefinition.Nodes.IsValidIndex(PreviousNode)) return false;

    ExitCurrentNode();
    StopActiveTasks();
    EnterNode(PreviousNode);
    return true;
}

void UDynamicStateTreeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicStateTreeComponent, ActiveStateTreeTag);
    DOREPLIFETIME(UDynamicStateTreeComponent, FallbackStateTreeTag);
    DOREPLIFETIME(UDynamicStateTreeComponent, CurrentStateTag);
}

void UDynamicStateTreeComponent::OnRep_ActiveStateTreeTag()
{
    if (CachedGameState.IsValid() && ActiveStateTreeTag.IsValid())
    {
        if (const FDynamicStateTreeDef* Def = CachedGameState->StateTreeMasterRegistry.Find(ActiveStateTreeTag))
        {
            ActiveTreeDefinition = *Def;
        }
    }
}

void UDynamicStateTreeComponent::OnRep_CurrentStateTag()
{
    OnStateChanged.Broadcast(FGameplayTag(), CurrentStateTag);
}