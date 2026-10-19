// DynamicSocialComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicRelationshipChangedSignature, const FGuid&, TargetGuid, FGameplayTag, RelationshipTag, float,CurrentDecayedAffinity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicNeedChangedSignature, FGameplayTag, NeedOrMoodTag, float, CurrentValue);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicSocialComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicSocialComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Social")
    FDynamicRelationshipChangedSignature OnRelationshipChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Needs")
    FDynamicNeedChangedSignature OnNeedOrMoodChanged;

    // Persistent unique identity for cross-session and P2P miner graph indexing
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Identity")
    FGuid CharacterPersistentGuid;

    // Active Profession (links to ADynamicGameState::ProfessionMasterRegistry)
    UPROPERTY(ReplicatedUsing = OnRep_Professions, BlueprintReadOnly, Category = "Dynamic GAS|Profession")
    FFastAssignedProfessionArray ReplicatedProfessions;

    // Replicated Personality & Food/Drink preferences
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PersonalityProfile, Category = "Dynamic GAS|Personality")
    FDynamicPersonalityProfile PersonalityProfile;

    // --- Fast-Array Replicated Matrices ---
    UPROPERTY(ReplicatedUsing = OnRep_RelationshipEdges)
    FFastRelationshipEdgeArray ReplicatedRelationships;

    UPROPERTY(ReplicatedUsing = OnRep_KnownPlaces)
    FFastKnownPlaceArray ReplicatedKnownPlaces;

    UPROPERTY(ReplicatedUsing = OnRep_NeedStates)
    FFastNeedStateArray ReplicatedNeedsAndMoods;

    UFUNCTION() void OnRep_PersonalityProfile();
    UFUNCTION() void OnRep_RelationshipEdges();
    UFUNCTION() void OnRep_KnownPlaces();
    UFUNCTION() void OnRep_NeedStates();
    UFUNCTION() void OnRep_Professions();

    void HandleProfessionReplicated(const FDynamicAssignedProfessionItem& Item, bool bRemoved = false);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Profession")
    void ServerAddOrUpdateProfession(FGameplayTag ProfessionTag, float InitialProficiency = 1.0f, float Priority = 1.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Profession")
    bool ServerRemoveProfession(FGameplayTag ProfessionTag);

    // Fast-Array Incremental Callbacks (Must be public for FFastArraySerializerItem)
    void HandleRelationshipEdgeReplicated(const FDynamicRelationshipEdgeItem& Item, bool bRemoved = false);
    void HandleKnownPlaceReplicated(const FDynamicKnownPlaceItem& Item, bool bRemoved = false);
    void HandleNeedStateReplicated(const FDynamicNeedStateItem& Item, bool bRemoved = false);

    // --- Lazy Time-Decay Evaluation (No Tick Required) ---
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    bool GetDecayedRelationship(const FGuid& TargetGuid, float& OutAffinity, float& OutTrust, float& OutFamiliarity, float& OutIndebtedness) const;

    // Modifies or creates a relationship edge (Character-to-Character or Character-to-PhysicsActor)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Social")
    void ServerRecordSocialInteraction(
        const FGuid& TargetGuid,
        AActor* OptionalTargetActor,
        ERelationshipTargetKind TargetKind,
        FGameplayTag RelationshipTag,
        float AffinityDelta,
        float TrustDelta,
        float FamiliarityDelta,
        float DebtDelta,
        const FGameplayTagContainer& AddedContextTags);

    // Registers or updates knowledge of a place/workstation/bed/food source
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|SpatialKnowledge")
    void ServerRegisterKnownPlace(FGameplayTag PlaceRoleTag, FGameplayTag LandmarkOrArchetypeTag, const FGuid& ActorGuid, const FVector& WorldLocation, float Confidence = 1.0f);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SpatialKnowledge")
    bool GetKnownPlaceByRole(FGameplayTag PlaceRoleTag, FDynamicKnownPlaceItem& OutPlace) const;

    // Evaluates current Need/Mood value with lazy time-elapsed drift
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Needs")
    float GetCurrentNeedOrMoodValue(FGameplayTag NeedOrMoodTag) const;
    
    // C++ method with clean default parameter handling
    void ServerModifyNeedOrMood(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag = FGameplayTag());

    // UFUNCTION exposed to Blueprints without the unparseable default value on the macro pin
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Needs", meta=(AdvancedDisplay = "ConsumedItemOrActivityTag"))
    void ServerModifyNeedOrMoodBP(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag);

    // Resolves the highest priority task or schedule activity for the current 24h game hour
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Profession")
    FGameplayTag EvaluateCurrentExpectedActivity(float CurrentGameHour24, FGameplayTag& OutTargetPlaceRoleTag) const;

    // Serializes a compact, self-contained JSON telemetry packet for P2P Miner AI Compute Nodes
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|P2P Telemetry")
    bool ExportComputeNodePayloadJson(FString& OutTelemetryJson) const;

private:
    void RebuildRelationshipCache();
    void RebuildPlaceCache();
    void RebuildNeedCache();

    float ApplyContextualDecay(float BaseVal, float DecayRatePerDay, float MinFloor, double ElapsedSeconds, ERelationshipDecayCurve Curve) const;

    TMap<FGuid, int32> GuidToEdgeIndexMap;
    TMap<FGameplayTag, int32> RoleToPlaceIndexMap;
    TMap<FGameplayTag, int32> TagToNeedIndexMap;

    // Default conversion: 1 in-game day = 86400 game-world seconds (adjustable via GameState)
    static constexpr double SecondsPerGameDay = 86400.0;
};