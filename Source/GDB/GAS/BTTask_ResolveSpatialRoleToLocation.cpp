// BTTask_ResolveSpatialRoleToLocation.cpp
#include "BTTask_ResolveSpatialRoleToLocation.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"

UBTTask_ResolveSpatialRoleToLocation::UBTTask_ResolveSpatialRoleToLocation()
{
    NodeName = "Resolve Spatial Role To Location";
    bNotifyTick = false;

    RoleTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ResolveSpatialRoleToLocation, RoleTagKey));
    RoleTagKey.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ResolveSpatialRoleToLocation, RoleTagKey));

    TargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ResolveSpatialRoleToLocation, TargetLocationKey));
    TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ResolveSpatialRoleToLocation, TargetActorKey), AActor::StaticClass());
}

EBTNodeResult::Type UBTTask_ResolveSpatialRoleToLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    const AAIController* AICon = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();

    if (!ControlledPawn || !BB)
    {
        return EBTNodeResult::Failed;
    }

    // 1. Extract Role Tag Name safely using key type reflection
    FName RoleName = NAME_None;
    const FBlackboard::FKey KeyID = BB->GetKeyID(RoleTagKey.SelectedKeyName);

    if (KeyID != FBlackboard::InvalidKey)
    {
        const TSubclassOf<UBlackboardKeyType> KeyType = BB->GetKeyType(KeyID);
        if (KeyType)
        {
            if (KeyType->IsChildOf(UBlackboardKeyType_Name::StaticClass()))
            {
                RoleName = BB->GetValueAsName(RoleTagKey.SelectedKeyName);
            }
            else if (KeyType->IsChildOf(UBlackboardKeyType_String::StaticClass()))
            {
                const FString StringVal = BB->GetValueAsString(RoleTagKey.SelectedKeyName);
                if (!StringVal.IsEmpty())
                {
                    RoleName = FName(*StringVal);
                }
            }
        }
    }

    if (RoleName.IsNone())
    {
        return EBTNodeResult::Failed;
    }

    const FGameplayTag RoleTag = FGameplayTag::RequestGameplayTag(RoleName, false);
    if (!RoleTag.IsValid())
    {
        return EBTNodeResult::Failed;
    }

    UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>();

    // 2. Query Agent Social Knowledge Cache First
    if (bCheckSocialKnowledgeFirst && SocialComp)
    {
        FDynamicKnownPlaceItem KnownPlace;
        if (SocialComp->GetKnownPlaceByRole(RoleTag, KnownPlace) && KnownPlace.ConfidenceScore > 0.05f)
        {
            BB->SetValueAsVector(TargetLocationKey.SelectedKeyName, FVector(KnownPlace.LastKnownLocation));
            return EBTNodeResult::Succeeded;
        }
    }

    // 3. O(1) Spatial Registry Query via GameState
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS)
    {
        return EBTNodeResult::Failed;
    }

    TArray<AActor*> CandidateActors;
    GS->GetActorsWithRoleTag(RoleTag, CandidateActors);

    if (CandidateActors.IsEmpty())
    {
        return EBTNodeResult::Failed;
    }

    const FVector PawnLoc = ControlledPawn->GetActorLocation();
    AActor* BestTargetActor = nullptr;
    float BestDistSq = FMath::Square(MaxSearchRadius);

    for (AActor* Candidate : CandidateActors)
    {
        if (IsValid(Candidate))
        {
            const float DistSq = FVector::DistSquared(PawnLoc, Candidate->GetActorLocation());
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestTargetActor = Candidate;
            }
        }
    }

    if (!BestTargetActor)
    {
        return EBTNodeResult::Failed;
    }

    // 4. Update Blackboard and Agent Memory
    const FVector ResolvedLocation = BestTargetActor->GetActorLocation();
    BB->SetValueAsVector(TargetLocationKey.SelectedKeyName, ResolvedLocation);

    if (TargetActorKey.SelectedKeyName != NAME_None)
    {
        BB->SetValueAsObject(TargetActorKey.SelectedKeyName, BestTargetActor);
    }

    if (SocialComp)
    {
        SocialComp->ServerRegisterKnownPlace(RoleTag, RoleTag, FGuid::NewGuid(), ResolvedLocation, 1.0f);
    }

    return EBTNodeResult::Succeeded;
}