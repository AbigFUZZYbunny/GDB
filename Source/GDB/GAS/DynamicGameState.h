// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;
class ADynamicPhysicsActor;
class UGameplayEffect;

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- Master Registries Populated via JSON ---
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
    TMap<FGameplayTag, FDynamicGeneLocusDef> GeneticsMasterRegistry;
    TMap<FGameplayTag, FDynamicBiomeDef> BiomeMasterRegistry;
    TMap<FGameplayTag, FDynamicWeatherPatternDef> WeatherMasterRegistry;
    TMap<FGameplayTag, FEvolutionProfileDef> EvolutionProfilesRegistry;
    TMap<FGameplayTag, FQuestDef> QuestMasterRegistry;
    TMap<FGameplayTag, FDynamicQuestGeneratorRule> QuestGeneratorRules;
    
    // --- Language Registry ---
    TMap<FGameplayTag, FDynamicLanguageDef> LanguageMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Calendar")
    FDynamicCalendarDef CalendarConfig;

    // Fully dynamic environment configuration
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    FDynamicEnvironmentSystemConfig EnvironmentConfig;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    FFastEnvironmentGridArray ReplicatedEnvironmentGrid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 GridWidth = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 GridHeight = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float CellSizeWorldUnits = 25000.0f;

    // Configures the dynamic element indices and metric formulas from loaded JSON
    void SetupDynamicEnvironment(const FDynamicEnvironmentSystemConfig& InConfig);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    float GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Environment")
    float ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Environment")
    void ServerTickEnvironmentSimulation(float ElapsedGameHours);

    // Dynamic O(1) tag-to-index resolution
    FORCEINLINE int32 ResolveElementArrayIndex(const FGameplayTag& ElementTag) const
    {
        if (const int32* FoundIdx = TrackedElementsMap.Find(ElementTag))
        {
            return *FoundIdx;
        }
        return INDEX_NONE;
    }

    void RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell);

    // --- UGameplayEffect Instance Cache ---
    UGameplayEffect* GetOrCreateCachedEffectObject(FGameplayTag EffectTag);
    void WarmGameplayEffectCache();

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|Crafting")
    TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    TMap<FGameplayTag, FComputedAbilityScoreInfo> ComputedAbilityScoresCache;

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

    // Universal Property Reflection and Dynamic Tag Resolver: zero hardcoded names
    float ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const;
    static float ExtractReflectedNumericProperty(const void* StructMemory, const UScriptStruct* StructType, FName PropertyPath);

    UPROPERTY() TArray<FPhysicsLimbConfig> GlobalLimbs;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<FName, FPhysicsJointNodeConfig> BoneToJointMap;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;

    UPROPERTY(Transient)
    TMap<FGameplayTag, TObjectPtr<UGameplayEffect>> DynamicEffectCDOCache;

    UPROPERTY() TArray<TObjectPtr<ADynamicSpatialActor>> InactiveSpatialActorPool;
    UPROPERTY() TArray<TObjectPtr<ADynamicPhysicsActor>> InactivePhysicsActorPool;

    // Runtime data-driven element tag index map built from EnvironmentConfig
    TMap<FGameplayTag, int32> TrackedElementsMap;
};