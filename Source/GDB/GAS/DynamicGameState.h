// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "GameplayEffect.h"
#include "Containers/Queue.h"
#include "HAL/CriticalSection.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;
class ADynamicPhysicsActor;
class ADynamicCharacter;
class ADynamicEnvironmentChunkActor;
class UDynamicNetworkComponent;

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Master Registries loaded from JSON
    UPROPERTY() TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicDeliveryArchetype> DeliveryArchetypes;
    UPROPERTY() TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    UPROPERTY() TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    UPROPERTY() TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicPhysicsActorDef> MasterPhysicsActorRegistry;
    UPROPERTY() TMap<FGameplayTag, FCharacterTemplateDef> CharacterTemplatesRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicSkeletonDef> SkeletonMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicRelationshipTypeDef> RelationshipTypeRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicProfessionDef> ProfessionMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicAIAgentDef> MasterAIAgentRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicTransformationRule> ConversionRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicEvolutionContainer> EvolutionMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicGeneLocusDef> GeneticsMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicBiomeDef> BiomeMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicWeatherPatternDef> WeatherMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FEvolutionProfileDef> EvolutionProfilesRegistry;
    UPROPERTY() TMap<FGameplayTag, FQuestDef> QuestMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicQuestGeneratorRule> QuestGeneratorRules;
    UPROPERTY() TMap<FGameplayTag, FDynamicLanguageDef> LanguageMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FNameGenerationRuleConfig> NameGenerationRulesRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FFastPathDialogueDef> FastPathDialogueRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicMovementStateDef> MovementStateRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicNetworkNodeDef> NetworkMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicAIBehaviorDef> AIBehaviorMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicVoxelTypeDef> VoxelMasterRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicStructureMaterialDef> StructureMaterialRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicStructurePieceDef> StructurePieceRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicStructurePrefabDef> StructurePrefabRegistry;
    UPROPERTY() TMap<FGameplayTag, FDynamicRoomTypeDef> RoomTypeRegistry;

    UPROPERTY() FDynamicCalendarDef CalendarConfig;
    UPROPERTY() FPhysicsCalculationSettings CalculationSettings;
    UPROPERTY() TArray<FPhysicsLimbConfig> GlobalLimbs;
    UPROPERTY() FAbilityScoreConfigData AbilityScoreConfig;
    UPROPERTY() FDynamicEnvironmentSystemConfig EnvironmentConfig;

    // Generic Pre-warmed Gameplay Effects (Zero runtime GC allocations)
    UPROPERTY() TObjectPtr<UGameplayEffect> GenericInstantEffect;
    UPROPERTY() TObjectPtr<UGameplayEffect> GenericDurationEffect;
    UPROPERTY() TObjectPtr<UGameplayEffect> GenericInfiniteEffect;

    // Dynamic AI Behavior Evaluation & Cooldowns
    bool EvaluateBestDynamicBehavior(ADynamicCharacter* Character, FDynamicAIBehaviorDef& OutBestBehavior, float& OutBestScore) const;
    void RecordBehaviorExecutionCooldown(ADynamicCharacter* Character, FGameplayTag BehaviorTag, float CooldownSeconds);
    bool IsBehaviorOnCooldown(ADynamicCharacter* Character, FGameplayTag BehaviorTag) const;

    // Network Component Graph Management
    void RegisterNetworkNode(UDynamicNetworkComponent* Node);
    void UnregisterNetworkNode(UDynamicNetworkComponent* Node);
    void MarkNetworkGraphDirty(UDynamicNetworkComponent* Node);
    void QueueNetworkBurstTransfer(UDynamicNetworkComponent* Node, FGameplayTag ResourceTag);

    // Spatial Roles
    void RegisterSpatialRoleActor(AActor* TargetActor, const FGameplayTagContainer& RoleTags);
    void UnregisterSpatialRoleActor(AActor* TargetActor);
    void GetActorsWithRoleTag(FGameplayTag RoleTag, TArray<AActor*>& OutActors) const;

    // Environmental Simulation
    float GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const;
    float ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount);
    void ServerTickEnvironmentSimulation(float ElapsedGameHours);
    void RegisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk);
    void UnregisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk);
    void RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell);

    // Object Pooling
    ADynamicSpatialActor* AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn);
    ADynamicPhysicsActor* AcquirePhysicsActor(FGameplayTag ArchetypeTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn);
    ADynamicCharacter* AcquireCharacter(FGameplayTag TemplateTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleaseCharacterToPool(ADynamicCharacter* ActorToReturn);

    // Gameplay Effect Cache & Staging
    UGameplayEffect* GetOrCreateCachedEffectObject(FGameplayTag EffectTag);
    void WarmGameplayEffectCache();
    void WarmGameplayEffectCacheAsync(TFunction<void()> OnComplete, float MaxBudgetPerFrameSeconds = 0.002f);
    void MergeStagingRegistry(FMasterRegistryStagingData&& StagingData);

    const FPhysicsCalculationSettings& GetCalculationSettings() const { return CalculationSettings; }
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
    const FPhysicsJointNodeConfig* GetBoneJointData(const FName& BoneName) const;
    const TMap<uint8, FPhysicsLimbConfig>& GetLimbLookupMap() const { return LimbLookupMap; }
    bool GetCharacterTemplate(FGameplayTag TemplateTag, FCharacterTemplateDef& OutTemplate) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Ability Scoring")
    bool GetComputedAbilityScore(FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    const TArray<FEnvironmentGridCellItem>& GetEnvironmentReadBuffer() const { return EnvironmentReadBuffer; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    TArray<FEnvironmentGridCellItem>& GetMutableEnvironmentReadBuffer() { return EnvironmentReadBuffer; }

    // Grid Dimensions and Metric Getters (Resolves C2248)
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetGridWidth() const { return GridWidth; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetGridHeight() const { return GridHeight; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    float GetCellSizeWorldUnits() const { return CellSizeWorldUnits; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Environment")
    int32 GetChunkDimension() const { return ChunkDimension; }

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Voxels")
    FDynamicVoxelGenerationConfig VoxelGenerationConfig;

private:
    void SetupDynamicEnvironment(const FDynamicEnvironmentSystemConfig& InConfig);
    void SpawnServerEnvironmentChunks();
    int32 ResolveElementArrayIndex(const FGameplayTag& Tag) const { return TrackedElementsMap.FindRef(Tag); }

    float ExtractReflectedNumericProperty(const void* StructMemory, const UScriptStruct* StructType, FName PropertyPath) const;
    float ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const;
    FComputedAbilityScoreInfo EvaluateAbilityScore(FGameplayTag AbilityTag, const FDynamicAbilityDef& AbilityDef) const;
    void RebuildAllAbilityScores();

    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    void SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig);
    void SetActiveDifficultyTag(FGameplayTag NewDifficultyTag);
    void RebuildLimbMapsFromTransport();
    void RebuildNetworkGraphs();
    void EvaluateNetworkGraphs(float DeltaSeconds);

    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<FName, FPhysicsJointNodeConfig> BoneToJointMap;
    TMap<FGameplayTag, FComputedAbilityScoreInfo> ComputedAbilityScoresCache;

    // Environmental Grid Constants
    static constexpr int32 GridWidth = 64;
    static constexpr int32 GridHeight = 64;
    static constexpr int32 ChunkDimension = 8;
    static constexpr float CellSizeWorldUnits = 1000.0f;

    TArray<FEnvironmentGridCellItem> EnvironmentReadBuffer;
    TArray<FEnvironmentGridCellItem> EnvironmentWriteBuffer;
    TArray<float> AdvectionDeltaScratchpad;

    TMap<FIntPoint, TObjectPtr<ADynamicEnvironmentChunkActor>> ActiveChunkMap;
    TMap<FGameplayTag, int32> TrackedElementsMap;
    TQueue<FEnvironmentManaModificationRecord, EQueueMode::Mpsc> PendingManaModifications;

    bool bIsSimulationTaskRunning = false;
    double SimulationTaskStartTimestamp = 0.0;
    static constexpr double SimulationTaskTimeoutSeconds = 5.0;
    float AccumulatedUnsimulatedHours = 0.0f;

    // Actor Pools
    UPROPERTY() TArray<TObjectPtr<ADynamicSpatialActor>> InactiveSpatialActorPool;
    UPROPERTY() TArray<TObjectPtr<ADynamicPhysicsActor>> InactivePhysicsActorPool;
    UPROPERTY() TArray<TObjectPtr<ADynamicCharacter>> InactiveCharacterPool;

    // Spatial Roles Registry
    TMap<FGameplayTag, TArray<TWeakObjectPtr<AActor>>> SpatialRoleActorRegistry;
    TMap<TWeakObjectPtr<AActor>, TArray<FGameplayTag>> ActorToRegisteredRolesMap;

    // Network Components
    UPROPERTY() TArray<TWeakObjectPtr<UDynamicNetworkComponent>> ActiveNetworkNodes;
    bool bIsNetworkGraphDirty = false;

    struct FResourceGraphState
    {
        TArray<TWeakObjectPtr<UDynamicNetworkComponent>> Sources;
        TArray<TWeakObjectPtr<UDynamicNetworkComponent>> Consumers;
        TArray<TWeakObjectPtr<UDynamicNetworkComponent>> Storages;
        TArray<TWeakObjectPtr<UDynamicNetworkComponent>> Transporters;
    };
    TMap<int32, FResourceGraphState> ComputedResourceGraphs;

    mutable TMap<TWeakObjectPtr<ADynamicCharacter>, TMap<FGameplayTag, double>> CharacterBehaviorCooldowns;
};