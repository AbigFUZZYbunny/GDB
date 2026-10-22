// EnvQueryTest_LandmarkRole.cpp
#include "EnvQueryTest_LandmarkRole.h"
#include "DynamicSpatialLandmark.h"
#include "DynamicPhysicsActor.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"

UEnvQueryTest_LandmarkRole::UEnvQueryTest_LandmarkRole()
{
    Cost = EEnvTestCost::Low;
    ValidItemType = UEnvQueryItemType_Actor::StaticClass();
    SetWorkOnFloatValues(false);
}

void UEnvQueryTest_LandmarkRole::RunTest(FEnvQueryInstance& QueryInstance) const
{
    const UObject* DataOwner = QueryInstance.Owner.Get();
    const AAIController* AICon = Cast<AAIController>(DataOwner);
    if (!AICon)
    {
        if (const APawn* Pawn = Cast<APawn>(DataOwner))
        {
            AICon = Cast<AAIController>(Pawn->GetController());
        }
    }

    FGameplayTag TargetTag = RoleTagToMatch;

    // Read dynamic role tag from Blackboard if configured
    if (!RoleTagBlackboardKey.IsNone() && AICon && AICon->GetBlackboardComponent())
    {
        const FName BBValue = AICon->GetBlackboardComponent()->GetValueAsName(RoleTagBlackboardKey);
        if (!BBValue.IsNone())
        {
            TargetTag = FGameplayTag::RequestGameplayTag(BBValue, false);
        }
    }

    if (!TargetTag.IsValid())
    {
        return;
    }

    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        const AActor* ItemActor = GetItemActor(QueryInstance, It.GetIndex());
        if (!ItemActor)
        {
            It.ForceItemState(EEnvItemStatus::Failed);
            continue;
        }

        bool bMatches = false;

        if (const ADynamicSpatialLandmark* Landmark = Cast<ADynamicSpatialLandmark>(ItemActor))
        {
            bMatches = Landmark->HasRoleTag(TargetTag);
        }
        else if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(ItemActor))
        {
            bMatches = PhysActor->GetActorArchetypeTag().MatchesTag(TargetTag);
        }

        It.SetScore(TestPurpose, FilterType, bMatches, true);
    }
}

FText UEnvQueryTest_LandmarkRole::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("Landmark Role Tag Match"));
}

FText UEnvQueryTest_LandmarkRole::GetDescriptionDetails() const
{
    if (!RoleTagBlackboardKey.IsNone())
    {
        return FText::FromString(FString::Printf(TEXT("Matches Blackboard Role: %s"), *RoleTagBlackboardKey.ToString()));
    }
    return FText::FromString(FString::Printf(TEXT("Matches Role Tag: %s"), *RoleTagToMatch.ToString()));
}