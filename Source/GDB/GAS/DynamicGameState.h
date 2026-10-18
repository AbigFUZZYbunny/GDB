// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;
class ADynamicPhysicsActor;

USTRUCT(BlueprintType)
struct FTagAttributePair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FDynamicAttributeDef ValueDef;
};

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- UHT-Safe Transport Containers ---
    UPROPERTY(ReplicatedUsing = OnRep_AttributeMasterRegistryArr)
    TArray<FTagAttributePair> AttributeMasterRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_DeliveryArchetypesArr)
    TArray<FTagDeliveryArchetypePair> DeliveryArchetypesArr;

    UPROPERTY(ReplicatedUsing = OnRep_AbilityMasterRegistryArr)
    TArray<FTagAbilityDefPair> AbilityMasterRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_SharedConditionTreesArr)
    TArray<FNameConditionPair> SharedConditionTreesArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterMilestoneRegistryArr)
    TArray<FTagMilestonePair> MasterMilestoneRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterThresholdRegistryArr)
    TArray<FTagThresholdPair> MasterThresholdRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterEffectRegistryArr)
    TArray<FTagEffectPair> MasterEffectRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterPhysicsActorRegistryArr)
    TArray<FTagPhysicsActorDefPair> MasterPhysicsActorRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedLimbs)
    TArray<FPhysicsLimbConfig> ReplicatedLimbs;

    UPROPERTY(ReplicatedUsing = OnRep_AbilityScoreConfig)
    FAbilityScoreConfigData AbilityScoreConfig;

    // --- Local Fast Authoritative Lookups ---
    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;
    TMap<FGameplayTag, FDynamicDeliveryArchetype> DeliveryArchetypes;
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;
    TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;
    TMap<FGameplayTag, FDynamicPhysicsActorDef> MasterPhysicsActorRegistry;

    // Pre-computed O(1) Recipe Lookup Map (Target Archetype -> List of Recipes to craft it)
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|Crafting")
    TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;

    // Pre-computed O(1) cache of Ability Scores, Spell Levels (1-10), and Learn Costs for the active Difficulty
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    TMap<FGameplayTag, FComputedAbilityScoreInfo> ComputedAbilityScoresCache;

    void RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);
    void RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);
    void RegisterNewDynamicPhysicsActorDef(FGameplayTag ArchetypeTag, FDynamicPhysicsActorDef ActorDef);

    // --- Ability Score & Difficulty Engine ---
    void SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|AbilityScore")
    void SetActiveDifficultyTag(FGameplayTag NewDifficultyTag);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|AbilityScore")
    FComputedAbilityScoreInfo EvaluateAbilityScore(FGameplayTag AbilityTag, const FDynamicAbilityDef& AbilityDef) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|AbilityScore")
    bool GetComputedAbilityScore(FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;

    void RebuildAllAbilityScores();

    // --- Unified Spatial & Physics Actor Pooling (Server Authority) ---
    ADynamicSpatialActor* AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn);

    ADynamicPhysicsActor* AcquirePhysicsActor(FGameplayTag ArchetypeTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn);

    // --- Physics & Skeletal Topology Mappings ---
    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
    const FPhysicsJointNodeConfig* GetBoneJointData(const FName& BoneName) const;
    const FPhysicsCalculationSettings& GetCalculationSettings() const { return CalculationSettings; }
    const TMap<uint8, FPhysicsLimbConfig>& GetLimbLookupMap() const { return LimbLookupMap; }

protected:
    UFUNCTION() void OnRep_AttributeMasterRegistryArr();
    UFUNCTION() void OnRep_DeliveryArchetypesArr();
    UFUNCTION() void OnRep_AbilityMasterRegistryArr();
    UFUNCTION() void OnRep_SharedConditionTreesArr();
    UFUNCTION() void OnRep_MasterMilestoneRegistryArr();
    UFUNCTION() void OnRep_MasterThresholdRegistryArr();
    UFUNCTION() void OnRep_MasterEffectRegistryArr();
    UFUNCTION() void OnRep_MasterPhysicsActorRegistryArr();
    UFUNCTION() void OnRep_ReplicatedLimbs();
    UFUNCTION() void OnRep_AbilityScoreConfig();

private:
    void RebuildLimbMapsFromTransport();
    float ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const;

    UPROPERTY(Replicated)
    FPhysicsCalculationSettings CalculationSettings;

    TMap<FName, uint8> BoneToLimbMap;
    TMap<FName, FPhysicsJointNodeConfig> BoneToJointMap;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;

    UPROPERTY()
    TArray<TObjectPtr<ADynamicSpatialActor>> InactiveSpatialActorPool;

    UPROPERTY()
    TArray<TObjectPtr<ADynamicPhysicsActor>> InactivePhysicsActorPool;
};