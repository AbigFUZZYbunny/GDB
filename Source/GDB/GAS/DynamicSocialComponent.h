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

    // --- Query API ---
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetNeedValue(const FGameplayTag& NeedTag, float& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    float GetDecayedRelationshipMetric(const FGuid& TargetEntityGuid, const FGameplayTag& MetricTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetKnownPlaceByRole(const FGameplayTag& RoleTag, FDynamicKnownPlaceItem& OutPlace) const;

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

    // Filtered serialization to optimize token budgets and server serialization times
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social")
    FString SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const;

    // --- Fast-Path Dialogue & Contextual Chatter Engine ---
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social|Dialogue")
    bool TryResolveFastPathDialogue(
        FGameplayTag CategoryTag,
        AActor* TargetActor,
        FString& OutUtterance,
        TSoftObjectPtr<UAnimMontage>& OutMontage,
        FGameplayTag& OutVoiceCueTag
    );

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    bool TriggerCalloutToTarget(AActor* TargetActor, float IgnoreTimeout = 6.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    void HandleCalloutIgnored();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Dialogue")
    void ClearActiveCallout();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social|Dialogue")
    FString FormatDialogueTokens(
        const FString& TemplateString,
        AActor* TargetActor,
        const FString& CustomLocation = TEXT("the tavern"),
        const FString& CustomTime = TEXT("recently")
    ) const;

    // --- FastArray Cache Callbacks ---
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
    mutable double LastContextSerializationTime = 0.0;

    TWeakObjectPtr<AActor> ActiveCalloutTarget;
    FTimerHandle TimerHandle_CalloutTimeout;
};