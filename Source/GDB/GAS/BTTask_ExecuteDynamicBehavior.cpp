// BTTask_ExecuteDynamicBehavior.cpp
#include "BTTask_ExecuteDynamicBehavior.h"
#include "AIController.h"
#include "Engine/AssetManager.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "DynamicInteractableComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

UBTTask_ExecuteDynamicBehavior::UBTTask_ExecuteDynamicBehavior()
{
    NodeName = "Execute Dynamic AI Behavior";
    bNotifyTick = true;
    bCreateNodeInstance = true;

    BehaviorTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteDynamicBehavior, BehaviorTagKey));
    TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteDynamicBehavior, TargetActorKey), AActor::StaticClass());
    TargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteDynamicBehavior, TargetLocationKey));
    UtteranceTextKey.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteDynamicBehavior, UtteranceTextKey));
}

EBTNodeResult::Type UBTTask_ExecuteDynamicBehavior::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    ElapsedExecutionTime = 0.0f;
    TargetExecutionDuration = 1.0f;
    ActiveBehaviorDef = FDynamicAIBehaviorDef();

    AAIController* AICon = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AICon ? AICon->GetPawn() : nullptr;
    ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(ControlledPawn);
    if (!DynChar || !DynChar->HasAuthority()) return EBTNodeResult::Failed;

    CachedCharacter = DynChar;
    UWorld* World = DynChar->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return EBTNodeResult::Failed;

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    FGameplayTag SelectedBehaviorTag = ExplicitBehaviorTag;

    if (!SelectedBehaviorTag.IsValid() && BB && BehaviorTagKey.SelectedKeyName != NAME_None)
    {
        const FName TagName = BB->GetValueAsName(BehaviorTagKey.SelectedKeyName);
        if (!TagName.IsNone())
        {
            SelectedBehaviorTag = FGameplayTag::RequestGameplayTag(TagName, false);
        }
    }

    if (bAutoSelectHighestPriorityBehavior || !SelectedBehaviorTag.IsValid())
    {
        float BestScore = -MAX_flt;
        if (!GS->EvaluateBestDynamicBehavior(DynChar, ActiveBehaviorDef, BestScore))
        {
            return EBTNodeResult::Failed;
        }
    }
    else
    {
        if (const FDynamicAIBehaviorDef* FoundDef = GS->AIBehaviorMasterRegistry.Find(SelectedBehaviorTag))
        {
            ActiveBehaviorDef = *FoundDef;
        }
        else
        {
            return EBTNodeResult::Failed;
        }
    }

    FVector TargetLocation = FVector::ZeroVector;
    AActor* TargetActor = nullptr;
    ResolveSpatialTarget(DynChar, ActiveBehaviorDef, OwnerComp, TargetLocation, TargetActor);

    if (BB)
    {
        if (BehaviorTagKey.SelectedKeyName != NAME_None)
        {
            BB->SetValueAsName(BehaviorTagKey.SelectedKeyName, ActiveBehaviorDef.BehaviorTag.GetTagName());
        }

        if (UtteranceTextKey.SelectedKeyName != NAME_None)
        {
            FString ResolvedText = TEXT("");
            TSoftObjectPtr<UAnimMontage> DialogueMontage;
            FGameplayTag VoiceCueTag;

            if (UDynamicSocialComponent* SocialComp = DynChar->FindComponentByClass<UDynamicSocialComponent>())
            {
                TMap<FString, FString> WildcardMap;
                static const FGameplayTag DurationTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.Duration"), false);
                if (const float* DurVal = ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.Find(DurationTag))
                {
                    WildcardMap.Add(TEXT("Amt"), FString::FromInt(FMath::RoundToInt(*DurVal)));
                }

                // Resolves dialogue across behavior utterances, linked FastPath categories, and target condition observation
                SocialComp->TryResolveDialogueForBehavior(ActiveBehaviorDef, TargetActor, ResolvedText, DialogueMontage, VoiceCueTag, WildcardMap);
            }

            if (!ResolvedText.IsEmpty())
            {
                BB->SetValueAsString(UtteranceTextKey.SelectedKeyName, ResolvedText);
            }
        }
    }

    GS->RecordBehaviorExecutionCooldown(DynChar, ActiveBehaviorDef.BehaviorTag, ActiveBehaviorDef.CooldownDuration);

    static const FGameplayTag DurationMetricTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.Duration"), false);
    if (DurationMetricTag.IsValid())
    {
        if (const float* CustomDuration = ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.Find(DurationMetricTag))
        {
            TargetExecutionDuration = FMath::Max(0.1f, *CustomDuration);
        }
    }

    switch (ActiveBehaviorDef.ExecutionType)
    {
        case ENpcBehaviorExecutionType::Ability:
        {
            if (UAbilitySystemComponent* ASC = DynChar->GetAbilitySystemComponent())
            {
                if (ActiveBehaviorDef.AbilityTagToTrigger.IsValid())
                {
                    FGameplayTagContainer AbilityTags(ActiveBehaviorDef.AbilityTagToTrigger);
                    ASC->TryActivateAbilitiesByTag(AbilityTags);
                }
            }
            return EBTNodeResult::Succeeded;
        }
        case ENpcBehaviorExecutionType::Interaction:
        {
            if (TargetActor)
            {
                if (UDynamicInteractableComponent* Interactable = TargetActor->FindComponentByClass<UDynamicInteractableComponent>())
                {
                    FDynamicInteractionProfile Profile;
                    Profile.InteractionActionTag = ActiveBehaviorDef.ActionTag;
                    Interactable->ServerExecuteInteraction(DynChar, Profile, 1.0f);
                }
            }
            return EBTNodeResult::Succeeded;
        }
        case ENpcBehaviorExecutionType::Movement:
        {
            if (ActiveBehaviorDef.MovementStateTagToSet.IsValid())
            {
                DynChar->ServerSetMovementState(ActiveBehaviorDef.MovementStateTagToSet);
            }
            if (AICon && !TargetLocation.IsZero())
            {
                static const FGameplayTag RadiusTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.AcceptanceRadius"), false);
                const float AcceptanceRadius = RadiusTag.IsValid() ? ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.FindRef(RadiusTag) : 100.0f;
                AICon->MoveToLocation(TargetLocation, FMath::Max(50.0f, AcceptanceRadius));
            }
            return EBTNodeResult::Succeeded;
        }
    case ENpcBehaviorExecutionType::Montage:
        {
            if (ActiveBehaviorDef.MontageToPlay.IsValid())
            {
                FSoftObjectPath MontagePath = ActiveBehaviorDef.MontageToPlay.ToSoftObjectPath();
                TWeakObjectPtr<UBTTask_ExecuteDynamicBehavior> WeakThis(this);
                
                UAssetManager::GetStreamableManager().RequestAsyncLoad(MontagePath, FStreamableDelegate::CreateLambda([WeakThis, MontagePath]() {
                    if (UBTTask_ExecuteDynamicBehavior* StrongThis = WeakThis.Get())
                    {
                        if (UAnimMontage* Montage = Cast<UAnimMontage>(MontagePath.ResolveObject()))
                        {
                            if (StrongThis->CachedCharacter.IsValid())
                            {
                                if (USkeletalMeshComponent* MeshComp = StrongThis->CachedCharacter->GetMeshComponent())
                                {
                                    if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                                    {
                                        AnimInst->Montage_Play(Montage, 1.0f);
                                        StrongThis->TargetExecutionDuration = Montage->GetPlayLength();
                                    }
                                }
                            }
                        }
                    }
                }));
                return EBTNodeResult::InProgress;
            }
            return EBTNodeResult::Succeeded;
        }
        case ENpcBehaviorExecutionType::Wait:
        default:
            return EBTNodeResult::InProgress;
    }
}

void UBTTask_ExecuteDynamicBehavior::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);
    ElapsedExecutionTime += DeltaSeconds;

    if (ElapsedExecutionTime >= TargetExecutionDuration)
    {
        FinishBehaviorExecution(OwnerComp, true);
    }
}

// BTTask_ExecuteDynamicBehavior.cpp (AbortTask implementation)
EBTNodeResult::Type UBTTask_ExecuteDynamicBehavior::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (CachedCharacter.IsValid())
    {
        // FIX: Ensure GAS abilities activated by the behavior tree are properly canceled on BT aborts
        if (ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Ability && ActiveBehaviorDef.AbilityTagToTrigger.IsValid())
        {
            if (UAbilitySystemComponent* ASC = CachedCharacter->GetAbilitySystemComponent())
            {
                FGameplayTagContainer TagsToCancel(ActiveBehaviorDef.AbilityTagToTrigger);
                ASC->CancelAbilities(&TagsToCancel);
            }
        }
        else if (ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Montage && ActiveBehaviorDef.MontageToPlay.IsValid())
        {
            if (USkeletalMeshComponent* MeshComp = CachedCharacter->GetMeshComponent())
            {
                if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                {
                    AnimInst->Montage_Stop(0.2f);
                }
            }
        }
    }
    return EBTNodeResult::Aborted;
}

bool UBTTask_ExecuteDynamicBehavior::ResolveSpatialTarget(ADynamicCharacter* Character, const FDynamicAIBehaviorDef& BehaviorDef, UBehaviorTreeComponent& OwnerComp, FVector& OutLocation, AActor*& OutActor)
{
    OutLocation = FVector::ZeroVector;
    OutActor = nullptr;

    if (!Character || !BehaviorDef.TargetPlaceRoleTag.IsValid()) return false;

    if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
    {
        FDynamicKnownPlaceItem KnownPlace;
        if (SocialComp->GetKnownPlaceByRole(BehaviorDef.TargetPlaceRoleTag, KnownPlace) && KnownPlace.ConfidenceScore > 0.05f)
        {
            OutLocation = FVector(KnownPlace.LastKnownLocation);
        }
    }

    if (OutLocation.IsZero())
    {
        if (ADynamicGameState* GS = Character->GetWorld() ? Character->GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            TArray<AActor*> CandidateActors;
            GS->GetActorsWithRoleTag(BehaviorDef.TargetPlaceRoleTag, CandidateActors);

            float BestDistSq = MAX_flt;
            const FVector CharLoc = Character->GetActorLocation();

            for (AActor* Candidate : CandidateActors)
            {
                if (IsValid(Candidate))
                {
                    const float DistSq = FVector::DistSquared(CharLoc, Candidate->GetActorLocation());
                    if (DistSq < BestDistSq)
                    {
                        BestDistSq = DistSq;
                        OutActor = Candidate;
                        OutLocation = Candidate->GetActorLocation();
                    }
                }
            }
        }
    }

    if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
    {
        if (TargetLocationKey.SelectedKeyName != NAME_None && !OutLocation.IsZero())
        {
            BB->SetValueAsVector(TargetLocationKey.SelectedKeyName, OutLocation);
        }
        if (TargetActorKey.SelectedKeyName != NAME_None && OutActor)
        {
            BB->SetValueAsObject(TargetActorKey.SelectedKeyName, OutActor);
        }
    }

    return !OutLocation.IsZero() || OutActor != nullptr;
}

void UBTTask_ExecuteDynamicBehavior::FinishBehaviorExecution(UBehaviorTreeComponent& OwnerComp, bool bSuccess)
{
    FinishLatentTask(OwnerComp, bSuccess ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
}