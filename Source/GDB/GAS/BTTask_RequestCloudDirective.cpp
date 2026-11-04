// BTTask_RequestCloudDirective.cpp
#include "BTTask_RequestCloudDirective.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DynamicCloudAISubsystem.h"
#include "DynamicAttributeSet.h" 
#include "AbilitySystemComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/OverlapResult.h"

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

    // Acquire conversational turn lock while preparing/awaiting speech
    AActor* TargetActor = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(TEXT("TargetActor")));
    if (!TargetActor && TargetPlaceRoleKey.SelectedKeyName != NAME_None)
    {
        TargetActor = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(TargetPlaceRoleKey.SelectedKeyName));
    }
    SocialComp->ServerAcquireConversationalLock(TargetActor, true);

    const FString ContextJson = SocialComp->SerializeAgentContextForLLM(RequestType);

    TWeakObjectPtr<UBTTask_RequestCloudDirective> WeakThis(this);
    TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp(&OwnerComp);

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
            SocialComp->ServerReleaseConversationalLock();
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

    // --- REAL-TIME ACOUSTIC SUBTITLE STREAMING ---
    if (AAIController* AICon = BTC->GetAIOwner())
    {
        if (APawn* ControlledPawn = AICon->GetPawn())
        {
            if (UDynamicSocialComponent* SocialComp = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>())
            {
                FGameplayTag SpokenLanguage = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
                if (UDynamicLanguageComponent* LangComp = ControlledPawn->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    SpokenLanguage = LangComp->ActiveSpokenLanguageTag;
                }

                // Use normal volume settings for the real-time stream. 
                // Full contextual modulation (Scream/Whisper) resolves perfectly upon the final turn evaluation.
                FGameplayTag NormalVolTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
                float BaseRadius = 1500.0f;

                SocialComp->ServerBroadcastStreamingDialogueChunk(TokenChunk, SpokenLanguage, NormalVolTag, BaseRadius, false);
            }
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
            SocialComp->ServerReleaseConversationalLock();
        }
    }

    UBlackboardComponent* BB = BTC->GetBlackboardComponent();
    if (BB && Commands.Num() > 0)
    {
        const FCloudAICommandOutput& PrimaryCmd = Commands[0];

        // 1. Update Blackboard Variables
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

        // 2. Resolve Target Interlocutor Actor from Blackboard
        AActor* TargetActor = nullptr;
        if (TargetPlaceRoleKey.SelectedKeyName != NAME_None)
        {
            TargetActor = Cast<AActor>(BB->GetValueAsObject(TargetPlaceRoleKey.SelectedKeyName));
        }
        if (!TargetActor)
        {
            TargetActor = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
        }
        if (!TargetActor)
        {
            TargetActor = Cast<AActor>(BB->GetValueAsObject(TEXT("InterlocutorActor")));
        }

        // 3. Apply Facial Emotion Morphs
        if (PrimaryCmd.EmotionTag.IsValid() && ControlledPawn && ControlledPawn->HasAuthority())
        {
            if (UDynamicCustomizationComponent* CustComp = ControlledPawn->FindComponentByClass<UDynamicCustomizationComponent>())
            {
                CustComp->ApplyEmotion(PrimaryCmd.EmotionTag);
            }
        }

        // ============================================================================
        // FULL ACOUSTIC LANGUAGE BROADCAST, EXPOSURE & SUBTITLE DISTRIBUTION
        // ============================================================================
        if (ControlledPawn && ControlledPawn->HasAuthority() && !PrimaryCmd.UtteranceText.IsEmpty())
        {
            UDynamicLanguageComponent* LangComp = ControlledPawn->FindComponentByClass<UDynamicLanguageComponent>();
            UDynamicSocialComponent* SpeakerSocial = ControlledPawn->FindComponentByClass<UDynamicSocialComponent>();
            UWorld* World = ControlledPawn->GetWorld();

            if (LangComp && World)
            {
                // A. Sociolinguistic Code-Switching (Determines Native vs Common based on target and crowd)
                FGameplayTagContainer ContextTags;
                if (PrimaryCmd.CommandActionTag.IsValid()) ContextTags.AddTag(PrimaryCmd.CommandActionTag);
                const FGameplayTag SpokenLanguageTag = LangComp->DetermineSpokenLanguageForTarget(TargetActor, ContextTags);

                // B. Formulate Spoken Utterance (Native fluency translation or non-native broken attempt)
                const FString SpokenUtteranceText = LangComp->FormulateSpokenUtterance(SpokenLanguageTag, PrimaryCmd.UtteranceText);

                // C. Determine Acoustic Volume & Base Audible Radius (Normal speech default: 1500 units)
                FGameplayTag VolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
                float BaseRadius = 1500.0f;

                // Physiological and physical stance volume modulation
                if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(ControlledPawn))
                {
                    if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
                    {
                        static const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Health"), false);
                        if (HealthTag.IsValid())
                        {
                            const float CurHP = AttrSet->GetDynamicAttributeValue(HealthTag, 0);
                            if (CurHP > 0.0f && CurHP <= 30.0f)
                            {
                                VolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Scream"), false);
                                BaseRadius = 4500.0f;
                            }
                        }
                    }

                    static const FGameplayTag StanceCrouch = FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Crouch"), false);
                    if (CharOwner->ActiveMovementStateTag.MatchesTag(StanceCrouch))
                    {
                        VolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Whisper"), false);
                        BaseRadius = 300.0f;
                    }
                }

                // D. Record Conversational History Turn in Speaker's Memory
                if (SpeakerSocial)
                {
                    SpeakerSocial->RecordDialogueTurn(
                        SpeakerSocial->CharacterPersistentGuid,
                        SpeakerSocial->ResolveActorDisplayName(ControlledPawn),
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

                // E. Acoustic Sphere Query for Listeners (Evaluates up to maximum sensory amplification: 4.5x)
                const float QueryRadius = FMath::Clamp(BaseRadius * 4.5f, 300.0f, 15000.0f);
                TArray<FOverlapResult> Overlaps;
                FCollisionShape Sphere = FCollisionShape::MakeSphere(QueryRadius);
                FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AICloudDialogueSweep), false, ControlledPawn);

                const FVector SpeakerLoc = ControlledPawn->GetActorLocation();

                if (World->OverlapMultiByChannel(Overlaps, SpeakerLoc, FQuat::Identity, ECC_Pawn, Sphere, QueryParams))
                {
                    for (const FOverlapResult& Overlap : Overlaps)
                    {
                        AActor* Listener = Overlap.GetActor();
                        if (!Listener || Listener == ControlledPawn) continue;

                        if (UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
                        {
                            const float Distance = FVector::Dist(SpeakerLoc, Listener->GetActorLocation());

                            // Check audibility (auditory perception attributes, ultra-hearing buffs, weather noise, and O(1) room occlusion)
                            if (ListenerLang->CanHearUtteranceAtDistance(Distance, BaseRadius, VolumeTag, SpeakerLoc, ControlledPawn))
                            {
                                // 1. Automatic Linguistic Exposure Acquisition
                                ListenerLang->ServerRegisterLanguageExposure(SpokenLanguageTag, SpokenUtteranceText);

                                // 2. Deliver Decrypted Subtitle to Player Listeners via Client RPC
                                APawn* ListenerPawn = Cast<APawn>(Listener);
                                if (ListenerPawn && ListenerPawn->IsPlayerControlled())
                                {
                                    const FGameplayTag VoiceProfileTag = LangComp ? LangComp->ResolveActiveVoiceProfile() : FGameplayTag();

                                    ListenerLang->ClientReceiveHeardDialogue(
                                        ControlledPawn,
                                        SpokenUtteranceText,
                                        SpokenLanguageTag,
                                        BaseRadius,
                                        VolumeTag,
                                        PrimaryCmd.EmotionTag,
                                        VoiceProfileTag
                                    );
                                }

                                // 3. Record Conversational Turn and Topic Gossip in Listener's Memory Buffer
                                if (UDynamicSocialComponent* ListenerSocial = Listener->FindComponentByClass<UDynamicSocialComponent>())
                                {
                                    ListenerSocial->RecordDialogueTurn(
                                        SpeakerSocial ? SpeakerSocial->CharacterPersistentGuid : FGuid(),
                                        SpeakerSocial ? SpeakerSocial->CharacterDisplayName : ControlledPawn->GetName(),
                                        SpokenLanguageTag,
                                        SpokenUtteranceText
                                    );

                                    if (PrimaryCmd.CommandActionTag.IsValid())
                                    {
                                        ListenerSocial->ServerRecordOverheardTopic(
                                            PrimaryCmd.CommandActionTag,
                                            PrimaryCmd.UtteranceText,
                                            SpeakerSocial ? SpeakerSocial->CharacterPersistentGuid : FGuid(),
                                            0.85f
                                        );
                                    }

                                    // 4. Trigger Conversational Reactivity on Listening NPCs
                                    if (!ListenerPawn || !ListenerPawn->IsPlayerControlled())
                                    {
                                        ListenerSocial->HandleHeardDialogueFromSpeaker(
                                            ControlledPawn,
                                            SpokenUtteranceText,
                                            SpokenLanguageTag,
                                            VolumeTag
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============================================================================
        // EXECUTE LLM-DIRECTED GAMEPLAY ACTION (GAS ABILITIES, MOVEMENT MODES, TEACHING)
        // ============================================================================
        if (PrimaryCmd.CommandActionTag.IsValid() && ControlledPawn && ControlledPawn->HasAuthority())
        {
            // 1. Ability activation (e.g. Ability.Magic.Heal, Ability.Combat.Attack)
            if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Ability"), false)))
            {
                if (UAbilitySystemComponent* ASC = ControlledPawn->FindComponentByClass<UAbilitySystemComponent>())
                {
                    ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(PrimaryCmd.CommandActionTag));
                }
            }
            // 2. Movement/stance state change (e.g. Movement.Mode.Sprint, Movement.Mode.Flee, Movement.Mode.Crouch)
            else if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode"), false)))
            {
                if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(ControlledPawn))
                {
                    DynChar->ServerSetMovementState(PrimaryCmd.CommandActionTag);
                }
            }
            // 3. Language teaching interaction
            else if (PrimaryCmd.CommandActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Action.Interact.TeachWord"), false)))
            {
                if (UDynamicLanguageComponent* LangComp = ControlledPawn->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    LangComp->ServerTeachRandomNativeWord(TargetActor);
                }
            }
        }

        FinishLatentTask(*BTC, EBTNodeResult::Succeeded);
        return;
    }

    FinishLatentTask(*BTC, EBTNodeResult::Failed);
}