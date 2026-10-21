// BTTask_RequestCloudDirective.cpp
#include "BTTask_RequestCloudDirective.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DynamicCloudAISubsystem.h"
#include "DynamicSocialComponent.h"
#include "DynamicCharacter.h"

UBTTask_RequestCloudDirective::UBTTask_RequestCloudDirective()
{
    NodeName = "Request Cloud Directive";
    bNotifyTick = false;
    bCreateNodeInstance = false; // Shared node definition using node memory

    ActionTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, ActionTagKey));
    TargetPlaceRoleKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, TargetPlaceRoleKey));
    UtteranceTextKey.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, UtteranceTextKey));
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    FBTRequestCloudDirectiveMemory* TaskMemory = reinterpret_cast<FBTRequestCloudDirectiveMemory*>(NodeMemory);
    TaskMemory->ActiveRequestGuid.Invalidate();
    TaskMemory->OwnerTreeComp = &OwnerComp;

    AAIController* AICon = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    if (!ControlledPawn || !ControlledPawn->HasAuthority())
    {
        return EBTNodeResult::Failed;
    }

    UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>();
    if (!SocialComp)
    {
        return EBTNodeResult::Failed;
    }

    UWorld* World = ControlledPawn->GetWorld();
    UDynamicCloudAISubsystem* CloudSubsystem = World ? World->GetSubsystem<UDynamicCloudAISubsystem>() : nullptr;
    if (!CloudSubsystem)
    {
        return EBTNodeResult::Failed;
    }

    CachedCloudSubsystem = CloudSubsystem;

    // Serialize agent context and notify social component of suspension
    SocialComp->ServerSetCloudAISuspension(true);
    const FString ContextJson = SocialComp->SerializeAgentContextForLLM(RequestType);

    TaskMemory->ActiveRequestGuid = CloudSubsystem->DispatchRemoteAIRequest(ControlledPawn, RequestType, ContextJson, RequestTimeoutSeconds);

    // Bind callback to subsystem
    CloudSubsystem->OnRemoteAIResponseReceived.AddUniqueDynamic(this, &UBTTask_RequestCloudDirective::HandleCloudAIResponse);

    return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    FBTRequestCloudDirectiveMemory* TaskMemory = reinterpret_cast<FBTRequestCloudDirectiveMemory*>(NodeMemory);

    if (CachedCloudSubsystem.IsValid())
    {
        CachedCloudSubsystem->OnRemoteAIResponseReceived.RemoveDynamic(this, &UBTTask_RequestCloudDirective::HandleCloudAIResponse);
    }

    AAIController* AICon = OwnerComp.GetAIOwner();
    if (APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr)
    {
        if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerSetCloudAISuspension(false);
        }
    }

    TaskMemory->ActiveRequestGuid.Invalidate();
    return EBTNodeResult::Aborted;
}

void UBTTask_RequestCloudDirective::HandleCloudAIResponse(const FGuid& ResponseGuid, const TArray<FCloudAICommandOutput>& Commands)
{
    // Search active task memory instances across trees
    if (!CachedCloudSubsystem.IsValid()) return;

    for (FConstControllerIterator It = CachedCloudSubsystem->GetWorld()->GetControllerIterator(); It; ++It)
    {
        if (AAIController* AICon = Cast<AAIController>(It->Get()))
        {
            if (UBehaviorTreeComponent* BTC = Cast<UBehaviorTreeComponent>(AICon->GetBrainComponent()))
            {
                uint8* RawMemory = BTC->GetNodeMemory(this, BTC->FindInstanceContainingNode(this));
                if (!RawMemory) continue;

                FBTRequestCloudDirectiveMemory* TaskMemory = reinterpret_cast<FBTRequestCloudDirectiveMemory*>(RawMemory);
                if (TaskMemory->ActiveRequestGuid == ResponseGuid)
                {
                    TaskMemory->ActiveRequestGuid.Invalidate();

                    if (APawn* ControlledPawn = AICon->GetPawn())
                    {
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

                    // Fallback if timeout or empty commands returned
                    FinishLatentTask(*BTC, EBTNodeResult::Failed);
                    return;
                }
            }
        }
    }
}