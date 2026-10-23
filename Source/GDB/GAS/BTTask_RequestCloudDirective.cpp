// BTTask_RequestCloudDirective.cpp
#include "BTTask_RequestCloudDirective.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DynamicCloudAISubsystem.h"
#include "DynamicSocialComponent.h"
#include "DynamicCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

UBTTask_RequestCloudDirective::UBTTask_RequestCloudDirective()
{
    NodeName = "Request Distributed Cloud Directive";
    bNotifyTick = true;
    bCreateNodeInstance = true;

    ActionTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, ActionTagKey));
    TargetPlaceRoleKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, TargetPlaceRoleKey));
    UtteranceTextKey.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, UtteranceTextKey));

    // Default Vamping filler phrases
    VampingConfig.FillerUtterances.Add(TEXT("Hmm... let me consider that."));
    VampingConfig.FillerUtterances.Add(TEXT("Give me a moment to think..."));
    VampingConfig.FillerUtterances.Add(TEXT("Now, what was that again?"));
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    ActiveRequestGuid.Invalidate();
    ElapsedTaskTime = 0.0f;
    DynamicTimeoutLimit = BaseRequestTimeoutSeconds;
    bVampingStarted = false;
    bUtteranceStarted = false;
    LiveUtteranceBuffer.Empty();
    ActiveVampingMontage.Reset();

    AAIController* AICon = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    if (!ControlledPawn || !ControlledPawn->HasAuthority()) return EBTNodeResult::Failed;

    UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>();
    UDynamicCloudAISubsystem* CloudSubsystem = ControlledPawn->GetWorld()->GetSubsystem<UDynamicCloudAISubsystem>();
    if (!SocialComp || !CloudSubsystem) return EBTNodeResult::Failed;

    CachedCloudSubsystem = CloudSubsystem;
    SocialComp->ServerSetCloudAISuspension(true);
    const FString ContextJson = SocialComp->SerializeAgentContextForLLM(RequestType);

    TWeakObjectPtr<UBTTask_RequestCloudDirective> WeakThis(this);
    TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp(&OwnerComp);

    // Bind using the updated global delegates
    FOnCloudAITokenStream StreamCallback;
    StreamCallback.BindLambda([WeakThis, WeakOwnerComp](const FString& Chunk) {
        if (UBTTask_RequestCloudDirective* StrongThis = WeakThis.Get()) StrongThis->HandleTokenStream(Chunk, WeakOwnerComp);
    });

    FOnCloudAIResponseDirectCallback DirectCallback;
    DirectCallback.BindLambda([WeakThis, WeakOwnerComp](const TArray<FCloudAICommandOutput>& Commands) {
        if (UBTTask_RequestCloudDirective* StrongThis = WeakThis.Get()) StrongThis->HandleDirectCloudResponse(WeakOwnerComp, Commands);
    });

    FOnCloudAITaskAssigned AssignedCallback;
    AssignedCallback.BindLambda([WeakThis](float EstimatedLatency) {
        if (UBTTask_RequestCloudDirective* StrongThis = WeakThis.Get()) StrongThis->HandleTaskAssigned(EstimatedLatency);
    });

    ActiveRequestGuid = CloudSubsystem->DispatchDistributedAIRequest(
        ControlledPawn, RequestType, TaskPriority, ContextJson, BaseRequestTimeoutSeconds, 
        StreamCallback, DirectCallback, AssignedCallback
    );

    return EBTNodeResult::InProgress;
}

void UBTTask_RequestCloudDirective::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);
    ElapsedTaskTime += DeltaSeconds;

    // Timeout guard adjusted dynamically by network latency
    if (ElapsedTaskTime >= DynamicTimeoutLimit)
    {
        AAIController* AICon = OwnerComp.GetAIOwner();
        APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
        if (ControlledPawn)
        {
            StopVampingBehavior(ControlledPawn);
            if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
            {
                SocialComp->ServerSetCloudAISuspension(false);
            }
        }
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    // Trigger latency-masking vamping chatter/animation
    if (VampingConfig.bEnableVamping && !bVampingStarted && !bUtteranceStarted && ElapsedTaskTime >= VampingConfig.VampingStartDelay)
    {
        AAIController* AICon = OwnerComp.GetAIOwner();
        APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
        if (ControlledPawn)
        {
            StartVampingBehavior(ControlledPawn, OwnerComp);
            bVampingStarted = true;
        }
    }
}

void UBTTask_RequestCloudDirective::StartVampingBehavior(APawn* ControlledPawn, UBehaviorTreeComponent& OwnerComp)
{
    // 1. Play contextual thinking animation
    if (VampingConfig.ThinkingMontages.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, VampingConfig.ThinkingMontages.Num() - 1);
        if (UAnimMontage* Montage = VampingConfig.ThinkingMontages[RandIdx].LoadSynchronous())
        {
            if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(ControlledPawn))
            {
                if (USkeletalMeshComponent* MeshComp = DynChar->GetMeshComponent())
                {
                    if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                    {
                        AnimInst->Montage_Play(Montage, 1.0f);
                        ActiveVampingMontage = Montage;
                    }
                }
            }
        }
    }

    // 2. Set temporary filler subtitle / utterance
    if (VampingConfig.FillerUtterances.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, VampingConfig.FillerUtterances.Num() - 1);
        const FString FillerText = VampingConfig.FillerUtterances[RandIdx];
        UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
        if (BB && UtteranceTextKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsString(UtteranceTextKey.SelectedKeyName, FillerText);
        }
    }
}

void UBTTask_RequestCloudDirective::StopVampingBehavior(APawn* ControlledPawn)
{
    if (ActiveVampingMontage.IsValid())
    {
        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(ControlledPawn))
        {
            if (USkeletalMeshComponent* MeshComp = DynChar->GetMeshComponent())
            {
                if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                {
                    AnimInst->Montage_Stop(0.2f, ActiveVampingMontage.Get());
                }
            }
        }
        ActiveVampingMontage.Reset();
    }
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (CachedCloudSubsystem.IsValid() && ActiveRequestGuid.IsValid())
    {
        CachedCloudSubsystem->CancelRemoteAIRequest(ActiveRequestGuid);
    }

    AAIController* AICon = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    if (ControlledPawn)
    {
        StopVampingBehavior(ControlledPawn);
        if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerSetCloudAISuspension(false);
        }
    }

    ActiveRequestGuid.Invalidate();
    return EBTNodeResult::Aborted;
}

void UBTTask_RequestCloudDirective::HandleTaskAssigned(float EstimatedLatencySeconds)
{
    DynamicTimeoutLimit = BaseRequestTimeoutSeconds + EstimatedLatencySeconds;
}

void UBTTask_RequestCloudDirective::HandleTokenStream(const FString& TokenChunk, TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp)
{
    UBehaviorTreeComponent* BTC = OwnerTreeComp.Get();
    if (!BTC) return;

    if (!bUtteranceStarted)
    {
        bUtteranceStarted = true;
        if (AAIController* AICon = BTC->GetAIOwner())
        {
            StopVampingBehavior(AICon->GetPawn());
        }
    }

    LiveUtteranceBuffer += TokenChunk;

    if (UBlackboardComponent* BB = BTC->GetBlackboardComponent())
    {
        if (UtteranceTextKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsString(UtteranceTextKey.SelectedKeyName, LiveUtteranceBuffer);
        }
    }
}

void UBTTask_RequestCloudDirective::HandleDirectCloudResponse(TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp, const TArray<FCloudAICommandOutput>& Commands)
{
    ActiveRequestGuid.Invalidate();
    UBehaviorTreeComponent* BTC = OwnerTreeComp.Get();
    if (!BTC) return;

    AAIController* AICon = BTC->GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    if (ControlledPawn)
    {
        StopVampingBehavior(ControlledPawn);
        if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerSetCloudAISuspension(false);
        }
    }

    UBlackboardComponent* BB = BTC->GetBlackboardComponent();
    if (BB && Commands.Num() > 0)
    {
        const FCloudAICommandOutput& PrimaryCmd = Commands[0];
        if (ActionTagKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsName(ActionTagKey.SelectedKeyName, PrimaryCmd.CommandActionTag.GetTagName());
        }
        if (TargetPlaceRoleKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsName(TargetPlaceRoleKey.SelectedKeyName, PrimaryCmd.TargetEntityOrPlaceRoleTag.GetTagName());
        }
        if (UtteranceTextKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsString(UtteranceTextKey.SelectedKeyName, PrimaryCmd.UtteranceText);
        }

        FinishLatentTask(*BTC, EBTNodeResult::Succeeded);
        return;
    }

    FinishLatentTask(*BTC, EBTNodeResult::Failed);
}