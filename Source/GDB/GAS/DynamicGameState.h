// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;
class ADynamicPhysicsActor;

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();

    // --- Local Fast Authoritative Lookups (Populated via JSON on both Server & Client) ---
    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;
    TMap<FGameplayTag, FDynamicDeliveryArchetype> DeliveryArchetypes;
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;
    TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;
    TMap<FGameplayTag, FDynamicPhysicsActorDef> MasterPhysicsActorRegistry;
    TMap<FGameplayTag, FCharacterTemplateDef> CharacterTemplatesRegistry;
    TMap<FGameplayTag, FDynamicSkeletonDef> SkeletonMasterRegistry;
    TMap<FGameplayTag, FDynamicRelationshipTypeDef> RelationshipTypeRegistry;
    TMap<FGameplayTag, FDynamicProfessionDef> ProfessionMasterRegistry;
    TMap<FGameplayTag, FDynamicAIAgentDef> MasterAIAgentRegistry;
    TMap<FGameplayTag, FDynamicTransformationRule> ConversionRegistry;
    TMap<FGameplayTag, FDynamicEvolutionContainer> EvolutionMasterRegistry;
    TArray<FDynamicLifeStageDef> LifeStagesRegistry;
    TMap<FGameplayTag, FDynamicGeneLocusDef> GeneticsMasterRegistry;
    TMap<FGameplayTag, FDynamicBiomeDef> BiomeMasterRegistry;
    TMap<FGameplayTag, FDynamicWeatherPatternDef> WeatherMasterRegistry;
    TMap<FGameplayTag, FEvolutionProfileDef> EvolutionProfilesRegistry;
    TMap<FGameplayTag, FQuestDef> QuestMasterRegistry;
    TMap<FGameplayTag, FDynamicQuestGeneratorRule> QuestGeneratorRules;
    
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Calendar")
    FDynamicCalendarDef CalendarConfig;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    FFastEnvironmentGridArray ReplicatedEnvironmentGrid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 GridWidth = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 GridHeight = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float CellSizeWorldUnits = 25000.0f; // 250m per cell

    // Converts a World Location to a flat array index
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const;

    // Queries Temperature, Wetness, Sunlight, or Elemental Mana at a world coordinate
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    float GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const;

    // Consumes or generates Elemental Mana in the environment when a character casts abilities
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Environment")
    float ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount);

    // Simulates weather advection (clouds moving across cells), rain precipitation, homeostasis, and biome shifting
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Environment")
    void ServerTickEnvironmentSimulation(float ElapsedGameHours);

    int32 ResolveElementArrayIndex(FGameplayTag ElementTag) const;
    void RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell, float SunLightFactor);

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|Crafting")
    TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    TMap<FGameplayTag, FComputedAbilityScoreInfo> ComputedAbilityScoresCache;

    // --- Global Configurations ---
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Config")
    FAbilityScoreConfigData AbilityScoreConfig;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Config")
    FPhysicsCalculationSettings CalculationSettings;

    void SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig);
    void SetActiveDifficultyTag(FGameplayTag NewDifficultyTag);
    FComputedAbilityScoreInfo EvaluateAbilityScore(FGameplayTag AbilityTag, const FDynamicAbilityDef& AbilityDef) const;
    bool GetComputedAbilityScore(FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;
    void RebuildAllAbilityScores();

    bool GetCharacterTemplate(FGameplayTag TemplateTag, FCharacterTemplateDef& OutTemplate) const;

    ADynamicSpatialActor* AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn);

    ADynamicPhysicsActor* AcquirePhysicsActor(FGameplayTag ArchetypeTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn);

    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
    const FPhysicsJointNodeConfig* GetBoneJointData(const FName& BoneName) const;
    const TMap<uint8, FPhysicsLimbConfig>& GetLimbLookupMap() const { return LimbLookupMap; }

    const FPhysicsCalculationSettings& GetCalculationSettings() const { return CalculationSettings; }

private:
    void RebuildLimbMapsFromTransport();
    float ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const;

    UPROPERTY() TArray<FPhysicsLimbConfig> GlobalLimbs;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<FName, FPhysicsJointNodeConfig> BoneToJointMap;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;

    UPROPERTY() TArray<TObjectPtr<ADynamicSpatialActor>> InactiveSpatialActorPool;
    UPROPERTY() TArray<TObjectPtr<ADynamicPhysicsActor>> InactivePhysicsActorPool;
};