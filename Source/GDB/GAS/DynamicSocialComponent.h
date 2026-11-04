// DynamicSocialComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialComponent.generated.h"

class ADynamicCharacter;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnFastPathDialogueSpokenSignature, AActor*, Speaker, AActor*, Listener, const FString&, SpokenText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnConversationalLockStateChangedSignature, EConversationalTurnState, NewState, AActor*, Interlocutor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicSocialComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicSocialComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_CharacterPersistentGuid, Category = "Dynamic GAS|Social")
    FGuid CharacterPersistentGuid;

    UFUNCTION()
    void OnRep_CharacterPersistentGuid(const FGuid& OldGuid);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void SetCharacterPersistentGuid(const FGuid& NewGuid);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Social")
    FString CharacterDisplayName = TEXT("");

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|Social")
    FGuid CurrentPropertyID;

    // Conversational Lock & Arbitration
    UPROPERTY(ReplicatedUsing = OnRep_ActiveTurnState, BlueprintReadOnly, Category = "Dynamic GAS|Social|Conversation")
    EConversationalTurnState ActiveTurnState = EConversationalTurnState::Idle;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Social|Conversation")
    TWeakObjectPtr<AActor> ActiveInterlocutor = nullptr;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Social|Conversation")
    FOnConversationalLockStateChangedSignature OnConversationalLockStateChanged;

    UFUNCTION()
    void OnRep_ActiveTurnState();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Conversation")
    bool ServerAcquireConversationalLock(AActor* InterlocutorActor, bool bPondering = false);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Conversation")
    void ServerReleaseConversationalLock();

    // Memory & Dialogue Buffers
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social|Conversation")
    float ConversationalMemoryTimeoutSeconds = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social|Conversation")
    int32 MaxDialogueHistoryTurns = 5;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|Social|Conversation")
    TArray<FDynamicDialogueTurn> RecentDialogueHistory;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Conversation")
    void RecordDialogueTurn(const FGuid& SpeakerGuid, const FString& SpeakerName, FGameplayTag SpokenLanguageTag, const FString& UtteranceText);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Conversation")
    void RecordNonVerbalAction(const FGuid& ActorGuid, const FString& ActorName, const FString& ActionDescription);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Conversation")
    void CleanExpiredDialogueTurns();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Conversation")
    void TriggerMemoryConsolidation(const FGuid& TargetEntityGuid, const TArray<FDynamicDialogueTurn>& TurnsToCompress);

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedTopics)
    FFastTopicMemoryArray ReplicatedTopics;

    UFUNCTION()
    void OnRep_ReplicatedTopics();

    void HandleSingleTopicReplicated(const FDynamicTopicMemoryItem& Item);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Gossip")
    void ServerRecordOverheardTopic(FGameplayTag TopicTag, const FString& TopicSummary, const FGuid& OriginatorGuid, float Credibility = 1.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|AI")
    void HandleHeardDialogueFromSpeaker(AActor* Speaker, const FString& UtteranceText, FGameplayTag SpokenLanguageTag, FGameplayTag VolumeTag);

    // Added EmotionTag default argument
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    void ServerBroadcastStreamingDialogueChunk(
        const FString& FormulatedChunk, 
        FGameplayTag SpokenLanguageTag, 
        FGameplayTag VolumeTag, 
        float BaseAudibleRadius, 
        bool bIsFinalChunk, 
        FGameplayTag EmotionTag = FGameplayTag()
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Reactivity")
    void ApplySocialReactionConsequences(const FDynamicDialogueReactionRule& Rule, AActor* TargetOffender);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Reactivity")
    void ServerHandleSpeechInterrupted(AActor* Interrupter);

    UPROPERTY(ReplicatedUsing = OnRep_Relationships) FFastRelationshipEdgeArray ReplicatedRelationships;
    UPROPERTY(ReplicatedUsing = OnRep_NeedsAndMoods) FFastNeedStateArray ReplicatedNeedsAndMoods;
    UPROPERTY(ReplicatedUsing = OnRep_KnownPlaces) FFastKnownPlaceArray ReplicatedKnownPlaces;
    UPROPERTY(ReplicatedUsing = OnRep_Professions) FFastAssignedProfessionArray ReplicatedProfessions;
    UPROPERTY(ReplicatedUsing = OnRep_CrimeMemories) FFastCrimeMemoryArray ReplicatedCrimeMemories;

    UFUNCTION() void OnRep_Relationships();
    UFUNCTION() void OnRep_NeedsAndMoods();
    UFUNCTION() void OnRep_KnownPlaces();
    UFUNCTION() void OnRep_Professions();
    UFUNCTION() void OnRep_CrimeMemories();
    void HandleSingleCrimeMemoryReplicated(const FCrimeMemoryItem& Item);

    void RebuildRelationshipCache();
    void RebuildNeedCache();
    void RebuildPlaceCache();
    void RebuildTopicCache();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetNeedValue(const FGameplayTag& NeedTag, float& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    float GetDecayedRelationshipMetric(const FGuid& TargetEntityGuid, const FGameplayTag& MetricTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetKnownPlaceByRole(const FGameplayTag& RoleTag, FDynamicKnownPlaceItem& OutPlace) const;

    const FDynamicRelationshipEdgeItem* FindRelationshipWithActor(const AActor* TargetActor) const;
    const FDynamicRelationshipEdgeItem* FindRelationshipByGuid(const FGuid& TargetGuid) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerRegisterKnownPlace(const FGameplayTag& PlaceTag, const FGameplayTag& RoleTag, const FGuid& AreaGuid, const FVector& WorldLocation, float InitialConfidence = 1.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerSetNeedValue(const FGameplayTag& NeedTag, float NewValue);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerRecordSocialInteraction(
        const FGuid& TargetEntityGuid,
        AActor* TargetActor,
        ERelationshipTargetKind TargetKind,
        FGameplayTag RelationshipTypeTag,
        const TArray<FTagFloatPair>& MetricDeltas,
        const FGameplayTagContainer& ContextTags
    );

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Cloud AI")
    FCloudAIContextFilterConfig ContextFilterConfig;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Cloud AI")
    bool bCloudAISuspended = false;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    void ServerSetCloudAISuspension(bool bSuspended);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI")
    FString SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Dialogue")
    FOnFastPathDialogueSpokenSignature OnFastPathDialogueSpoken;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Dialogue")
    FString ResolveActorDisplayName(const AActor* InActor) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Dialogue")
    FString ExtractFirstName(const FString& FullName) const;

    bool SelectBestUtterance(const TArray<FDynamicDialogueUtteranceDef>& CandidateUtterances, AActor* TargetActor, FDynamicDialogueUtteranceDef& OutSelectedUtterance, float& OutComputedWeight) const;
    FString FormatDialogueTokens(const FString& TemplateString, AActor* TargetActor, const FString& CustomLocation, const FString& CustomTime) const;
    FString FormatDialogueTokens(const FString& TemplateString, AActor* TargetActor, const FString& CustomLocation, const FString& CustomTime, const TMap<FString, FString>& CustomWildcards) const;

    bool TryResolveFastPathDialogue(FGameplayTag CategoryTag, AActor* TargetActor, FString& OutUtterance, TSoftObjectPtr<UAnimMontage>& OutMontage, FGameplayTag& OutVoiceCueTag, FGameplayTag SpecificProfileTag = FGameplayTag());
    bool TryResolveFastPathDialogue(FGameplayTag CategoryTag, AActor* TargetActor, FString& OutUtterance, TSoftObjectPtr<UAnimMontage>& OutMontage, FGameplayTag& OutVoiceCueTag, const TMap<FString, FString>& CustomWildcards, FGameplayTag SpecificProfileTag = FGameplayTag());
    bool TryResolveDialogueForBehavior(const FDynamicAIBehaviorDef& BehaviorDef, AActor* TargetActor, FString& OutUtterance, TSoftObjectPtr<UAnimMontage>& OutMontage, FGameplayTag& OutVoiceCueTag);
    bool TryResolveDialogueForBehavior(const FDynamicAIBehaviorDef& BehaviorDef, AActor* TargetActor, FString& OutUtterance, TSoftObjectPtr<UAnimMontage>& OutMontage, FGameplayTag& OutVoiceCueTag, const TMap<FString, FString>& CustomWildcards);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Dialogue")
    bool TriggerCalloutToTarget(AActor* TargetActor, float IgnoreTimeout = 5.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Dialogue")
    void ClearActiveCallout();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Law")
    FGameplayTag AssignedJurisdictionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Senses")
    float SightFieldOfViewDegrees = 120.0f;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Law")
    void ServerEvaluateAndBroadcastAction(
        FGameplayTag ActionTag,
        AActor* Perpetrator,
        AActor* Victim,
        FGuid OptionalStolenItemGuid,
        const TArray<FGuid>& CoConspirators
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Law")
    void ServerProcessCrimeSensoryEvent(
        FGameplayTag CrimeTag,
        AActor* Perpetrator,
        AActor* Victim,
        FGuid OptionalStolenItemGuid,
        const TArray<FGuid>& CoConspirators,
        const TMap<FGameplayTag, float>& SensorySignatures
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Law")
    bool ServerReportCrimesToAuthority(AActor* AuthorityFigure);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Law")
    bool ServerSearchTargetForContraband(ADynamicCharacter* Suspect);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Law")
    bool ServerInterrogateSuspect(ADynamicCharacter* Suspect, FName InterrogationConditionTree);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social|AI")
    bool GetOwnedPropertyLocation(FVector& OutLocation) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social|AI")
    bool GetOwnedRoomLocation(FGameplayTag RoomTypeTag, FVector& OutLocation) const;
    
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Law")
    float GetPerceivedBounty(const FGuid& SuspectGuid, FGameplayTag JurisdictionTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Law")
    FGameplayTag GetAuthorizedForceLevel(ADynamicCharacter* Suspect, FGameplayTag JurisdictionTag) const;

private:
    void HandleCalloutIgnored();
    void ExecuteDynamicWitnessReaction(const FCrimeMemoryItem& Memory, AActor* Perpetrator, AActor* Victim);

    TMap<FGuid, int32> CachedRelationshipIndices;
    TMap<FGameplayTag, float> CachedNeeds;
    TMap<FGameplayTag, FDynamicKnownPlaceItem> CachedPlacesByRole;
    TMap<FGameplayTag, int32> CachedTopicIndices;

    TWeakObjectPtr<AActor> ActiveCalloutTarget;
    FTimerHandle TimerHandle_CalloutTimeout;
    mutable double LastContextSerializationTime = 0.0;
};