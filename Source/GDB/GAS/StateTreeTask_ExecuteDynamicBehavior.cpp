// StateTreeTask_ExecuteDynamicBehavior.cpp
#include "StateTreeTask_ExecuteDynamicBehavior.h"
#include "StateTreeExecutionContext.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "DynamicInteractableComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "AIController.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Navigation/PathFollowingComponent.h"

FStateTreeTask_ExecuteDynamicBehavior::FStateTreeTask_ExecuteDynamicBehavior()
{
    bShouldCallTick = true;
}

EStateTreeRunStatus FStateTreeTask_ExecuteDynamicBehavior::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character || !InstanceData.Character->HasAuthority())
    {
        return EStateTreeRunStatus::Failed;
    }

    UWorld* World = InstanceData.Character->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS)
    {
        return EStateTreeRunStatus::Failed;
    }

    InstanceData.ElapsedExecutionTime = 0.0f;
    InstanceData.TargetExecutionDuration = 1.0f;
    InstanceData.bWaitingForEQS = false;
    InstanceData.bHasDispatchedExecution = false;
    InstanceData.ActiveEQSBucket = nullptr;
    InstanceData.ActiveMontage = nullptr;
    InstanceData.ActiveBehaviorDef = FDynamicAIBehaviorDef();

    // 1. Resolve Best Behavior Definition
    if (InstanceData.BehaviorTag.IsValid())
    {
        if (const FDynamicAIBehaviorDef* FoundDef = GS->AIBehaviorMasterRegistry.Find(InstanceData.BehaviorTag))
        {
            InstanceData.ActiveBehaviorDef = *FoundDef;
        }
        else
        {
            return EStateTreeRunStatus::Failed;
        }
    }
    else
    {
        float BestScore = -MAX_flt;
        if (!GS->EvaluateBestDynamicBehavior(InstanceData.Character, InstanceData.ActiveBehaviorDef, BestScore))
        {
            return EStateTreeRunStatus::Failed;
        }
    }

    // 2. Set Execution Duration from Metric
    static const FGameplayTag DurationTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.Duration"), false);
    if (DurationTag.IsValid())
    {
        if (const float* CustomDuration = InstanceData.ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.Find(DurationTag))
        {
            InstanceData.TargetExecutionDuration = FMath::Max(0.1f, *CustomDuration);
        }
    }

    // 3. Resolve Target Location/Actor via Dynamic EQS or Spatial Fallback
    if (!InstanceData.ActiveBehaviorDef.TargetEQSConfig.QueryAsset.IsNull())
    {
        ExecuteEQSQuery(Context, InstanceData.ActiveBehaviorDef, InstanceData);
        return EStateTreeRunStatus::Running;
    }

    ResolveSpatialFallback(InstanceData.Character, InstanceData.ActiveBehaviorDef, InstanceData.EvaluatedLocation, InstanceData.EvaluatedTarget);
    DispatchBehaviorAction(Context, InstanceData);

    return EStateTreeRunStatus::Running;
}

void FStateTreeTask_ExecuteDynamicBehavior::ExecuteEQSQuery(FStateTreeExecutionContext& Context, const FDynamicAIBehaviorDef& BehaviorDef, FInstanceDataType& InstanceData) const
{
    UEnvQuery* Query = BehaviorDef.TargetEQSConfig.QueryAsset.LoadSynchronous();
    if (!Query || !InstanceData.Character)
    {
        ResolveSpatialFallback(InstanceData.Character, BehaviorDef, InstanceData.EvaluatedLocation, InstanceData.EvaluatedTarget);
        DispatchBehaviorAction(Context, InstanceData);
        return;
    }

    FEnvQueryRequest QueryRequest(Query, InstanceData.Character);
    for (const FDynamicEQSParam& Param : BehaviorDef.TargetEQSConfig.QueryParams)
    {
        QueryRequest.SetFloatParam(Param.ParamName, Param.Value);
    }

    TSharedPtr<FEQSPendingResultBucket> Bucket = MakeShared<FEQSPendingResultBucket>();
    InstanceData.ActiveEQSBucket = Bucket;
    InstanceData.bWaitingForEQS = true;

    QueryRequest.Execute(EEnvQueryRunMode::SingleResult, FQueryFinishedSignature::CreateLambda([Bucket](TSharedPtr<FEnvQueryResult> Result)
    {
        if (Bucket.IsValid())
        {
            Bucket->bCompleted = true;
            if (Result.IsValid() && Result->IsSuccessful())
            {
                Bucket->bSuccess = true;
                Bucket->TargetActor = Result->GetItemAsActor(0);
                Bucket->TargetLocation = Result->GetItemAsLocation(0);
            }
        }
    }));
}

bool FStateTreeTask_ExecuteDynamicBehavior::ResolveSpatialFallback(ADynamicCharacter* Character, const FDynamicAIBehaviorDef& BehaviorDef, FVector& OutLocation, AActor*& OutActor) const
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

    return !OutLocation.IsZero() || OutActor != nullptr;
}

void FStateTreeTask_ExecuteDynamicBehavior::DispatchBehaviorAction(FStateTreeExecutionContext& Context, FInstanceDataType& InstanceData) const
{
    if (InstanceData.bHasDispatchedExecution || !InstanceData.Character) return;
    InstanceData.bHasDispatchedExecution = true;

    ADynamicCharacter* DynChar = InstanceData.Character;
    AAIController* AICon = Cast<AAIController>(DynChar->GetController());
    UWorld* World = DynChar->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    // 1. Spoken Utterance Resolution
    if (UDynamicSocialComponent* SocialComp = DynChar->FindComponentByClass<UDynamicSocialComponent>())
    {
        FString ResolvedText;
        TSoftObjectPtr<UAnimMontage> DialogueMontage;
        FGameplayTag VoiceCueTag;

        TMap<FString, FString> WildcardMap;
        static const FGameplayTag DurationMetricTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.Duration"), false);
        if (const float* DurVal = InstanceData.ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.Find(DurationMetricTag))
        {
            WildcardMap.Add(TEXT("Amt"), FString::FromInt(FMath::RoundToInt(*DurVal)));
        }

        SocialComp->TryResolveDialogueForBehavior(InstanceData.ActiveBehaviorDef, InstanceData.EvaluatedTarget, ResolvedText, DialogueMontage, VoiceCueTag, WildcardMap);
    }

    // 2. Cooldown Registration
    if (GS)
    {
        GS->RecordBehaviorExecutionCooldown(DynChar, InstanceData.ActiveBehaviorDef.BehaviorTag, InstanceData.ActiveBehaviorDef.CooldownDuration);
    }

    // 3. Execution by Type
    switch (InstanceData.ActiveBehaviorDef.ExecutionType)
    {
        case ENpcBehaviorExecutionType::Ability:
        {
            if (UAbilitySystemComponent* ASC = DynChar->GetAbilitySystemComponent())
            {
                if (InstanceData.ActiveBehaviorDef.AbilityTagToTrigger.IsValid())
                {
                    FGameplayTagContainer AbilityTags(InstanceData.ActiveBehaviorDef.AbilityTagToTrigger);
                    ASC->TryActivateAbilitiesByTag(AbilityTags);
                }
            }
            break;
        }

        case ENpcBehaviorExecutionType::Interaction:
        {
            if (InstanceData.EvaluatedTarget)
            {
                if (UDynamicInteractableComponent* Interactable = InstanceData.EvaluatedTarget->FindComponentByClass<UDynamicInteractableComponent>())
                {
                    FDynamicInteractionProfile Profile;
                    Profile.InteractionActionTag = InstanceData.ActiveBehaviorDef.ActionTag;
                    Interactable->ServerExecuteInteraction(DynChar, Profile, 1.0f);
                }
            }
            break;
        }

        case ENpcBehaviorExecutionType::Movement:
        {
            if (InstanceData.ActiveBehaviorDef.MovementStateTagToSet.IsValid())
            {
                DynChar->ServerSetMovementState(InstanceData.ActiveBehaviorDef.MovementStateTagToSet);
            }

            if (AICon)
            {
                static const FGameplayTag RadiusTag = FGameplayTag::RequestGameplayTag(TEXT("Metric.AcceptanceRadius"), false);
                const float AcceptanceRadius = RadiusTag.IsValid() ? InstanceData.ActiveBehaviorDef.ExecutionMetrics.LocalLookupMap.FindRef(RadiusTag) : 100.0f;
                const float ClampedRadius = FMath::Max(50.0f, AcceptanceRadius);

                if (InstanceData.EvaluatedTarget)
                {
                    AICon->MoveToActor(InstanceData.EvaluatedTarget, ClampedRadius);
                }
                else if (!InstanceData.EvaluatedLocation.IsZero())
                {
                    AICon->MoveToLocation(InstanceData.EvaluatedLocation, ClampedRadius);
                }
            }
            break;
        }

        case ENpcBehaviorExecutionType::Montage:
        {
            if (InstanceData.ActiveBehaviorDef.MontageToPlay.IsValid())
            {
                FSoftObjectPath MontagePath = InstanceData.ActiveBehaviorDef.MontageToPlay.ToSoftObjectPath();
                TWeakObjectPtr<ADynamicCharacter> WeakChar(DynChar);

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
            break;
        }

        case ENpcBehaviorExecutionType::Wait:
        default:
            break;
    }
}

EStateTreeRunStatus FStateTreeTask_ExecuteDynamicBehavior::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character) return EStateTreeRunStatus::Failed;

    // Check EQS Async Completion
    if (InstanceData.bWaitingForEQS)
    {
        if (InstanceData.ActiveEQSBucket.IsValid() && InstanceData.ActiveEQSBucket->bCompleted)
        {
            InstanceData.bWaitingForEQS = false;
            if (InstanceData.ActiveEQSBucket->bSuccess)
            {
                InstanceData.EvaluatedTarget = InstanceData.ActiveEQSBucket->TargetActor.Get();
                InstanceData.EvaluatedLocation = InstanceData.ActiveEQSBucket->TargetLocation;
            }
            else
            {
                ResolveSpatialFallback(InstanceData.Character, InstanceData.ActiveBehaviorDef, InstanceData.EvaluatedLocation, InstanceData.EvaluatedTarget);
            }

            DispatchBehaviorAction(Context, InstanceData);
        }
        return EStateTreeRunStatus::Running;
    }

    InstanceData.ElapsedExecutionTime += DeltaTime;

    // Movement-Specific Completion Check
    if (InstanceData.ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Movement)
    {
        if (AAIController* AICon = Cast<AAIController>(InstanceData.Character->GetController()))
        {
            if (AICon->GetMoveStatus() == EPathFollowingStatus::Idle)
            {
                return EStateTreeRunStatus::Succeeded;
            }
        }
    }

    if (InstanceData.ElapsedExecutionTime >= InstanceData.TargetExecutionDuration)
    {
        return EStateTreeRunStatus::Succeeded;
    }

    return EStateTreeRunStatus::Running;
}

void FStateTreeTask_ExecuteDynamicBehavior::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character) return;

    // Clean up abilities and animations if transitioning prematurely
    if (InstanceData.ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Ability && InstanceData.ActiveBehaviorDef.AbilityTagToTrigger.IsValid())
    {
        if (UAbilitySystemComponent* ASC = InstanceData.Character->GetAbilitySystemComponent())
        {
            FGameplayTagContainer TagsToCancel(InstanceData.ActiveBehaviorDef.AbilityTagToTrigger);
            ASC->CancelAbilities(&TagsToCancel);
        }
    }
    else if (InstanceData.ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Montage)
    {
        if (USkeletalMeshComponent* MeshComp = InstanceData.Character->GetMeshComponent())
        {
            if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
            {
                AnimInst->Montage_Stop(0.2f);
            }
        }
    }
    else if (InstanceData.ActiveBehaviorDef.ExecutionType == ENpcBehaviorExecutionType::Movement)
    {
        if (AAIController* AICon = Cast<AAIController>(InstanceData.Character->GetController()))
        {
            AICon->StopMovement();
        }
    }
}