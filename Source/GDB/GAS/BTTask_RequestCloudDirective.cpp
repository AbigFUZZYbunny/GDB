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
    // Enable unique node instance per behavior tree component to avoid cross-NPC state contamination
    bCreateNodeInstance = true;

    ActionTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, ActionTagKey));
    TargetPlaceRoleKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, TargetPlaceRoleKey));
    UtteranceTextKey.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_RequestCloudDirective, UtteranceTextKey));
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    ActiveRequestGuid.Invalidate();

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
    SocialComp->ServerSetCloudAISuspension(true);

    const FString ContextJson = SocialComp->SerializeAgentContextForLLM(RequestType);

    // Register direct O(1) callback bound to this specific node instance
    TWeakObjectPtr<UBTTask_RequestCloudDirective> WeakThis(this);
    TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp(&OwnerComp);

    FOnCloudAIResponseDirectCallback DirectCallback;
    DirectCallback.BindLambda([WeakThis, WeakOwnerComp](const TArray<FCloudAICommandOutput>& Commands)
    {
        if (UBTTask_RequestCloudDirective* StrongThis = WeakThis.Get())
        {
            StrongThis->HandleDirectCloudResponse(WeakOwnerComp, Commands);
        }
    });

    ActiveRequestGuid = CloudSubsystem->DispatchRemoteAIRequestNative(
        ControlledPawn, 
        RequestType, 
        ContextJson, 
        RequestTimeoutSeconds, 
        DirectCallback
    );

    return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_RequestCloudDirective::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (CachedCloudSubsystem.IsValid() && ActiveRequestGuid.IsValid())
    {
        CachedCloudSubsystem->CancelRemoteAIRequest(ActiveRequestGuid);
    }

    AAIController* AICon = OwnerComp.GetAIOwner();
    if (APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr)
    {
        if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerSetCloudAISuspension(false);
        }
    }

    ActiveRequestGuid.Invalidate();
    return EBTNodeResult::Aborted;
}

void UBTTask_RequestCloudDirective::HandleDirectCloudResponse(TWeakObjectPtr<UBehaviorTreeComponent> OwnerTreeComp, const TArray<FCloudAICommandOutput>& Commands)
{
    ActiveRequestGuid.Invalidate();

    UBehaviorTreeComponent* BTC = OwnerTreeComp.Get();
    if (!BTC) return;

    AAIController* AICon = BTC->GetAIOwner();
    if (APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr)
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

    FinishLatentTask(*BTC, EBTNodeResult::Failed);
}