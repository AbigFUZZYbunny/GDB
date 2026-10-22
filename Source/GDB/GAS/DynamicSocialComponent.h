// DynamicSocialComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicSocialComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicSocialComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Social")
    FString SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const;

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
};