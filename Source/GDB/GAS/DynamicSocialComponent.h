// DynamicSocialComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDynamicRelationshipMetricChangedSignature, const FGuid&, TargetGuid, FGameplayTag, RelationshipTag, FGameplayTag, MetricTag, float, NewMetricValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicRelationshipTypeChangedSignature, const FGuid&, TargetGuid, FGameplayTag, OldRelationshipTag, FGameplayTag, NewRelationshipTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicNeedChangedSignature, FGameplayTag, NeedOrMoodTag, float, CurrentValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAILocalStateSuspensionChangedSignature, bool, bIsSuspendedWaitingForCloud);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicSocialComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicSocialComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Social")
    FDynamicRelationshipMetricChangedSignature OnRelationshipMetricChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Social")
    FDynamicRelationshipTypeChangedSignature OnRelationshipTypeChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Needs")
    FDynamicNeedChangedSignature OnNeedOrMoodChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Cloud AI")
    FOnAILocalStateSuspensionChangedSignature OnSuspensionStateChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Identity")
    FGuid CharacterPersistentGuid;

    UPROPERTY(ReplicatedUsing = OnRep_Professions, BlueprintReadOnly, Category = "Dynamic GAS|Profession")
    FFastAssignedProfessionArray ReplicatedProfessions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PersonalityProfile, Category = "Dynamic GAS|Personality")
    FDynamicPersonalityProfile PersonalityProfile;

    UPROPERTY(ReplicatedUsing = OnRep_RelationshipEdges)
    FFastRelationshipEdgeArray ReplicatedRelationships;

    UPROPERTY(ReplicatedUsing = OnRep_KnownPlaces)
    FFastKnownPlaceArray ReplicatedKnownPlaces;

    UPROPERTY(ReplicatedUsing = OnRep_NeedStates)
    FFastNeedStateArray ReplicatedNeedsAndMoods;

    UPROPERTY(ReplicatedUsing = OnRep_IsSuspendedForCloudAI, BlueprintReadOnly, Category = "Dynamic GAS|Cloud AI")
    bool bIsSuspendedForCloudAI = false;

    // Hard ceiling for spatial memory entries replicated per NPC
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dynamic GAS|SpatialKnowledge|Limits")
    int32 MaxRetainedKnownPlaces = 50;

    UFUNCTION() void OnRep_PersonalityProfile();
    UFUNCTION() void OnRep_RelationshipEdges();
    UFUNCTION() void OnRep_KnownPlaces();
    UFUNCTION() void OnRep_NeedStates();
    UFUNCTION() void OnRep_Professions();
    UFUNCTION() void OnRep_IsSuspendedForCloudAI();

    void HandleProfessionReplicated(const FDynamicAssignedProfessionItem& Item, bool bRemoved = false);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Profession")
    void ServerAddOrUpdateProfession(FGameplayTag ProfessionTag, float InitialProficiency = 1.0f, float Priority = 1.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Profession")
    bool ServerRemoveProfession(FGameplayTag ProfessionTag);

    void HandleRelationshipEdgeReplicated(const FDynamicRelationshipEdgeItem& Item, bool bRemoved = false);
    void HandleKnownPlaceReplicated(const FDynamicKnownPlaceItem& Item, bool bRemoved = false);
    void HandleNeedStateReplicated(const FDynamicNeedStateItem& Item, bool bRemoved = false);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    float GetDecayedRelationshipMetric(const FGuid& TargetGuid, FGameplayTag MetricTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    TArray<FTagFloatPair> GetAllDecayedRelationshipMetrics(const FGuid& TargetGuid) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    FGameplayTag GetRelationshipType(const FGuid& TargetGuid) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerRecordSocialInteraction(
        const FGuid& TargetGuid,
        AActor* OptionalTargetActor,
        ERelationshipTargetKind TargetKind,
        FGameplayTag RelationshipTag,
        const TArray<FTagFloatPair>& MetricDeltas,
        const FGameplayTagContainer& AddedContextTags);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerModifyRelationshipMetric(const FGuid& TargetGuid, FGameplayTag MetricTag, float DeltaAmount);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Cleanup")
    void ServerPruneDeadRelationships();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social|Cleanup")
    bool ServerSeverRelationship(const FGuid& TargetGuid);

    // Active memory decay and capacity management
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|SpatialKnowledge|Cleanup")
    void ServerPruneStalePlaces(float MinimumConfidenceThreshold = 0.05f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|SpatialKnowledge|Cleanup")
    bool ServerForgetPlace(FGameplayTag PlaceRoleTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Needs|Cleanup")
    bool ServerClearNeed(FGameplayTag NeedTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|SpatialKnowledge")
    void ServerRegisterKnownPlace(FGameplayTag PlaceRoleTag, FGameplayTag LandmarkOrArchetypeTag, const FGuid& ActorGuid, const FVector& WorldLocation, float Confidence = 1.0f);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SpatialKnowledge")
    bool GetKnownPlaceByRole(FGameplayTag PlaceRoleTag, FDynamicKnownPlaceItem& OutPlace) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Needs")
    float GetCurrentNeedOrMoodValue(FGameplayTag NeedOrMoodTag) const;

    void ServerModifyNeedOrMood(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag = FGameplayTag());

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Needs", meta=(AdvancedDisplay = "ConsumedItemOrActivityTag"))
    void ServerModifyNeedOrMoodBP(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Profession")
    FGameplayTag EvaluateCurrentExpectedActivity(float CurrentGameHour24, FGameplayTag& OutTargetPlaceRoleTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI")
    FString SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    void ServerSetCloudAISuspension(bool bSuspend);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
    void ServerRequestLLMDecision(ECloudAIRequestType RequestType);

    UFUNCTION()
    void HandleCloudAIResponseReceived(const FGuid& RequestGuid, const TArray<FCloudAICommandOutput>& Commands);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|P2P Telemetry")
    bool ExportComputeNodePayloadJson(FString& OutTelemetryJson) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Blocks")
    bool CanInitiateSocialInteraction(FGameplayTag InteractionType, ADynamicCharacter* TargetCharacter) const;

    float EvaluateMetricDecay(float BaseVal, const FRelationshipMetricRule& Rule, double ElapsedSeconds) const;    
    
    void RebuildRelationshipCache();
    void RebuildPlaceCache();
    void RebuildNeedCache();

private:
    void EnforceKnownPlaceMemoryCap();

    TMap<FGuid, int32> GuidToEdgeIndexMap;
    TMap<FGameplayTag, int32> RoleToPlaceIndexMap;
    TMap<FGameplayTag, int32> TagToNeedIndexMap;

    FGuid ActiveRemoteRequestGuid;
    static constexpr double SecondsPerGameDay = 86400.0;
};