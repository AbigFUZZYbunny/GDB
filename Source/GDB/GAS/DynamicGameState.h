// DynamicGameState.h
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;
class ADynamicPhysicsActor;
class ADynamicEnvironmentChunkActor;
class UGameplayEffect;
class ADynamicCharacter;

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- Pre-Warmed Generic Gameplay Effects ---
    UPROPERTY(Transient)
    TObjectPtr<UGameplayEffect> GenericInstantEffect;

    UPROPERTY(Transient)
    TObjectPtr<UGameplayEffect> GenericDurationEffect;

    UPROPERTY(Transient)
    TObjectPtr<UGameplayEffect> GenericInfiniteEffect;

    void WarmGameplayEffectCache();
    void WarmGameplayEffectCacheAsync(TFunction<void()> OnComplete, float MaxBudgetPerFrameSeconds = 0.002f);
    UGameplayEffect* GetOrCreateCachedEffectObject(FGameplayTag EffectTag);

    // --- Data Registries ---
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicDeliveryArchetype> DeliveryArchetypes;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicPhysicsActorDef> MasterPhysicsActorRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FCharacterTemplateDef> CharacterTemplatesRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicSkeletonDef> SkeletonMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicRelationshipTypeDef> RelationshipTypeRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicProfessionDef> ProfessionMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicAIAgentDef> MasterAIAgentRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicTransformationRule> ConversionRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicEvolutionContainer> EvolutionMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicGeneLocusDef> GeneticsMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicBiomeDef> BiomeMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicWeatherPatternDef> WeatherMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FEvolutionProfileDef> EvolutionProfilesRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FQuestDef> QuestMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicQuestGeneratorRule> QuestGeneratorRules;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicLanguageDef> LanguageMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FNameGenerationRuleConfig> NameGenerationRulesRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FFastPathDialogueDef> FastPathDialogueRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicMovementStateDef> MovementStateRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;
    
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Registries")
    TMap<FGameplayTag, FDynamicNetworkNodeDef> NetworkMasterRegistry;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Configuration")
    FDynamicCalendarDef CalendarConfig;

    // --- Core Systems APIs ---
    void MergeStagingRegistry(FMasterRegistryStagingData&& StagingData);

    ADynamicCharacter* AcquireCharacter(FGameplayTag TemplateTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    ADynamicSpatialActor* AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    ADynamicPhysicsActor* AcquirePhysicsActor(FGameplayTag ArchetypeTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    
    void ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn);
    void ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn);

    void RegisterSpatialRoleActor(AActor* TargetActor, const FGameplayTagContainer& RoleTags);
    void UnregisterSpatialRoleActor(AActor* TargetActor);
    void GetActorsWithRoleTag(FGameplayTag RoleTag, TArray<AActor*>& OutActors) const;

    void RegisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk);
    void UnregisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk);
    void SetupDynamicEnvironment(const FDynamicEnvironmentSystemConfig& InConfig);
    float ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount);
    void ServerTickEnvironmentSimulation(float ElapsedGameHours);
    float GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const;
    int32 GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const;
    void RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell);

    bool GetCharacterTemplate(FGameplayTag TemplateTag, FCharacterTemplateDef& OutTemplate) const;
    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    void RebuildLimbMapsFromTransport();
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
    const FPhysicsJointNodeConfig* GetBoneJointData(const FName& BoneName) const;

    void SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig);
    void SetActiveDifficultyTag(FGameplayTag NewDifficultyTag);
    void RebuildAllAbilityScores();
    bool GetComputedAbilityScore(FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;

    const FPhysicsCalculationSettings& GetCalculationSettings() const { return CalculationSettings; }
    const TMap<uint8, FPhysicsLimbConfig>& GetLimbLookupMap() const { return LimbLookupMap; }
    
    void ReleaseCharacterToPool(ADynamicCharacter* ActorToReturn);

    void RegisterNetworkNode(class UDynamicNetworkComponent* Node);
    void UnregisterNetworkNode(class UDynamicNetworkComponent* Node);
    void MarkNetworkGraphDirty(class UDynamicNetworkComponent* Node);
    void QueueNetworkBurstTransfer(class UDynamicNetworkComponent* Node, FGameplayTag ResourceTag);

private:
    // --- Network Graph Tracking ---
    UPROPERTY()
    TArray<TWeakObjectPtr<class UDynamicNetworkComponent>> ActiveNetworkNodes;

    bool bIsNetworkGraphDirty = false;
    
    // Struct representing a disjoint subgraph for a specific resource
    struct FResourceGraphState
    {
        TArray<TWeakObjectPtr<class UDynamicNetworkComponent>> Sources;
        TArray<TWeakObjectPtr<class UDynamicNetworkComponent>> Consumers;
        TArray<TWeakObjectPtr<class UDynamicNetworkComponent>> Storages;
        TArray<TWeakObjectPtr<class UDynamicNetworkComponent>> Transporters;
    };
    
    TMap<int32, FResourceGraphState> ComputedResourceGraphs;
    
    void RebuildNetworkGraphs();
    void EvaluateNetworkGraphs(float DeltaSeconds);
    
    // Hooks directly into the existing async environment simulation task
    void SolveNetworksAsync(float HoursToSimulate);
    
    FComputedAbilityScoreInfo EvaluateAbilityScore(FGameplayTag AbilityTag, const FDynamicAbilityDef& AbilityDef) const;
    float ExtractReflectedNumericProperty(const void* StructMemory, const UScriptStruct* StructType, FName PropertyPath) const;
    float ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const;
    void SpawnServerEnvironmentChunks();

    int32 ResolveElementArrayIndex(FGameplayTag Tag) const { const int32* Found = TrackedElementsMap.Find(Tag); return Found ? *Found : INDEX_NONE; }

    FCriticalSection EffectCacheCriticalSection;
    TMap<FGameplayTag, TObjectPtr<UGameplayEffect>> DynamicEffectCDOCache;

    TArray<ADynamicCharacter*> InactiveCharacterPool;
    TArray<ADynamicSpatialActor*> InactiveSpatialActorPool;
    TArray<ADynamicPhysicsActor*> InactivePhysicsActorPool;

    TMap<TWeakObjectPtr<AActor>, TArray<FGameplayTag>> ActorToRegisteredRolesMap;
    TMap<FGameplayTag, TArray<TWeakObjectPtr<AActor>>> SpatialRoleActorRegistry;

    FPhysicsCalculationSettings CalculationSettings;
    TArray<FPhysicsLimbConfig> GlobalLimbs;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<FName, FPhysicsJointNodeConfig> BoneToJointMap;

    FAbilityScoreConfigData AbilityScoreConfig;
    TMap<FGameplayTag, FComputedAbilityScoreInfo> ComputedAbilityScoresCache;

    FDynamicEnvironmentSystemConfig EnvironmentConfig;
    TMap<FGameplayTag, int32> TrackedElementsMap;
    TMap<FIntPoint, TObjectPtr<ADynamicEnvironmentChunkActor>> ActiveChunkMap;
    TArray<FEnvironmentGridCellItem> EnvironmentReadBuffer;
    TArray<FEnvironmentGridCellItem> EnvironmentWriteBuffer;
    TArray<float> AdvectionDeltaScratchpad;

    bool bIsSimulationTaskRunning = false;
    double SimulationTaskStartTimestamp = 0.0;
    const double SimulationTaskTimeoutSeconds = 5.0;
    float AccumulatedUnsimulatedHours = 0.0f;
    TQueue<FEnvironmentManaModificationRecord, EQueueMode::Mpsc> PendingManaModifications;

    const int32 GridWidth = 256;
    const int32 GridHeight = 256;
    const int32 ChunkDimension = 8;
    const float CellSizeWorldUnits = 1000.0f;
};