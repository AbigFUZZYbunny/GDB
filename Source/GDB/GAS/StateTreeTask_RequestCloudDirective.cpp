// StateTreeTask_RequestCloudDirective.cpp
#include "StateTreeTask_RequestCloudDirective.h"
#include "StateTreeExecutionContext.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "DynamicLanguageComponent.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicCloudAISubsystem.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"

FStateTreeTask_RequestCloudDirective::FStateTreeTask_RequestCloudDirective()
{
    bShouldCallTick = true;
}

EStateTreeRunStatus FStateTreeTask_RequestCloudDirective::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character || !InstanceData.Character->HasAuthority())
    {
        return EStateTreeRunStatus::Failed;
    }

    ADynamicCharacter* Character = InstanceData.Character;
    UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>();
    UDynamicCloudAISubsystem* CloudSubsystem = Character->GetWorld() ? Character->GetWorld()->GetSubsystem<UDynamicCloudAISubsystem>() : nullptr;
    if (!SocialComp || !CloudSubsystem) return EStateTreeRunStatus::Failed;

    InstanceData.ElapsedTaskTime = 0.0f;
    InstanceData.DynamicTimeoutLimit = InstanceData.BaseRequestTimeoutSeconds;
    InstanceData.bVampingStarted = false;
    InstanceData.bUtteranceStarted = false;
    InstanceData.LiveUtteranceBuffer.Empty();
    InstanceData.ActiveVampingMontage.Reset();

    SocialComp->ServerSetCloudAISuspension(true);

    // Acquire conversational turn lock while pondering
    SocialComp->ServerAcquireConversationalLock(InstanceData.TargetActor, true);

    const FString ContextJson = SocialComp->SerializeAgentContextForLLM(InstanceData.RequestType);

    TSharedPtr<FCloudAIStateTreeResultBucket> Bucket = MakeShared<FCloudAIStateTreeResultBucket>();
    InstanceData.ResultBucket = Bucket;

    FOnCloudAITokenStream StreamCallback;
    StreamCallback.BindLambda([Bucket](const FString& Chunk)
    {
        if (Bucket.IsValid())
        {
            Bucket->StreamingTokenQueue.Enqueue(Chunk);
        }
    });

    FOnCloudAIResponseDirectCallback DirectCallback;
    DirectCallback.BindLambda([Bucket](const TArray<FCloudAICommandOutput>& Commands)
    {
        if (Bucket.IsValid())
        {
            Bucket->Commands = Commands;
            Bucket->bCompleted = true;
        }
    });

    FOnCloudAITaskAssigned AssignedCallback;
    AssignedCallback.BindLambda([Bucket](float EstimatedLatency)
    {
        if (Bucket.IsValid())
        {
            Bucket->EstimatedLatency = EstimatedLatency;
            Bucket->bAssigned = true;
        }
    });

    InstanceData.ActiveRequestGuid = CloudSubsystem->DispatchDistributedAIRequest(
        Character,
        InstanceData.RequestType,
        InstanceData.TaskPriority,
        ContextJson,
        InstanceData.BaseRequestTimeoutSeconds,
        StreamCallback,
        DirectCallback,
        AssignedCallback
    );

    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeTask_RequestCloudDirective::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character || !InstanceData.ResultBucket.IsValid())
    {
        return EStateTreeRunStatus::Failed;
    }

    InstanceData.ElapsedTaskTime += DeltaTime;
    ADynamicCharacter* Character = InstanceData.Character;
    TSharedPtr<FCloudAIStateTreeResultBucket> Bucket = InstanceData.ResultBucket;

    // 1. Process Task Assignment latency adjustment
    if (Bucket->bAssigned)
    {
        InstanceData.DynamicTimeoutLimit = InstanceData.BaseRequestTimeoutSeconds + Bucket->EstimatedLatency;
        Bucket->bAssigned = false;
    }

    // 2. Stream real-time dialogue tokens to listeners
    FString StreamedChunk;
    while (Bucket->StreamingTokenQueue.Dequeue(StreamedChunk))
    {
        if (!InstanceData.bUtteranceStarted)
        {
            InstanceData.bUtteranceStarted = true;
            StopVampingBehavior(Character, InstanceData);
        }

        InstanceData.LiveUtteranceBuffer += StreamedChunk;

        if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
        {
            FGameplayTag SpokenLanguage = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
            if (UDynamicLanguageComponent* LangComp = Character->FindComponentByClass<UDynamicLanguageComponent>())
            {
                SpokenLanguage = LangComp->ActiveSpokenLanguageTag;
            }

            FGameplayTag NormalVolTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
            SocialComp->ServerBroadcastStreamingDialogueChunk(StreamedChunk, SpokenLanguage, NormalVolTag, 1500.0f, false);
        }
    }

    // 3. Check for Successful Cloud Response Completion
    if (Bucket->bCompleted)
    {
        StopVampingBehavior(Character, InstanceData);
        ProcessCompletedDirectives(Character, InstanceData, Bucket->Commands);
        return EStateTreeRunStatus::Succeeded;
    }

    // 4. Timeout Guard: Drop to fallback branch on LLM Timeout
    if (InstanceData.ElapsedTaskTime >= InstanceData.DynamicTimeoutLimit)
    {
        StopVampingBehavior(Character, InstanceData);

        if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerSetCloudAISuspension(false);
            SocialComp->ServerReleaseConversationalLock();
        }

        // Returning Failed allows the State Tree transition engine to immediately execute OnStateFailed or fallback
        return EStateTreeRunStatus::Failed;
    }

    // 5. Latency Masking (Vamping Chatter & Thinking Montages)
    if (InstanceData.VampingConfig.bEnableVamping && !InstanceData.bVampingStarted && !InstanceData.bUtteranceStarted &&
        InstanceData.ElapsedTaskTime >= InstanceData.VampingConfig.VampingStartDelay)
    {
        StartVampingBehavior(Character, InstanceData);
        InstanceData.bVampingStarted = true;
    }

    return EStateTreeRunStatus::Running;
}

void FStateTreeTask_RequestCloudDirective::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character) return;

    ADynamicCharacter* Character = InstanceData.Character;

    // Cancel active cloud task if departing state prematurely
    if (InstanceData.ActiveRequestGuid.IsValid())
    {
        if (UWorld* World = Character->GetWorld())
        {
            if (UDynamicCloudAISubsystem* CloudSubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
            {
                CloudSubsystem->CancelRemoteAIRequest(InstanceData.ActiveRequestGuid);
            }
        }
        InstanceData.ActiveRequestGuid.Invalidate();
    }

    StopVampingBehavior(Character, InstanceData);

    if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
    {
        SocialComp->ServerSetCloudAISuspension(false);
        SocialComp->ServerReleaseConversationalLock();
    }

    InstanceData.ResultBucket.Reset();
}

void FStateTreeTask_RequestCloudDirective::StartVampingBehavior(ADynamicCharacter* Character, FInstanceDataType& InstanceData) const
{
    if (InstanceData.VampingConfig.ThinkingMontages.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, InstanceData.VampingConfig.ThinkingMontages.Num() - 1);
        if (UAnimMontage* Montage = InstanceData.VampingConfig.ThinkingMontages[RandIdx].LoadSynchronous())
        {
            if (USkeletalMeshComponent* MeshComp = Character->GetMeshComponent())
            {
                if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                {
                    AnimInst->Montage_Play(Montage, 1.0f);
                    InstanceData.ActiveVampingMontage = Montage;
                }
            }
        }
    }

    if (InstanceData.VampingConfig.FillerUtterances.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, InstanceData.VampingConfig.FillerUtterances.Num() - 1);
        const FString FillerText = InstanceData.VampingConfig.FillerUtterances[RandIdx];

        if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
        {
            FGameplayTag SpokenLanguage = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
            if (UDynamicLanguageComponent* LangComp = Character->FindComponentByClass<UDynamicLanguageComponent>())
            {
                SpokenLanguage = LangComp->ActiveSpokenLanguageTag;
            }
            FGameplayTag NormalVolTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
            SocialComp->ServerBroadcastStreamingDialogueChunk(FillerText, SpokenLanguage, NormalVolTag, 1500.0f, true);
        }
    }
}

void FStateTreeTask_RequestCloudDirective::StopVampingBehavior(ADynamicCharacter* Character, FInstanceDataType& InstanceData) const
{
    if (InstanceData.ActiveVampingMontage.IsValid())
    {
        if (USkeletalMeshComponent* MeshComp = Character->GetMeshComponent())
        {
            if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
            {
                AnimInst->Montage_Stop(0.2f, InstanceData.ActiveVampingMontage.Get());
            }
        }
        InstanceData.ActiveVampingMontage.Reset();
    }
}

void FStateTreeTask_RequestCloudDirective::ProcessCompletedDirectives(ADynamicCharacter* Character, FInstanceDataType& InstanceData, const TArray<FCloudAICommandOutput>& Commands) const
{
    if (Commands.Num() == 0 || !Character) return;

    const FCloudAICommandOutput& PrimaryCmd = Commands[0];
    UWorld* World = Character->GetWorld();
    UDynamicLanguageComponent* LangComp = Character->FindComponentByClass<UDynamicLanguageComponent>();
    UDynamicSocialComponent* SpeakerSocial = Character->FindComponentByClass<UDynamicSocialComponent>();

    // 1. Emotion Application
    if (PrimaryCmd.EmotionTag.IsValid())
    {
        if (UDynamicCustomizationComponent* CustComp = Character->FindComponentByClass<UDynamicCustomizationComponent>())
        {
            CustComp->ApplyEmotion(PrimaryCmd.EmotionTag);
        }
    }

    // 2. Full Acoustic Dialogue Broadcast
    if (!PrimaryCmd.UtteranceText.IsEmpty() && LangComp && World)
    {
        FGameplayTagContainer ContextTags;
        if (PrimaryCmd.CommandActionTag.IsValid()) ContextTags.AddTag(PrimaryCmd.CommandActionTag);
        const FGameplayTag SpokenLanguageTag = LangComp->DetermineSpokenLanguageForTarget(InstanceData.TargetActor, ContextTags);
        const FString SpokenUtteranceText = LangComp->FormulateSpokenUtterance(SpokenLanguageTag, PrimaryCmd.UtteranceText);

        FGameplayTag VolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
        float BaseRadius = 1500.0f;

        if (SpeakerSocial)
        {
            SpeakerSocial->RecordDialogueTurn(
                SpeakerSocial->CharacterPersistentGuid,
                SpeakerSocial->ResolveActorDisplayName(Character),
                SpokenLanguageTag,
                SpokenUtteranceText
            );

            if (PrimaryCmd.CommandActionTag.IsValid())
            {
                SpeakerSocial->ServerRecordOverheardTopic(
                    PrimaryCmd.CommandActionTag,
                    PrimaryCmd.UtteranceText,
                    SpeakerSocial->CharacterPersistentGuid,
                    1.0f
                );
            }
        }

        const float QueryRadius = FMath::Clamp(BaseRadius * 4.5f, 300.0f, 15000.0f);
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(QueryRadius);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StateTreeDialogueSweep), false, Character);

        const FVector SpeakerLoc = Character->GetActorLocation();

        if (World->OverlapMultiByChannel(Overlaps, SpeakerLoc, FQuat::Identity, ECC_Pawn, Sphere, QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                AActor* Listener = Overlap.GetActor();
                if (!Listener || Listener == Character) continue;

                if (UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    const float Distance = FVector::Dist(SpeakerLoc, Listener->GetActorLocation());
                    if (ListenerLang->CanHearUtteranceAtDistance(Distance, BaseRadius, VolumeTag, SpeakerLoc, Character))
                    {
                        ListenerLang->ServerRegisterLanguageExposure(SpokenLanguageTag, SpokenUtteranceText);

                        APawn* ListenerPawn = Cast<APawn>(Listener);
                        if (ListenerPawn && ListenerPawn->IsPlayerControlled())
                        {
                            const FGameplayTag VoiceProfileTag = LangComp->ResolveActiveVoiceProfile();
                            ListenerLang->ClientReceiveHeardDialogue(
                                Character,
                                SpokenUtteranceText,
                                SpokenLanguageTag,
                                BaseRadius,
                                VolumeTag,
                                PrimaryCmd.EmotionTag,
                                VoiceProfileTag
                            );
                        }

                        if (UDynamicSocialComponent* ListenerSocial = Listener->FindComponentByClass<UDynamicSocialComponent>())
                        {
                            ListenerSocial->RecordDialogueTurn(
                                SpeakerSocial ? SpeakerSocial->CharacterPersistentGuid : FGuid(),
                                SpeakerSocial ? SpeakerSocial->CharacterDisplayName : Character->GetName(),
                                SpokenLanguageTag,
                                SpokenUtteranceText
                            );

                            if (!ListenerPawn || !ListenerPawn->IsPlayerControlled())
                            {
                                ListenerSocial->HandleHeardDialogueFromSpeaker(Character, SpokenUtteranceText, SpokenLanguageTag, VolumeTag);
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Action Execution (GAS Ability, Mover Mode, Interactions)
    if (PrimaryCmd.CommandActionTag.IsValid())
    {
        if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Ability"), false)))
        {
            if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
            {
                ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(PrimaryCmd.CommandActionTag));
            }
        }
        else if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode"), false)))
        {
            Character->ServerSetMovementState(PrimaryCmd.CommandActionTag);
        }
        else if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Action.Interact.TeachWord"), false)))
        {
            if (LangComp)
            {
                LangComp->ServerTeachRandomNativeWord(InstanceData.TargetActor);
            }
        }
    }
}