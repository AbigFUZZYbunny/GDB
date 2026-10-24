// DynamicSocialComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnFastPathDialogueSpokenSignature, AActor*, Speaker, AActor*, Target, const FString&, Utterance);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicSocialComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicSocialComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Social|Dialogue")
    FOnFastPathDialogueSpokenSignature OnFastPathDialogueSpoken;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Social")
    FGuid CharacterPersistentGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Social")
    FString CharacterDisplayName;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Social")
    FFastRelationshipEdgeArray ReplicatedRelationships;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Social")
    FFastNeedStateArray ReplicatedNeedsAndMoods;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Social")
    FFastKnownPlaceArray ReplicatedKnownPlaces;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Social")
    FFastAssignedProfessionArray ReplicatedProfessions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social|LLM")
    FCloudAIContextFilterConfig ContextFilterConfig;

    // --- Social & Relational Query API ---
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetNeedValue(const FGameplayTag& NeedTag, float& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    float GetDecayedRelationshipMetric(const FGuid& TargetEntityGuid, const FGameplayTag& MetricTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetKnownPlaceByRole(const FGameplayTag& RoleTag, FDynamicKnownPlaceItem& OutPlace) const;

    const FDynamicRelationshipEdgeItem* FindRelationshipWithActor(const AActor* TargetActor) const;
    const FDynamicRelationshipEdgeItem* FindRelationshipByGuid(const FGuid& TargetGuid) const;

    // --- Authority Modification API ---
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

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerSetCloudAISuspension(bool bSuspended);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social")
    FString SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const;

    // --- Fast-Path Dialogue, Wildcards, & Relational Utterance Selection ---
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social|Dialogue")
    bool SelectBestUtterance(
        const TArray<FDynamicDialogueUtteranceDef>& CandidateUtterances,
        AActor* TargetActor,
        FDynamicDialogueUtteranceDef& OutSelectedUtterance,
        float& OutComputedWeight
    ) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social|Dialogue", meta = (AdvancedDisplay = "TargetActor,CustomLocation,CustomTime"))
    FString FormatDialogueTokens(
        const FString& TemplateString,
        AActor* TargetActor = nullptr,
        const FString& CustomLocation = TEXT("the tavern"),
        const FString& CustomTime = TEXT("recently")
    ) const;

    FString FormatDialogueTokens(
        const FString& TemplateString,
        AActor* TargetActor,
        const FString& CustomLocation,
        const FString& CustomTime,
        const TMap<FString, FString>& CustomWildcards
    ) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Dialogue")
    bool TryResolveFastPathDialogue(
        FGameplayTag CategoryTag,
        AActor* TargetActor,
        FString& OutUtterance,
        TSoftObjectPtr<UAnimMontage>& OutMontage,
        FGameplayTag& OutVoiceCueTag,
        FGameplayTag SpecificProfileTag
    );

    bool TryResolveFastPathDialogue(
        FGameplayTag CategoryTag,
        AActor* TargetActor,
        FString& OutUtterance,
        TSoftObjectPtr<UAnimMontage>& OutMontage,
        FGameplayTag& OutVoiceCueTag,
        const TMap<FString, FString>& CustomWildcards,
        FGameplayTag SpecificProfileTag
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Dialogue")
    bool TryResolveDialogueForBehavior(
        const FDynamicAIBehaviorDef& BehaviorDef,
        AActor* TargetActor,
        FString& OutUtterance,
        TSoftObjectPtr<UAnimMontage>& OutMontage,
        FGameplayTag& OutVoiceCueTag
    );

    bool TryResolveDialogueForBehavior(
        const FDynamicAIBehaviorDef& BehaviorDef,
        AActor* TargetActor,
        FString& OutUtterance,
        TSoftObjectPtr<UAnimMontage>& OutMontage,
        FGameplayTag& OutVoiceCueTag,
        const TMap<FString, FString>& CustomWildcards
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    bool TriggerCalloutToTarget(AActor* TargetActor, float IgnoreTimeout = 6.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    void HandleCalloutIgnored();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    void ClearActiveCallout();

    void RebuildRelationshipCache();
    void RebuildNeedCache();
    void RebuildPlaceCache();

protected:
    UPROPERTY()
    TMap<FGameplayTag, float> CachedNeeds;

    UPROPERTY()
    TMap<FGameplayTag, FDynamicKnownPlaceItem> CachedPlacesByRole;

    UPROPERTY(Replicated)
    bool bCloudAISuspended = false;

private:
    FString ResolveActorDisplayName(const AActor* InActor) const;
    FString ExtractFirstName(const FString& FullName) const;

    mutable double LastContextSerializationTime = 0.0;
    TWeakObjectPtr<AActor> ActiveCalloutTarget;
    FTimerHandle TimerHandle_CalloutTimeout;
};