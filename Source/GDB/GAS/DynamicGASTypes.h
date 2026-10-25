// DynamicGASTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimMontage.h"
#include "DynamicGASTypes.generated.h"

class UDynamicLanguageComponent;
class UDynamicAttributeSet;
class ADynamicGameState;
class UDynamicForceComponent;
class UDynamicInteractableComponent;
class UDynamicContainerComponent;
class ADynamicPhysicsActor;
class UDynamicSocialComponent;
class AAIController;
class UDynamicQuestComponent;
class UDynamicLifeComponent;
class UDynamicEquipmentComponent;
class ADynamicEnvironmentChunkActor;
class UDynamicQuestComponent;

// ============================================================================
// QUANTIZATION UTILITIES (RANGE-AWARE QUANTIZATION)
// ============================================================================
namespace DynamicQuantization
{
    FORCEINLINE uint8 QuantizeRange(float Value, float MinVal, float MaxVal)
    {
        const float Range = MaxVal - MinVal;
        if (Range <= KINDA_SMALL_NUMBER) return 0;
        const float ClampedPct = FMath::Clamp((Value - MinVal) / Range, 0.0f, 1.0f);
        return static_cast<uint8>(FMath::RoundToInt(ClampedPct * 255.0f));
    }

    FORCEINLINE float DequantizeRange(uint8 PackedVal, float MinVal, float MaxVal)
    {
        const float Pct = static_cast<float>(PackedVal) / 255.0f;
        return MinVal + (Pct * (MaxVal - MinVal));
    }

    FORCEINLINE uint8 QuantizeElement(float Value, float MaxCap = 200.0f)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Value / FMath::Max(1.0f, MaxCap)) * 255.0f), 0, 255));
    }
    FORCEINLINE float DequantizeElement(uint8 PackedValue, float MaxCap = 200.0f)
    {
        return (static_cast<float>(PackedValue) / 255.0f) * MaxCap;
    }
    FORCEINLINE uint8 QuantizeMoisture(float Value, float MaxCap = 150.0f)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Value / FMath::Max(1.0f, MaxCap)) * 255.0f), 0, 255));
    }
    FORCEINLINE float DequantizeMoisture(uint8 PackedValue, float MaxCap = 150.0f)
    {
        return (static_cast<float>(PackedValue) / 255.0f) * MaxCap;
    }
    FORCEINLINE uint8 QuantizeScale100(float Value)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Value / 100.0f) * 255.0f), 0, 255));
    }
    FORCEINLINE float DequantizeScale100(uint8 PackedValue)
    {
        return (static_cast<float>(PackedValue) / 255.0f) * 100.0f;
    }
    FORCEINLINE uint8 QuantizeSignedClamped(float Value)
    {
        const float Normalized = FMath::Clamp((Value + 1.0f) * 0.5f, 0.0f, 1.0f);
        return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
    }
    FORCEINLINE float DequantizeSignedClamped(uint8 PackedValue)
    {
        return ((static_cast<float>(PackedValue) / 255.0f) * 2.0f) - 1.0f;
    }
}

// ============================================================================
// ENUMS
// ============================================================================

UENUM(BlueprintType)
enum class EAbilityExecutionType : uint8
{
    Instant,
    Channeled,
    Charging,
    SpatialDelivery,
    Hitscan,
    Chain,
    Movement,
    Interaction
};

UENUM(BlueprintType)
enum class EMovementPhysicsType : uint8
{
    LaunchVelocity,
    RootMotionSource,
    CustomWallRun
};

UENUM(BlueprintType)
enum class ELogicOperator : uint8 
{ 
    And, 
    Or, 
    Nand, 
    Nor 
};

UENUM(BlueprintType)
enum class EConditionType : uint8
{
    LogicalGateOnly,
    TagExists,
    TagValuePair,
    AttributeValue,
    AttributeComparedToWorldTime
};

UENUM(BlueprintType)
enum class EComparisonOp : uint8 
{ 
    LessThan, 
    LessThanOrEqual, 
    Equal, 
    NotEqual, 
    GreaterThanOrEqual, 
    GreaterThan 
};

UENUM(BlueprintType)
enum class EValidationPhase : uint8 
{ 
    Attempt, 
    Charging, 
    Release, 
    Recovery 
};

UENUM(BlueprintType)
enum class EMultiShotType : uint8 
{ 
    Single, 
    Repeated, 
    Spread, 
    MultiRepeated 
};

UENUM(BlueprintType)
enum class ESequenceTriggerTiming : uint8 
{ 
    TriggerWithEachSelection, 
    TriggerAfterAllSelections 
};

UENUM(BlueprintType)
enum class EThresholdTriggerType : uint8 
{ 
    LevelUpAttribute, 
    EvolveAttribute 
};

UENUM(BlueprintType)
enum class EEffectDurationPolicy : uint8 
{ 
    Instant, 
    Infinite, 
    HasDuration 
};

UENUM(BlueprintType)
enum class EEffectModifierOp : uint8 
{ 
    Additive, 
    Multiplicative, 
    Override 
};

UENUM(BlueprintType)
enum class EGameplayEffectInhibitionPolicy : uint8 
{ 
    NeverInhibit, 
    InhibitWithTags 
};

UENUM(BlueprintType)
enum class EGameplayEffectStackingPolicy : uint8 
{ 
    None, 
    AggregateBySource, 
    AggregateByTarget 
};

UENUM(BlueprintType)
enum class EGameplayEffectStackExpirationPolicy : uint8 
{ 
    ClearEntireStack, 
    RemoveSingleStack, 
    RefreshToZero 
};

UENUM(BlueprintType)
enum class EGameplayEffectStackDurationRefreshPolicy : uint8 
{ 
    RefreshDuration, 
    NeverRefresh 
};

UENUM(BlueprintType)
enum class ESpatialDeliveryMotion : uint8
{
    StationaryGround,
    AttachedToCaster,
    StraightVelocity,
    SourceGuided,
    Homing
};

UENUM(BlueprintType)
enum class ESpatialShapeBehavior : uint8
{
    PointImpact,
    StayFullSize,
    ExpandOut,
    ContractIn,
    ExpandAndContract,
    PulseAtFullSize
};

UENUM(BlueprintType)
enum class EAuraEffectApplicationType : uint8
{
    SingleHit,
    TickInterval,
    ConstantRate,
    PassiveWhileIn
};

UENUM(BlueprintType)
enum class EPassBackTriggerEvent : uint8
{
    OnCast,
    OnCollision,
    OnDamageDealt
};

UENUM(BlueprintType)
enum class EInteractionPhysicsMode : uint8
{
    None,
    PushAwayFromCaster,
    PullTowardCaster,
    AngularHingeTorque
};

UENUM(BlueprintType)
enum class ECharacterGender : uint8
{
    Neutral,
    Male,
    Female,
    Other
};

UENUM(BlueprintType)
enum class ERelationshipTargetKind : uint8
{
    Character,
    PhysicsActor,
    SpatialLandmark,
    Faction
};

UENUM(BlueprintType)
enum class ERelationshipDecayCurve : uint8
{
    None,
    Linear,
    Exponential,
    StepThreshold
};

UENUM(BlueprintType)
enum class EQuestCategory : uint8
{
    Story,
    DynamicEconomic,
    DynamicBounty,
    WorldEvent
};

UENUM(BlueprintType)
enum class EQuestObjectiveType : uint8
{
    FetchAndDeliver,
    CullTargets,
    EscortNPC,
    InteractWithActor,
    InvestigateArea,
    CustomCondition
};

UENUM(BlueprintType)
enum class EQuestStepState : uint8
{
    Inactive,
    Active,
    Completed,
    Failed,
    Abandoned
};

UENUM(BlueprintType)
enum class EAbilityScoreScalingMode : uint8
{
    DirectProportional,
    InverseProportional
};

UENUM(BlueprintType)
enum class ENameGenerationMode : uint8
{
    StaticPool,
    Compound,
    PatronymicOrClan,
    Locational
};

UENUM(BlueprintType)
enum class EDynastyEntityType : uint8
{
    FamilyHouse,
    Business,
    Guild,
    City,
    Country,
    Faction
};  

UENUM(BlueprintType)
enum class ECloudAIRequestType : uint8
{
    ComplexDecision,
    DailySchedulePlan,
    DynamicDialogue,
    QuestConditionGeneration
};

UENUM(BlueprintType)
enum class ECloudAIRequestStatus : uint8
{
    Unsent,
    PendingRemoteResponse,
    Completed,
    TimedOut,
    Failed
};

UENUM(BlueprintType)
enum class ECloudAIExecutionBackend : uint8
{
    RemoteCloudMiner,       // MMO production: HTTP POST to remote cloud or distributed coin miner pool
    LocalDedicatedNodeHttp, // Localhost/LAN node: HTTP POST to local LLM daemon (e.g. 127.0.0.1:11434 / Ollama)
    LocalRuleBasedFallback  // Local hosted co-op (1-5 players): Zero HTTP socket overhead, instant rule-based simulation
};

UENUM(BlueprintType)
enum class ENetworkNodeType : uint8
{
    Source,       // Generates resources (e.g., Solar Panel, Magic Node)
    Consumer,     // Drains resources (e.g., Lightbulb, Shield Generator)
    Storage,      // Stores resources bidirectionally (e.g., Battery, Fireball)
    Transporter   // Connects nodes, transfers up to FlowLimit (e.g., Pipe, Wire)
};

// Priority levels for task queue distribution
UENUM(BlueprintType)
enum class ECloudAITaskPriority : uint8
{
    Low = 0,    // immediate tasks > Group schedules > Individual schedules (Assigned to High Latency Miners)
    Medium = 1, // Ambient background chatter > immediate tasks
    High = 2    // Direct Player Conversation > Ambient background chatter (Strictly Assigned to Low Latency and High Accuracy Miners)
};

UENUM(BlueprintType)
enum class ENpcBehaviorExecutionType : uint8
{
    Ability,        // Triggers GAS Gameplay Ability via AbilitySystemComponent
    Interaction,    // Executes an interaction with a target actor (Eat, Sleep, Open, Use)
    Movement,       // Switches Mover locomotion state (Walk, Run, Sprint, Flee)
    Montage,        // Plays a contextual animation montage with custom playrate
    Wait            // Idles/waits for a designated duration
};

// ============================================================================
// LOCALIZATION & DICTIONARY MAP WRAPPERS
// ============================================================================

USTRUCT(BlueprintType)
struct FTagStringPair
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Localization")
    FGameplayTag KeyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Localization")
    FString DisplayValue = TEXT("");
};

USTRUCT(BlueprintType)
struct FDynamicTagStringPair
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag KeyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FString Value = TEXT("");

    bool operator==(const FDynamicTagStringPair& Other) const
    {
        return KeyTag == Other.KeyTag;
    }
};

USTRUCT(BlueprintType)
struct FDynamicTagStringMapWrapper
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TArray<FDynamicTagStringPair> ReplicatedEntries;

    UPROPERTY(NotReplicated)
    TMap<FGameplayTag, FString> LocalLookupMap;

    void SyncMapFromChangesArray()
    {
        LocalLookupMap.Empty(ReplicatedEntries.Num());
        for (const auto& Pair : ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid())
            {
                LocalLookupMap.Add(Pair.KeyTag, Pair.Value);
            }
        }
    }
};

USTRUCT(BlueprintType)
struct FDynamicLocalizationProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Localization")
    TArray<FTagStringPair> ReplicatedEntries;

    UPROPERTY(NotReplicated)
    TMap<FGameplayTag, FString> LocalLookupMap;

    void SyncMapFromChangesArray()
    {
        LocalLookupMap.Empty(ReplicatedEntries.Num());
        for (const FTagStringPair& Pair : ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid())
            {
                LocalLookupMap.Add(Pair.KeyTag, Pair.DisplayValue);
            }
        }
    }

    FString GetLocalizedText(FGameplayTag LocalizationTag) const
    {
        return LocalLookupMap.FindRef(LocalizationTag);
    }
};

USTRUCT(BlueprintType)
struct FTagFloatPair
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag KeyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct FDynamicTagFloatMapWrapper
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TArray<FTagFloatPair> ReplicatedEntries;

    UPROPERTY(NotReplicated)
    TMap<FGameplayTag, float> LocalLookupMap;

    void SyncMapFromChangesArray()
    {
        LocalLookupMap.Empty(ReplicatedEntries.Num());
        for (const FTagFloatPair& Pair : ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid())
            {
                LocalLookupMap.Add(Pair.KeyTag, Pair.Value);
            }
        }
    }

    void UpdateEntry(FGameplayTag Key, float NewValue)
    {
        LocalLookupMap.Add(Key, NewValue);
        int32 FoundIdx = ReplicatedEntries.IndexOfByPredicate([Key](const FTagFloatPair& Element) { return Element.KeyTag == Key; });
        if (FoundIdx != INDEX_NONE)
        {
            ReplicatedEntries[FoundIdx].Value = NewValue;
        }
        else
        {
            FTagFloatPair NewPair;
            NewPair.KeyTag = Key;
            NewPair.Value = NewValue;
            ReplicatedEntries.Add(NewPair);
        }
    }
};

// ============================================================================
// ATTRIBUTE FAST ARRAY (1-BYTE LIMB ID)
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicAttributeItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    uint8 LimbId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag AttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float Value = 0.0f;

    void PreReplicatedRemove(const struct FFastAttributeArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastAttributeArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastAttributeArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastAttributeArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicAttributeItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicAttributeSet* OwningAttributeSet = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicAttributeItem, FFastAttributeArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastAttributeArray> : public TStructOpsTypeTraitsBase2<FFastAttributeArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// INTERFACE: POOLED OBJECT SCRUBBING
// ============================================================================
UINTERFACE(MinimalAPI, BlueprintType)
class UDynamicPoolScrubbableInterface : public UInterface
{
    GENERATED_BODY()
};

class GDB_API IDynamicPoolScrubbableInterface
{
    GENERATED_BODY()

public:
    /**
     * Authoritatively resets all runtime mutations (ASC effects, attributes, forces, 
     * materials, containers, interactables) before returning an actor from the pool.
     */
    virtual void ResetToDefaultPoolState() = 0;
};

// ============================================================================
// INTERACTION TARGET POINT (WITH RESOLVED COMPONENT POINTER CACHING)
// ============================================================================
USTRUCT(BlueprintType)
struct FInteractionTargetPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTag TargetPointTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    TWeakObjectPtr<AActor> LinkedTargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FName ComponentName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FName BoneOrSocketName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FTransform LocalOffset = FTransform::Identity;

    // Transient cached pointer: eliminates TInlineComponentArray iterations and FName comparisons
    UPROPERTY(NotReplicated)
    mutable TWeakObjectPtr<USceneComponent> CachedResolvedComponent = nullptr;

    void InvalidateCache()
    {
        CachedResolvedComponent.Reset();
    }
};

USTRUCT(BlueprintType)
struct FDynamicInteractableStateItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTag StateTag;

    void PreReplicatedRemove(const struct FFastInteractableStateArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastInteractableStateArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastInteractableStateArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastInteractableStateArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicInteractableStateItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicInteractableComponent* OwningInteractable = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicInteractableStateItem, FFastInteractableStateArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastInteractableStateArray> : public TStructOpsTypeTraitsBase2<FFastInteractableStateArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicNetworkResourceConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ResourceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ENetworkNodeType NodeType = ENetworkNodeType::Transporter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxFlowRate = 100.0f; // Added fallback/default flow rate

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxBurstCapacity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag VolumeAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag MaxVolumeAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag FlowRateAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PressureAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsEnvironmentLinked = false;
};

USTRUCT(BlueprintType)
struct FDynamicNetworkNodeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FDynamicNetworkResourceConfig> SupportedResources;
};

// --- Fast Array for Visual & Logical Connections ---

USTRUCT(BlueprintType)
struct FNetworkConnectionItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<class UDynamicNetworkComponent> ConnectedNode;

    void PreReplicatedRemove(const struct FFastNetworkConnectionArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastNetworkConnectionArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastNetworkConnectionArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastNetworkConnectionArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FNetworkConnectionItem> Items;

    UPROPERTY(NotReplicated)
    class UDynamicNetworkComponent* OwningComponent = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FNetworkConnectionItem, FFastNetworkConnectionArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastNetworkConnectionArray> : public TStructOpsTypeTraitsBase2<FFastNetworkConnectionArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// ITEM & CONTAINER GRID SYSTEM
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicItemDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Item")
    int32 GridSizeX = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Item")
    int32 GridSizeY = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Item")
    bool bCanRotate = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Item")
    int32 MaxStackSize = 1;
};

// ============================================================================
// ITEM & CONTAINER GRID SYSTEM (NESTED CONTAINERS VIA GUID)
// ============================================================================
USTRUCT(BlueprintType)
struct FCharacterTransformHistorySnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    double ServerTimestamp = 0.0;

    UPROPERTY()
    FVector CapsuleLocation = FVector::ZeroVector;

    UPROPERTY()
    FQuat CapsuleRotation = FQuat::Identity;

    UPROPERTY()
    float ViewPitch = 0.0f;

    UPROPERTY()
    FGameplayTag ActiveMovementState;
};

USTRUCT(BlueprintType)
struct FDynamicContainerSlotItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    int32 GridX = 0;

    UPROPERTY()
    int32 GridY = 0;

    UPROPERTY()
    bool bIsRotated = false;

    UPROPERTY()
    FGameplayTag StoredActorArchetypeTag;

    UPROPERTY()
    int32 StackCount = 0;

    UPROPERTY()
    TArray<FDynamicAttributeItem> SavedAttributeState;

    UPROPERTY()
    FGameplayTagContainer SavedInteractableStates;

    void PreReplicatedRemove(const struct FFastContainerSlotArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastContainerSlotArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastContainerSlotArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastContainerSlotArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicContainerSlotItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicContainerComponent* OwningContainer = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicContainerSlotItem, FFastContainerSlotArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastContainerSlotArray> : public TStructOpsTypeTraitsBase2<FFastContainerSlotArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicContainerDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FGameplayTag ContainerIdentifierTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    int32 GridWidth = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    int32 GridHeight = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    TArray<FIntPoint> BlockedGridCells;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    float MaxWeightCapacity = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FGameplayTagContainer AllowedItemTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FGameplayTagContainer ForbiddenItemTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    TArray<FDynamicContainerSlotItem> DefaultStoredItems;
};

// ============================================================================
// CRAFTING SYSTEM
// ============================================================================

USTRUCT(BlueprintType)
struct FRecipeIngredient
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    FGameplayTag ArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    int32 RequiredQuantity = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    bool bConsume = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    bool bScaleWithTargetSize = false;
};

USTRUCT(BlueprintType)
struct FRecipeEnvironmentRequirement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    FGameplayTagContainer RequiredStateTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    float MaxDistance = 300.0f;
};

USTRUCT(BlueprintType)
struct FDynamicRecipeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    FGameplayTag RecipeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FRecipeIngredient> Ingredients;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FRecipeEnvironmentRequirement> NearbyRequirements;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    FName RequiredConditionTree = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    float EffortRequired = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FRecipeIngredient> AdditionalProducts;
};

USTRUCT(BlueprintType)
struct FDynamicRecipeList
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FDynamicRecipeDef> Recipes;
};

// ============================================================================
// GASP STAGE ANIMATION & PRESENTATION
// ============================================================================

USTRUCT(BlueprintType)
struct FAbilityStagePresentation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    TSoftObjectPtr<UAnimMontage> StageMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FName MontageSectionName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    float PlayRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FName GaspPhysicsControlProfile = TEXT("UpperBodyCast");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    float GaspKinematicBlendWeight = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FGameplayTag StageVfxCueTag;
};

USTRUCT(BlueprintType)
struct FDynamicAbilityStageVisuals
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FAbilityStagePresentation AttemptStage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FAbilityStagePresentation ChargingStage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FAbilityStagePresentation ReleaseStage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FAbilityStagePresentation RecoveryStage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Presentation")
    FAbilityStagePresentation ImpactStage;
};

USTRUCT(BlueprintType)
struct FSourcePassBackRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PassBack")
    EPassBackTriggerEvent TriggerEvent = EPassBackTriggerEvent::OnDamageDealt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PassBack")
    FGameplayTag SourceAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PassBack")
    float BaseGrantAmount = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PassBack")
    bool bScaleWithDamageDealt = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PassBack")
    float DamageRatioScalar = 0.25f;
};

// ============================================================================
// SPATIAL DELIVERY SYSTEM
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicDeliveryArchetype
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    TSoftObjectPtr<UStaticMesh> MeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag PrimaryGameplayCue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FDynamicTagFloatMapWrapper Metrics;
};

// ============================================================================
// SPATIAL DELIVERY SYSTEM (WITH LIVENESS & TARGET VALIDATION GUARDS)
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicSpatialDeliveryDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag ArchetypeLinkTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag SpeedMetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    ESpatialDeliveryMotion MotionType = ESpatialDeliveryMotion::StraightVelocity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    ESpatialShapeBehavior ShapeBehavior = ESpatialShapeBehavior::PointImpact;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    EAuraEffectApplicationType ApplicationType = EAuraEffectApplicationType::SingleHit;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float MinRadius = 16.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float MaxRadius = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float Duration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float SpeedOrExpansionRate = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float HomingTurnRate = 240.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float TickInterval = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    EMultiShotType MultiShotType = EMultiShotType::Single;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    int32 SpawnCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float SpreadAngleDegrees = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float PowerDecayPerAdditional = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag DirectTargetAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float DirectAttributeMagnitude = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float KineticImpulseOnHit = 900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    TArray<FGameplayTag> EffectsToApply;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FDynamicTagFloatMapWrapper RuntimeMetricModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTagContainer PropagatedTriggerTags;

    // --- Target Liveness & Faction Validation Filters ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery|Validation")
    FGameplayTagContainer RequiredTargetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery|Validation")
    FGameplayTagContainer BlockedTargetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery|Validation")
    FName TargetValidationConditionTree = NAME_None;
};

// ============================================================================
// INTERACTION PROFILE
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicInteractionProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTag InteractionActionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTag KinematicTargetTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTag ForceMetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FName CasterEffectorBone = TEXT("hand_r");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    float MaxInteractionDistance = 220.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    float TargetSweepRadius = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer RequiredTargetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer BlockedTargetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FName TargetValidationConditionTree = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer TagsToAddToTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer TagsToRemoveFromTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    EInteractionPhysicsMode PhysicsMode = EInteractionPhysicsMode::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FDynamicTagFloatMapWrapper InteractionMetrics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    TArray<FGameplayTag> EffectsToApplyToTarget;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Voxels")
    bool bCanModifyVoxels = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Voxels")
    FGameplayTag VoxelModificationTypeTag; // e.g., Element.Air for digging, Voxel.Type.Stone for building
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Voxels")
    float VoxelModificationRadius = 150.0f;
};

// ============================================================================
// MOVEMENT, CHAIN, & GASP PROFILES
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicChainProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Chain")
    FGameplayTag DamageParamTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Chain")
    int32 MaxBounces = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Chain")
    float DamageFalloffPerBounce = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Chain")
    float BounceSearchRadius = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Chain")
    FGameplayTagContainer ForbiddenTargetTags;
};

USTRUCT(BlueprintType)
struct FGaspMotorConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GASP Integration")
    FDynamicTagFloatMapWrapper RequiredAttributeThresholds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GASP Integration")
    float NewtonReflectiveForceFactor = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GASP Integration")
    float PhysicsBlendWeightMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct FDynamicMovementProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    EMovementPhysicsType PhysicsType = EMovementPhysicsType::LaunchVelocity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FGameplayTag MotionTagIdentifier;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FGameplayTag VelocityMetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FDynamicTagFloatMapWrapper MovementMetrics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|GASP Integration")
    FGaspMotorConfig GaspMotorSettings;
};

// ============================================================================
// ATTRIBUTE & EFFECT DEFINITIONS
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicAttributeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag AttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float DefaultValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    bool bDoesReplicate = true;
};

USTRUCT(BlueprintType)
struct FDynamicEffectExecutionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect")
    FString ExecutionClassPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect")
    FGameplayTag ScalingAttribute;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect")
    float ScalingMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct FDynamicModifierDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag TargetAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Modifiers")
    EEffectModifierOp ModifierOp = EEffectModifierOp::Additive;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float BaseMagnitude = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTagContainer SourceTagRequirements;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTagContainer TargetTagRequirements;
};

USTRUCT(BlueprintType)
struct FDynamicEffectDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Core")
    EEffectDurationPolicy DurationPolicy = EEffectDurationPolicy::Instant;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Core")
    float DurationMagnitude = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Core")
    float Period = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Core")
    EGameplayEffectInhibitionPolicy PeriodicInhibitionPolicy = EGameplayEffectInhibitionPolicy::NeverInhibit;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Stacking")
    EGameplayEffectStackingPolicy StackingPolicy = EGameplayEffectStackingPolicy::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Stacking")
    int32 MaxStackCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Stacking")
    EGameplayEffectStackExpirationPolicy StackExpirationPolicy = EGameplayEffectStackExpirationPolicy::ClearEntireStack;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Stacking")
    EGameplayEffectStackDurationRefreshPolicy StackDurationRefreshPolicy = EGameplayEffectStackDurationRefreshPolicy::RefreshDuration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Tags")
    FGameplayTagContainer GameplayCues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Tags")
    FGameplayTagContainer GrantedTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Logic")
    FName ConditionTreeLinkName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Logic")
    TArray<FDynamicModifierDef> Modifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|Logic")
    TArray<FDynamicEffectExecutionDef> ExecutionCalculations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Effect|PassBack")
    TArray<FSourcePassBackRule> SourcePassBackRules;
};

// ============================================================================
// TRUTH LOGIC TREES
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicConditionNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    EConditionType ConditionType = EConditionType::LogicalGateOnly;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    ELogicOperator LogicOperator = ELogicOperator::And;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    TArray<int32> ChildIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    FName UserDefinedKey;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    bool bCheckSource = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    EComparisonOp ComparisonOperator = EComparisonOp::Equal;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    float ComparisonValue = 0.0f;
};

USTRUCT(BlueprintType)
struct FDynamicConditionTreeContainer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic Engine")
    TArray<FDynamicConditionNode> FlatNodes;
};

// ============================================================================
// EXTERNAL VOXEL GENERATION & TERRAIN CONFIGURATION
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicVoxelBiomeMapping
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FColor MapColorKey = FColor::Black;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag SurfaceVoxelTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag SubsurfaceVoxelTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag BedrockVoxelTag;
};

USTRUCT(BlueprintType)
struct FDynamicVoxelGenerationConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UTexture2D> SurfaceHeightMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UTexture2D> BiomeMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UTexture2D> CaveDensityMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UTexture2D> EnergyLevelMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WorldHeightScale = 10000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseElevation = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MapWorldBounds = 200000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FDynamicVoxelBiomeMapping> BiomeColorMappings;
};

USTRUCT(BlueprintType)
struct FDynamicVoxelTypeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag VoxelTypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ElementalContributionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EnvironmentDensityContribution = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxHP = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DestroyedItemArchetype;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UMaterialInterface> VoxelMaterial;
};

// ============================================================================
// VOXEL NETWORK DELTAS (FastArray)
// ============================================================================
USTRUCT(BlueprintType)
struct FVoxelDeltaItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    int32 VoxelIndex = 0;

    UPROPERTY()
    uint8 VoxelTypeID = 0;

    UPROPERTY()
    uint8 CurrentHP = 0;

    void PreReplicatedRemove(const struct FFastVoxelDeltaArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastVoxelDeltaArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastVoxelDeltaArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastVoxelDeltaArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FVoxelDeltaItem> Items;

    UPROPERTY(NotReplicated)
    class ADynamicVoxelChunkActor* OwningChunk = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FVoxelDeltaItem, FFastVoxelDeltaArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastVoxelDeltaArray> : public TStructOpsTypeTraitsBase2<FFastVoxelDeltaArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// CONTINENT-SCALE VOXEL STREAMING & TEMPORAL LOD
// ============================================================================
UENUM(BlueprintType)
enum class EVoxelLODState : uint8
{
    ActiveHighRes   UMETA(DisplayName = "Active High-Res (Actor Spawned)"),
    ActiveLowRes    UMETA(DisplayName = "Active Low-Res (Actor Spawned)"),
    VirtualNearby   UMETA(DisplayName = "Virtual Nearby (1 Hr Updates)"),
    VirtualDistant  UMETA(DisplayName = "Virtual Distant (6 Hr Updates)"),
    VirtualHorizon  UMETA(DisplayName = "Virtual Horizon (24 Hr Updates)")
};

USTRUCT(BlueprintType)
struct FVoxelChunkPersistentData
{
    GENERATED_BODY()

    UPROPERTY()
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    // Deltas applied by players or generative simulation
    UPROPERTY()
    TArray<FVoxelDeltaItem> SavedDeltas;

    UPROPERTY()
    double LastGenerativeUpdateWorldTime = 0.0;

    UPROPERTY()
    EVoxelLODState CurrentLODState = EVoxelLODState::VirtualHorizon;
    
    // Only true if the visual ADynamicVoxelChunkActor is currently spawned in the world
    UPROPERTY()
    bool bIsSpawnedAsActor = false;
};

// ============================================================================
// BUILDING SOCKETS & ADVANCED MATERIAL DEFS
// ============================================================================

USTRUCT(BlueprintType)
struct FBuildingSocketDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SocketName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTransform RelativeTransform = FTransform::Identity;

    // Optional: Only allow specific piece tags to snap here (e.g., "Build.Piece.Roof")
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTagContainer AllowedConnections;
};

// ============================================================================
// BUILDING & STRUCTURAL INTEGRITY CONFIGURATION
// ============================================================================

USTRUCT(BlueprintType)
struct FStructureMaterialDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag MaterialTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StructuralDensity = 1.0f; // Affects mass

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SupportStrength = 100.0f; // Base weight it can hold before snapping

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InsulationValue = 5.0f; // Weather/temperature resistance

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DurabilityMultiplier = 1.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DestroyedYieldArchetype; // e.g., "Item.Resource.Wood"
};

USTRUCT(BlueprintType)
struct FStructurePieceDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PieceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UStaticMesh> MeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMass = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxHP = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OverlapConnectionRadius = 150.0f; // Radius to connect to adjacent pieces
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FBuildingSocketDef> ConnectionSockets;
};

// ============================================================================
// BUILDING & STRUCTURAL INTEGRITY CONFIGURATION (UPDATED)
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicStructureMaterialDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag MaterialTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StructuralDensity = 1.0f; // Affects mass and load-bearing stress

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SupportStrength = 100.0f; // Base weight it can hold before snapping

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InsulationValue = 5.0f; // Weather/temperature resistance

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DurabilityMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DestroyedYieldArchetype; // E.g., Item.Resource.Wood
};

USTRUCT(BlueprintType)
struct FStructureMaterialRequirement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag MaterialArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 RequiredQuantity = 1;
};

USTRUCT(BlueprintType)
struct FDynamicStructurePieceDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PieceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UStaticMesh> MeshAsset;

    // Defines Enshrouded-style micro-voxel blocks (Stone) vs Valheim-style socketed structural pieces (Wood/Logs)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsVolumetricBlock = false;

    // If a block, what is its micro-grid dimension (e.g., 25.0f for 25cm blocks)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float VolumetricGridSnapSize = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMass = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxHP = 100.0f;

    // Labor intensity represented in total needed hammer "swings" or ability charge
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseLaborCost = 10.0f;

    // Must be equipped in hand to construct the ghost
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag RequiredToolTag; 

    // Used for gating access to building this piece via dynamic logic
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PrerequisiteConditionTree = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OverlapConnectionRadius = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FBuildingSocketDef> ConnectionSockets;

    // Sequential materials required to build the ghost
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FStructureMaterialRequirement> RequiredMaterials;
};

USTRUCT(BlueprintType)
struct FPrefabComponentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PieceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTransform RelativeTransform;
};

USTRUCT(BlueprintType)
struct FDynamicStructurePrefabDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PrefabTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPrefabComponentDef> Components;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PrerequisiteConditionTree = NAME_None;
};

// ============================================================================
// FAST ARRAY INSTANCING (UPDATED)
// ============================================================================

USTRUCT(BlueprintType)
struct FStructureInstanceItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid InstanceID;

    UPROPERTY()
    FGameplayTag PieceTag;

    UPROPERTY()
    FGameplayTag MaterialTag;

    UPROPERTY()
    FTransform WorldTransform;

    UPROPERTY()
    float CurrentHP = 100.0f;

    UPROPERTY()
    float SupportValue = 0.0f;

    UPROPERTY()
    bool bIsGrounded = false;

    // Ghost System tracking
    UPROPERTY()
    bool bIsGhost = true;

    UPROPERTY()
    float ConstructionProgress = 0.0f;

    // Bitmask tracking which required materials have been provided (supports up to 32 discrete components per piece)
    UPROPERTY()
    uint32 ProvidedMaterialsMask = 0;

    void PreReplicatedRemove(const struct FFastStructureInstanceArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastStructureInstanceArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastStructureInstanceArray& InArrayOwner);
};

// ============================================================================
// WORLD PERSISTENCE & VOXEL SAVING
// ============================================================================
USTRUCT(BlueprintType)
struct FChunkDeltaSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    UPROPERTY()
    TArray<FVoxelDeltaItem> SavedDeltas;
};

USTRUCT(BlueprintType)
struct FBuildingChunkSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FIntPoint ChunkCoord;
    UPROPERTY() TArray<FStructureInstanceItem> SavedStructures;
};

USTRUCT(BlueprintType)
struct FWorldPersistenceSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    double WorldTime = 0.0;

    UPROPERTY()
    TArray<FChunkDeltaSnapshot> ModifiedVoxelChunks;
    
    UPROPERTY() TArray<FBuildingChunkSnapshot> ModifiedBuildingChunks;
};

USTRUCT(BlueprintType)
struct FFastStructureInstanceArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FStructureInstanceItem> Items;

    UPROPERTY(NotReplicated)
    class ADynamicBuildingChunkActor* OwningChunk = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FStructureInstanceItem, FFastStructureInstanceArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastStructureInstanceArray> : public TStructOpsTypeTraitsBase2<FFastStructureInstanceArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// COMBO WINDOWS & ABILITY DEFINITIONS
// ============================================================================

USTRUCT(BlueprintType)
struct FComboWindowRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    EValidationPhase ActivePhase = EValidationPhase::Charging;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float WindowStartTimeOrAlpha = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float WindowEndTimeOrAlpha = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    bool bUseNormalizedAlpha = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTag AllowedComboAbilityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FName OptionalConditionTreeName = NAME_None;
};

USTRUCT(BlueprintType)
struct FComboGateRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    bool bRequiresComboTransitionToCast = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTagContainer ValidPrerequisiteAbilityTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTag BypassGateMasteryAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float BypassGateMasteryThreshold = 50.0f;
};

USTRUCT(BlueprintType)
struct FDynamicAbilityLifeCycleRules
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag AttemptCostConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag AttemptCooldownConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag ChargingCostConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    bool bApplyChargeCostSmoothly = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag ReleaseCostConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag ReleaseCooldownConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float BaseResourceCost = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float BaseCooldownDuration = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float RecoveryWindowDuration = 0.35f;
};

USTRUCT(BlueprintType)
struct FDynamicChargeProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float MaxChargeTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    bool bHoldAtMaxCharge = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float CostScalingMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float CooldownScalingMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct FDynamicAbilityDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Progression")
    FGameplayTag RequiredMasteryAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Progression")
    FGameplayTag RequiredMasteryPointsAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    EAbilityExecutionType ExecutionType = EAbilityExecutionType::Instant;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicTagFloatMapWrapper NumericParams;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicAbilityLifeCycleRules LifeCycleRequirements;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicChargeProfile ChargeProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicSpatialDeliveryDef SpatialDelivery;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicInteractionProfile InteractionConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicChainProfile ChainConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicMovementProfile MovementConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicAbilityStageVisuals StageVisuals;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TArray<FSourcePassBackRule> SourcePassBackRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    TArray<FComboWindowRule> ComboWindows;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FComboGateRule ComboGate;
};

// ============================================================================
// ABILITY SCORING
// ============================================================================

USTRUCT(BlueprintType)
struct FAbilityPropertyScoreRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FName PropertyName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FGameplayTag MetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    EAbilityScoreScalingMode ScalingMode = EAbilityScoreScalingMode::DirectProportional;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float ReferenceBaseline = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float WeightScalar = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float Exponent = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float MinClampedValue = 0.05f;
};

USTRUCT(BlueprintType)
struct FAbilityScoreDifficultyProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FGameplayTag DifficultyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FString DifficultyDisplayName = TEXT("Normal");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float GlobalScoreMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float LearnPointCostPerScoreUnit = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float MinMasteryRequiredPerScoreUnit = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float MinScoreForLevel1 = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float MaxScoreForLevel10 = 500.0f;
};

USTRUCT(BlueprintType)
struct FAbilityScoreConfigData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    int32 Version = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FGameplayTag ActiveDifficultyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FDynamicTagFloatMapWrapper ExecutionTypeWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    TArray<FAbilityPropertyScoreRule> PropertyRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    TArray<FAbilityScoreDifficultyProfile> DifficultyProfiles;
};

USTRUCT(BlueprintType)
struct FComputedAbilityScoreInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    FGameplayTag AbilityTag;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float RawAbilityScore = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float DifficultyAdjustedScore = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    int32 ComputedSpellLevel = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float LearnPointCost = 1.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float MinimumMasteryRequired = 1.0f;
};

// ============================================================================
// MILESTONES & THRESHOLDS
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicMilestoneAction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    FGameplayTag ExecutionTriggerConditionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    int32 TriggerLevelOrInterval = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    bool bIsIntervalRepeating = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    bool bUndoOnLevelDown = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    TArray<FDynamicModifierDef> ModifiersToApply;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    TArray<FGameplayTag> EffectTagsToApply;
};

USTRUCT(BlueprintType)
struct FDynamicMilestoneContainer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Milestones")
    TArray<FDynamicMilestoneAction> MilestoneActions;
};

USTRUCT(BlueprintType)
struct FDynamicThresholdRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag MonitoredAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag MaxThresholdAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag TargetEvolvedAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag BroadcastEventTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    EThresholdTriggerType TriggerBehavior = EThresholdTriggerType::LevelUpAttribute;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag GainModifierAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag LossModifierAttributeTag;
};

USTRUCT(BlueprintType)
struct FDynamicThresholdContainer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Thresholds")
    TArray<FDynamicThresholdRule> Rules;
};

// ============================================================================
// ENVIRONMENT, BIOMES, & WEATHER
// ============================================================================

USTRUCT(BlueprintType)
struct FEvolutionStageKeyframe
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float NormalizedProgress = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag StageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float ScaleMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FTagFloatPair> StageAttributeModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FGameplayTag> TagsToApply;
};

USTRUCT(BlueprintType)
struct FBiomeShiftRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag TargetBiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FName ShiftConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float RequiredSustainedGameHours = 48.0f;
};

USTRUCT(BlueprintType)
struct FDynamicBiomeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag BiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FDynamicTagStringMapWrapper Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FTagFloatPair> DefaultElementalEquilibrium;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float HomeostasisPullRate = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FBiomeShiftRule> BiomeShiftRules;
};

USTRUCT(BlueprintType)
struct FDynamicWeatherPatternDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    FGameplayTag WeatherTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    FName SpawnConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FTagFloatPair> ElementalTransferRates;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FTagFloatPair> EnvironmentMetricOffsets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FGameplayTag> ExposedEffectsToApply;
};

USTRUCT(BlueprintType)
struct FEnvironmentalRateModifier
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag EnvironmentMetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MinThreshold = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MaxThreshold = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MinRateMultiplier = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MaxRateMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    bool bPauseOutsideRange = false;
};

USTRUCT(BlueprintType)
struct FDynamicEnvironmentPhysicalMetricFormula
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag MetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float BaseConstant = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FDynamicTagFloatMapWrapper ElementWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float MinClamp = -100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float MaxClamp = 100.0f;
};

// ============================================================================
// DYNAMIC ENVIRONMENT SYSTEM CONFIG (BOUNDARY & WRAPPING SETTINGS)
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicEnvironmentSystemConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FGameplayTag> TrackedElementalTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float MaxElementDensityCap = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float DefaultCellDensity = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FDynamicEnvironmentPhysicalMetricFormula> MetricFormulas;

    // Continuous/spherical world wrapping: enables toroidal wrap on advection
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    bool bWrapGridBoundaries = false;

    // Dissipation factor when crossing non-wrapped borders (prevents unnatural edge accumulation)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float BorderDissipationRate = 0.15f;
};

// ============================================================================
// SPATIALIZED ENVIRONMENT CHUNK GRID ITEM & FAST ARRAY
// ============================================================================
USTRUCT(BlueprintType)
struct FEnvironmentGridCellItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 CellIndex = 0; // Global cell index in the world grid

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag CurrentBiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag TargetShiftBiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float BiomeBlendAlpha = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag ActiveWeatherTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<uint8> PackedElementalDensities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FVector_NetQuantizeNormal WindDirection = FVector_NetQuantizeNormal(1.0f, 0.0f, 0.0f);

    UPROPERTY(NotReplicated, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    FDynamicTagFloatMapWrapper DerivedPhysicalMetrics;

    UPROPERTY(NotReplicated)
    float SustainedOutOfRangeHours = 0.0f;

    // DECLARATIONS ONLY: Implementations must remain in .cpp
    void PreReplicatedRemove(const struct FFastEnvironmentGridArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastEnvironmentGridArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastEnvironmentGridArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastEnvironmentGridArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FEnvironmentGridCellItem> Items;

    UPROPERTY(NotReplicated)
    ADynamicEnvironmentChunkActor* OwningChunkActor = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FEnvironmentGridCellItem, FFastEnvironmentGridArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastEnvironmentGridArray> : public TStructOpsTypeTraitsBase2<FFastEnvironmentGridArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FEvolutionProfileDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float TotalLifespanOrMaxXP = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FEvolutionStageKeyframe> ProgressionPhases;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FEnvironmentalRateModifier> EnvironmentalModifiers;
};

// ============================================================================
// DATA-DRIVEN PHYSICS & IMPACT CALCULATION CONFIG
// ============================================================================

// ============================================================================
// DATA-DRIVEN PHYSICS & IMPACT CALCULATION CONFIG
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicImpactConsequence
{
    GENERATED_BODY()

    // The attribute to damage (e.g., Attribute.Resource.Health, Attribute.Limb.Integrity, Attribute.Resource.Stamina)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    FGameplayTag TargetAttributeTag;

    // Multiplier for the raw excess impact force directed to this attribute
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    float DamageScalar = 1.0f;

    // If true, targets the specific LimbId struck. If false, targets the global entity (LimbId = 0).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    bool bIsLimbSpecific = false;

    // If the unmitigated damage to this entry exceeds this threshold, the StatusEffects are applied.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    float StatusEffectThreshold = 50.0f;

    // Effects to apply if the threshold is broken (e.g., State.Debuff.BrokenLeg, State.Debuff.Concussion)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    TArray<FGameplayTag> StatusEffectsToApply;
};

USTRUCT(BlueprintType)
struct FDynamicImpactReactionRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    float BaseImpactVelocityThreshold = 750.0f;

    // Threshold at which the character breaks stance, loses grip, or falls over
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    float BaseStabilityLossThreshold = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    float MassImpactMultiplier = 0.015f;

    // Attributes reducing effective impact velocity (e.g., Attribute.Skill.Acrobatics, Attribute.Stat.Agility)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    FDynamicTagFloatMapWrapper VelocityMitigationScalars;

    // Attributes reducing final damage (e.g., Attribute.Stat.Toughness, Attribute.Limb.Armor)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    FDynamicTagFloatMapWrapper DamageMitigationScalars;

    // Attributes increasing grip/stability retention (e.g., Attribute.Stat.Strength, Attribute.Stat.Balance)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    FDynamicTagFloatMapWrapper StabilityRetentionScalars;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    TArray<FDynamicImpactConsequence> Consequences;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    bool bTriggersRagdollOnInstability = true;
};

// ============================================================================
// SKELETAL TOPOLOGY & PHYSICS MOTOR CONFIG
// ============================================================================

USTRUCT(BlueprintType)
struct FPhysicsCalculationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CalculationMode = TEXT("ImpulseMagnitude");

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag PrimaryDamageTargetAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinImpulseThreshold = 350.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxImpulseCap = 25000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ImpulseToDamageScalar = 0.0085f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MassRatioExponent = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinRelativeVelocityThreshold = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float VelocityToDamageScalar = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CooldownBetweenHitsPerLimb = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Impact")
    TMap<FGameplayTag, FDynamicImpactReactionRule> ImpactReactionRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Traversal")
    float BaseHoldSweepRadius = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Traversal")
    float BaseHoldSweepDistance = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Traversal")
    float VelocityLookaheadScalar = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Traversal")
    FDynamicTagFloatMapWrapper SweepRadiusAttributeScalars;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Physics|Traversal")
    FDynamicTagFloatMapWrapper SweepDistanceAttributeScalars;
};

USTRUCT(BlueprintType)
struct FPhysicsJointNodeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FName BoneName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    uint8 JointChainIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float MotorDriveWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float LocalBoneDamageMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct FPhysicsLimbConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    uint8 LimbId = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    uint8 ParentLimbId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag LimbTypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag LimbSlotTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float DamageMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float ImpulseMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float StumbleThreshold = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    float BreakRagdollThreshold = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag IntegrityAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag MaxIntegrityAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag EnduranceAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FGameplayTag MaxEnduranceAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    TArray<FPhysicsJointNodeConfig> JointChain;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FDynamicTagFloatMapWrapper DefaultLimbAttributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FDynamicTagFloatMapWrapper AngularMotorAttributeScalars;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Skeletal")
    FDynamicTagFloatMapWrapper LinearMotorAttributeScalars;
};

USTRUCT(BlueprintType)
struct FPhysicsDamageConfigData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Version = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ConfigName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPhysicsCalculationSettings CalculationSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPhysicsLimbConfig> Limbs;
};

// ============================================================================
// EQUIPMENT & NON-CHARACTER ACTORS
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicEquipmentRequirementDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TArray<FGameplayTag> RequiredEquipmentSlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    float FittedSizeMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    float FittedSizeMax = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    bool bUseLeaderPoseComponent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TSoftObjectPtr<UAnimMontage> EquipAnimMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TSoftObjectPtr<UAnimMontage> UnequipAnimMontage;
};

USTRUCT(BlueprintType)
struct FDynamicPhysicsActorDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    TSoftObjectPtr<USkeletalMesh> SkeletalMeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    TSoftObjectPtr<UStaticMesh> StaticMeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    FName CollisionProfileName = TEXT("PhysicsActor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    bool bSimulatePhysicsOnSpawn = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    bool bUsePhysicsControl = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    float MassOverride = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    FDynamicTagFloatMapWrapper DefaultGlobalAttributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    TArray<FPhysicsLimbConfig> CustomLimbs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    FGameplayTagContainer InitialInteractableStateTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    TArray<FInteractionTargetPoint> KinematicTargetPoints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|PhysicsActor")
    TArray<FGameplayTag> GrantedAbilities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Item")
    FDynamicItemDef ItemConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    TArray<FDynamicContainerDef> Containers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FDynamicRecipeDef> CraftingRecipes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FDynamicEquipmentRequirementDef EquipmentRequirement;
};

USTRUCT(BlueprintType)
struct FDynamicEquipmentSlotItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag SlotTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    uint8 LinkedLimbId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag EquippedItemArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TArray<FDynamicAttributeItem> SavedItemAttributeState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTagContainer SavedItemInteractableStates;

    void PreReplicatedRemove(const struct FFastEquipmentSlotArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastEquipmentSlotArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastEquipmentSlotArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastEquipmentSlotArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicEquipmentSlotItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicEquipmentComponent* OwningEquipmentComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicEquipmentSlotItem, FFastEquipmentSlotArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastEquipmentSlotArray> : public TStructOpsTypeTraitsBase2<FFastEquipmentSlotArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicEquipmentSlotProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag SlotTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    uint8 LimbId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag LayerTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTagContainer AllowedItemTags;
};

// ============================================================================
// CUSTOMIZATION, GENETICS, & AGING
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicColorGradient
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Customization")
    FLinearColor PrimaryColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Customization")
    FLinearColor SecondaryColor = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FCharacterAttributeRangeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag AttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float MinValue = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float MaxValue = 100.0f;
};

USTRUCT(BlueprintType)
struct FCharacterWeightedAttributeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag AttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float SelectionChance = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float AssignedValue = 1.0f;
};

USTRUCT(BlueprintType)
struct FRacialSliderClampOverride
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Clamps")
    FGameplayTag SliderTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Clamps")
    float ClampedMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Clamps")
    float ClampedMax = 1.0f;
};

USTRUCT(BlueprintType)
struct FGenderAttributeModifier
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    FGameplayTag TargetAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    EEffectModifierOp ModifierOp = EEffectModifierOp::Multiplicative;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    float Magnitude = 0.0f;
};

USTRUCT(BlueprintType)
struct FCharacterGenderProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    ECharacterGender Gender = ECharacterGender::Neutral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FGenderAttributeModifier> StatModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FRacialSliderClampOverride> GenderSliderClamps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FGameplayTag> CulturalHairArchetypes;
};

USTRUCT(BlueprintType)
struct FCharacterProfessionRollDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag ProfessionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float ChanceWeight = 1.0f;
};

USTRUCT(BlueprintType)
struct FCharacterDefaultPlaceBindingDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag PlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag DefaultLandmarkTag;
};

USTRUCT(BlueprintType)
struct FDynamicCalendarDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    float TimeScalar = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 HoursPerDay = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 DaysPerSeason = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 SeasonsPerYear = 4;
};

USTRUCT(BlueprintType)
struct FDynamicAlleleDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag AlleleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    float DominanceWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FDynamicColorGradient PhenotypeColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FGameplayTag> PhenotypeAbilities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FGenderAttributeModifier> PhenotypeModifiers;
};

USTRUCT(BlueprintType)
struct FDynamicGeneLocusDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag LocusTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    float MutationChance = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FDynamicAlleleDef> PossibleAlleles;
};

// ============================================================================
// GENOME FAST ARRAY (BANDWIDTH OPTIMIZED: REPLICATE EXPRESSED ONLY)
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicGenePairItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag LocusTag;

    // Server-Only / Backend persistence: Recessive lineage is never sent over the wire
    UPROPERTY(NotReplicated, EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag MaternalAllele;

    // Server-Only / Backend persistence: Recessive lineage is never sent over the wire
    UPROPERTY(NotReplicated, EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag PaternalAllele;

    // Replicated to client proxies strictly to drive visual shaders, morphs, and abilities
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag ExpressedAllele;

    void PreReplicatedRemove(const struct FFastGenomeArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastGenomeArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastGenomeArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastGenomeArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicGenePairItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicLifeComponent* OwningLifeComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicGenePairItem, FFastGenomeArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastGenomeArray> : public TStructOpsTypeTraitsBase2<FFastGenomeArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FBoneScaleConstraint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    FName BoneName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MinScaleMultiplier = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MaxScaleMultiplier = 1.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    bool bAllowLengthAdjustment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MinLengthOffset = -10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MaxLengthOffset = 10.0f;
};

USTRUCT(BlueprintType)
struct FBodyslideSliderDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    FGameplayTag SliderTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float DefaultValue = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float MinLimit = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float MaxLimit = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    TArray<FBoneScaleConstraint> TargetBones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    TArray<FName> TargetMorphTargets;
};

USTRUCT(BlueprintType)
struct FDynamicSkeletonDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Skeleton")
    FGameplayTag SkeletonTypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Skeleton")
    FGameplayTag ParentSkeletonTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Skeleton")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Skeleton")
    TArray<FBodyslideSliderDef> BodyslideSliders;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Skeleton")
    TArray<FPhysicsLimbConfig> Limbs;
};

USTRUCT(BlueprintType)
struct FDynamicPersonalityProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FGameplayTagContainer PersonalityTraitTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FDynamicTagFloatMapWrapper FoodAndDrinkPreferences;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FDynamicTagFloatMapWrapper ActivityPreferences;
};

USTRUCT(BlueprintType)
struct FContinuousGrowthKeyframe
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float NormalizedAge = 0.0f; // [0.0 to 1.0]

    // Explicit life stage tag for this age bracket (e.g., Age.Stage.Child, Age.Stage.Adult)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    FGameplayTag LifeStageTag; 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float ScaleMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float PhysicsMotorMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    FGameplayTag LocomotionAnimTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FTagFloatPair> MorphTargetOffsets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FTagFloatPair> StatModifiers;
};

USTRUCT(BlueprintType)
struct FAgingProfileDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float ExpectedLifespanYears = 80.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FContinuousGrowthKeyframe> GrowthPhases;
};

USTRUCT(BlueprintType)
struct FCharacterTemplateDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag TemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag ParentTemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TSoftObjectPtr<USkeletalMesh> SkeletalMeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TSoftClassPtr<UAnimInstance> AnimationControllerClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag SkeletonTypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag DefaultAIAgentTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FDynamicColorGradient> AllowedSkinTones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FDynamicColorGradient> AllowedHairColors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FCharacterAttributeRangeDef> AttributeRanges;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FCharacterWeightedAttributeDef> WeightedAttributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FCharacterProfessionRollDef> ProfessionRolls;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FDynamicPersonalityProfile PersonalityProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FCharacterDefaultPlaceBindingDef> DefaultPlaceBindings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FCharacterGenderProfile> GenderVariants;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FRacialSliderClampOverride> RacialSliderClamps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FTagFloatPair> DefaultBodyslideSliders;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FGameplayTag> StartupAbilities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    TArray<FGameplayTag> StartupEquipmentArchetypes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FAgingProfileDef AgingProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FDynamicTagFloatMapWrapper AlleleDominanceOverrides;
};

USTRUCT(BlueprintType)
struct FDynamicCharacterMorphItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Customization")
    FGameplayTag SliderTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Customization")
    float SliderValue = 0.5f;

    void PreReplicatedRemove(const struct FFastCharacterMorphArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastCharacterMorphArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastCharacterMorphArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastCharacterMorphArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicCharacterMorphItem> Items;

    UPROPERTY(NotReplicated)
    class UDynamicCustomizationComponent* OwningCustomizationComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicCharacterMorphItem, FFastCharacterMorphArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastCharacterMorphArray> : public TStructOpsTypeTraitsBase2<FFastCharacterMorphArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// DATA-DRIVEN RELATIONSHIP DEFINITIONS & METRIC RULES
// ============================================================================
USTRUCT(BlueprintType)
struct FRelationshipMetricRule
{
    GENERATED_BODY()

    // e.g. Relationship.Metric.Affinity, Relationship.Metric.Attraction, Relationship.Metric.Trust
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag MetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DefaultValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float MinValue = -100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float MaxValue = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    ERelationshipDecayCurve DecayCurve = ERelationshipDecayCurve::Exponential;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DecayRatePerDay = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DecayTargetFloor = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float VolatilityScalar = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    bool bClampToMinMax = true;
};

USTRUCT(BlueprintType)
struct FDynamicRelationshipTypeDef
{
    GENERATED_BODY()

    // e.g. Relationship.Type.Friendship, Relationship.Type.Romantic, Relationship.Type.Rivalry
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag RelationshipTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FDynamicLocalizationProfile Localization;

    // Fully dynamic metric rules defined strictly in JSON
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    TArray<FRelationshipMetricRule> SupportedMetrics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    bool bPruneWhenAllAtFloor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float PruneTolerance = 0.05f;

    const FRelationshipMetricRule* FindMetricRule(FGameplayTag MetricTag) const
    {
        for (const FRelationshipMetricRule& Rule : SupportedMetrics)
        {
            if (Rule.MetricTag == MetricTag)
            {
                return &Rule;
            }
        }
        return nullptr;
    }
};

// ============================================================================
// DATA-DRIVEN RELATIONSHIP FAST ARRAY EDGE
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicRelationshipMetricEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag MetricTag;

    // 1-byte quantized value [0..255] mapped according to [MinValue, MaxValue] in FRelationshipMetricRule
    UPROPERTY()
    uint8 PackedValue = 128;

    // Exact unquantized value for authoritative calculations
    UPROPERTY(NotReplicated)
    float ExactValue = 0.0f;
};

// ============================================================================
// SOCIAL GRAPH, MEMORY, & NEEDS (QUANTIZED BYTES)
// ============================================================================
USTRUCT(BlueprintType)
struct FDynamicRelationshipEdgeItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGuid TargetEntityGuid;

    UPROPERTY(NotReplicated)
    TWeakObjectPtr<AActor> CachedTargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    ERelationshipTargetKind TargetKind = ERelationshipTargetKind::Character;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag PrimaryRelationshipTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTagContainer SecondaryContextTags;

    // Dynamic metrics collection driven entirely by the JSON definition schema
    UPROPERTY()
    TArray<FDynamicRelationshipMetricEntry> Metrics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    double LastInteractionWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastRelationshipEdgeArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastRelationshipEdgeArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastRelationshipEdgeArray& InArrayOwner);

    float GetMetricValue(FGameplayTag InTag) const
    {
        for (const FDynamicRelationshipMetricEntry& Entry : Metrics)
        {
            if (Entry.MetricTag == InTag) return Entry.ExactValue;
        }
        return 0.0f;
    }

    void SetMetricValue(FGameplayTag InTag, float InExactVal, uint8 InPackedVal)
    {
        for (FDynamicRelationshipMetricEntry& Entry : Metrics)
        {
            if (Entry.MetricTag == InTag)
            {
                Entry.ExactValue = InExactVal;
                Entry.PackedValue = InPackedVal;
                return;
            }
        }
        FDynamicRelationshipMetricEntry NewEntry;
        NewEntry.MetricTag = InTag;
        NewEntry.ExactValue = InExactVal;
        NewEntry.PackedValue = InPackedVal;
        Metrics.Add(NewEntry);
    }
};

USTRUCT(BlueprintType)
struct FFastRelationshipEdgeArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicRelationshipEdgeItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicRelationshipEdgeItem, FFastRelationshipEdgeArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastRelationshipEdgeArray> : public TStructOpsTypeTraitsBase2<FFastRelationshipEdgeArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// SOCIAL FAST ARRAYS (RELATIONSHIPS, NEEDS, KNOWN PLACES)
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicRelationshipItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGuid TargetPersistentGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag RelationshipTypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FDynamicTagFloatMapWrapper Metrics;

    void PreReplicatedRemove(const struct FDynamicRelationshipFastArray& InArrayOwner);
    void PostReplicatedAdd(const struct FDynamicRelationshipFastArray& InArrayOwner);
    void PostReplicatedChange(const struct FDynamicRelationshipFastArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FDynamicRelationshipFastArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicRelationshipItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicRelationshipItem, FDynamicRelationshipFastArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FDynamicRelationshipFastArray> : public TStructOpsTypeTraitsBase2<FDynamicRelationshipFastArray>
{
    enum { WithNetDeltaSerializer = true };
};


USTRUCT(BlueprintType)
struct FDynamicNeedAndMoodItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag NeedOrMoodTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float CurrentValue = 0.0f;

    void PreReplicatedRemove(const struct FDynamicNeedAndMoodFastArray& InArrayOwner);
    void PostReplicatedAdd(const struct FDynamicNeedAndMoodFastArray& InArrayOwner);
    void PostReplicatedChange(const struct FDynamicNeedAndMoodFastArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FDynamicNeedAndMoodFastArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicNeedAndMoodItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicNeedAndMoodItem, FDynamicNeedAndMoodFastArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FDynamicNeedAndMoodFastArray> : public TStructOpsTypeTraitsBase2<FDynamicNeedAndMoodFastArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicKnownPlaceItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGameplayTag PlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGameplayTag PlaceArchetypeOrLandmarkTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGuid LinkedActorGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FVector_NetQuantize10 LastKnownLocation = FVector_NetQuantize10::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    float ConfidenceScore = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    double LastVisitedWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastKnownPlaceArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastKnownPlaceArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastKnownPlaceArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastKnownPlaceArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicKnownPlaceItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicKnownPlaceItem, FFastKnownPlaceArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastKnownPlaceArray> : public TStructOpsTypeTraitsBase2<FFastKnownPlaceArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicScheduleBlockDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float StartHour24 = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float EndHour24 = 17.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    FGameplayTag ScheduledActivityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    FGameplayTag PreferredPlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float ScheduleAdherenceWeight = 1.0f;
};

USTRUCT(BlueprintType)
struct FDynamicTaskPriorityRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag TaskTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float BasePriorityWeight = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag LinkedAbilityOrRecipeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTagContainer RequiredItemTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTagContainer ProducedOutputTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FName TaskConditionTreeName = NAME_None;
};

USTRUCT(BlueprintType)
struct FDynamicStatPriorityRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag AttributeOrNeedTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float CriticalThresholdRatio = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float EmergencyPriorityWeight = 500.0f;
};

USTRUCT(BlueprintType)
struct FDynamicProfessionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag ProfessionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicTaskPriorityRule> ExpectedTasks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FDynamicTagFloatMapWrapper PrioritizedItemWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicStatPriorityRule> StatPriorities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicScheduleBlockDef> DefaultDailySchedule;
};

USTRUCT(BlueprintType)
struct FDynamicAssignedProfessionItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag ProfessionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float ProficiencyLevel = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float ProfessionPriorityWeight = 1.0f;

    void PreReplicatedRemove(const struct FFastAssignedProfessionArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastAssignedProfessionArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastAssignedProfessionArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastAssignedProfessionArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicAssignedProfessionItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicAssignedProfessionItem, FFastAssignedProfessionArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastAssignedProfessionArray> : public TStructOpsTypeTraitsBase2<FFastAssignedProfessionArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicNeedStateItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Needs")
    FGameplayTag NeedOrMoodTag;

    // Quantized 1-byte representation [0..100 -> 0..255]
    UPROPERTY()
    uint8 PackedCurrentValue = 255;

    UPROPERTY(NotReplicated)
    float LocalExactValue = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Needs")
    float ShiftRatePerHour = -2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Needs")
    double LastEvaluatedWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastNeedStateArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastNeedStateArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastNeedStateArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastNeedStateArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicNeedStateItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicSocialComponent* OwningSocialComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicNeedStateItem, FFastNeedStateArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastNeedStateArray> : public TStructOpsTypeTraitsBase2<FFastNeedStateArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ============================================================================
// AI AGENTS & DYNAMICS
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicAIAbilityRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag AbilityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FName RequiredConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float PriorityWeight = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float MinRange = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float MaxRange = 1500.0f;
};

USTRUCT(BlueprintType)
struct FDynamicAIGroupDynamics
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag GroupArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bFormsPacks = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    int32 MaxGroupSize = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float CohesionRadius = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bDefendsPackMembers = true;
};

USTRUCT(BlueprintType)
struct FDynamicAIAgentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag AIAgentTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TSoftClassPtr<AAIController> AIControllerClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FDynamicAIGroupDynamics GroupDynamics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTagContainer DietaryTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bHuntsForFood = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TArray<FDynamicAIAbilityRule> AbilityUsageRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TArray<FDynamicTaskPriorityRule> IdleTasks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float FleeHealthThreshold = 0.15f;
};

// ============================================================================
// CONVERSIONS, TRANSFORMATIONS & EVOLUTIONS
// ============================================================================

USTRUCT(BlueprintType)
struct FDynamicTransformationRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTag TransformationTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer InheritedCategoryTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer AllowedBaseTemplateTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTag OverrideAIAgentTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    bool bApplyColorTint = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FDynamicColorGradient TransformationColorTint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    TArray<FGenderAttributeModifier> TransformationStatModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    TArray<FGameplayTag> GrantedAbilities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bInvertsHealingToDamage = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bCanSustainViaBloodDrain = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bSiringDrainsMaxHP = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    float SiringMaxHPPenaltyAmount = 15.0f;
};

USTRUCT(BlueprintType)
struct FDynamicEvolutionBranch
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag EvolutionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag TargetCharacterTemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FName PrerequisiteConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag RequiredAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float RequiredAttributeMinimumValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FDynamicModifierDef> EvolutionStatModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag NewAIAgentTag;
};

USTRUCT(BlueprintType)
struct FDynamicEvolutionContainer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FDynamicEvolutionBranch> EvolutionBranches;
};

// ============================================================================
// QUEST SYSTEM
// ============================================================================

USTRUCT(BlueprintType)
struct FQuestRewardConsequence
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float CurrencyReward = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FRecipeIngredient> ItemRewards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FGenderAttributeModifier> AttributeModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float AffinityDelta = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float TrustDelta = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float DebtDelta = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer GrantedTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer RemovedTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FGameplayTag> GrantedAbilities;
};

USTRUCT(BlueprintType)
struct FQuestObjectiveDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag ObjectiveTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    EQuestObjectiveType ObjectiveType = EQuestObjectiveType::FetchAndDeliver;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag TargetEntityOrArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag TargetLocationOrLandmarkTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    int32 RequiredCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FName ValidationConditionTreeName = NAME_None;
};

USTRUCT(BlueprintType)
struct FQuestStepBranchDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag BranchTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag NextStepTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    bool bConcludesQuest = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    bool bMarkedAsSuccess = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FName BranchConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer RequiredTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer ExcludedTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FQuestRewardConsequence BranchConsequences;
};

USTRUCT(BlueprintType)
struct FQuestStepDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag StepTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FQuestObjectiveDef> Objectives;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    bool bAutoAdvanceOnObjectivesCompleted = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FQuestStepBranchDef> StepBranches;
};

USTRUCT(BlueprintType)
struct FQuestDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag QuestTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    EQuestCategory Category = EQuestCategory::Story;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FName PrerequisiteConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer RequiredPlayerTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer ExcludedPlayerTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float MinAffinityRequired = -0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float MinTrustRequired = -0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag InitialStepTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FQuestStepDef> Steps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FQuestRewardConsequence FinalRewards;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    TArray<FTagFloatPair> MinimumRelationshipMetricRequirements;
};

USTRUCT(BlueprintType)
struct FDynamicQuestGeneratorRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag RuleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag ContextProfessionOrFactionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FName TriggerConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag QuestTemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    float UrgencyMultiplier = 1.0f;
};

// Normalized, independently replicated quest objective struct (O(1) bandwidth)
USTRUCT(BlueprintType)
struct FDynamicActiveQuestObjectiveItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGuid QuestInstanceGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag ObjectiveTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    int32 CurrentProgress = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    int32 RequiredCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    bool bIsComplete = false;

    void PreReplicatedRemove(const struct FFastActiveQuestObjectiveArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastActiveQuestObjectiveArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastActiveQuestObjectiveArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastActiveQuestObjectiveArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicActiveQuestObjectiveItem> Items;

    UPROPERTY(NotReplicated)
    class UDynamicQuestComponent* OwningQuestComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicActiveQuestObjectiveItem, FFastActiveQuestObjectiveArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastActiveQuestObjectiveArray> : public TStructOpsTypeTraitsBase2<FFastActiveQuestObjectiveArray>
{
    enum { WithNetDeltaSerializer = true };
};

// High-level quest instance item (contains NO nested objective arrays)
USTRUCT(BlueprintType)
struct FDynamicActiveQuestItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGuid QuestInstanceGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag QuestDefTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGuid QuestGiverGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTag CurrentStepTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    EQuestStepState StepState = EQuestStepState::Active;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer QuestContextTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Quest")
    double StepStartWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastActiveQuestArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastActiveQuestArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastActiveQuestArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastActiveQuestArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicActiveQuestItem> Items;

    UPROPERTY(NotReplicated)
    class UDynamicQuestComponent* OwningQuestComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicActiveQuestItem, FFastActiveQuestArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastActiveQuestArray> : public TStructOpsTypeTraitsBase2<FFastActiveQuestArray>
{
    enum { WithNetDeltaSerializer = true };
};

// --- FAST KNOWN WORD ARRAY ---
USTRUCT(BlueprintType)
struct FDynamicKnownWordItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FGameplayTag LanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FString UnlockedNativeWord = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    int32 TimesEncountered = 1;

    void PreReplicatedRemove(const struct FFastKnownWordArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastKnownWordArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastKnownWordArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastKnownWordArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FDynamicKnownWordItem> Items;

    UPROPERTY(NotReplicated)
    UDynamicLanguageComponent* OwningComp = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FDynamicKnownWordItem, FFastKnownWordArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastKnownWordArray> : public TStructOpsTypeTraitsBase2<FFastKnownWordArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDraggedPhysicsActorSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGuid ActorPersistentGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGameplayTag ActorArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FTransform WorldTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FVector LinearVelocity = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FVector AngularVelocity = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicAttributeItem> SavedAttributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGameplayTagContainer SavedInteractableStates;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicContainerSlotItem> ContainerInventory;
};

USTRUCT(BlueprintType)
struct FPlayerPersistenceSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGuid CharacterGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGameplayTag CharacterTemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    ECharacterGender Gender = ECharacterGender::Neutral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FDynamicColorGradient SkinColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FDynamicColorGradient HairColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    FGameplayTag ActiveHairArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FTagFloatPair> MorphSliders;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicAttributeItem> Attributes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicGenePairItem> Genome;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicEquipmentSlotItem> Equipment;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicContainerSlotItem> InventorySlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicActiveQuestItem> ActiveQuests;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicActiveQuestObjectiveItem> ActiveObjectives;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicKnownWordItem> KnownWords;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicRelationshipEdgeItem> Relationships;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicNeedStateItem> Needs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicKnownPlaceItem> KnownPlaces;
    
    // Tethered or dragged world actors traversing the server boundary
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDraggedPhysicsActorSnapshot> DraggedPhysicsActors;
};

USTRUCT(BlueprintType)
struct FNameDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FString GivenName = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    ECharacterGender Gender = ECharacterGender::Neutral;

    // Allowed templates mapped to their selection probability [0.0 to 1.0]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FDynamicTagFloatMapWrapper TemplateProbabilities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FGameplayTagContainer PhonemeTags;
};

USTRUCT(BlueprintType)
struct FSurnameGeneratorRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming|Surnames")
    FGameplayTag CultureOrTemplateTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming|Surnames")
    ENameGenerationMode GenerationMode = ENameGenerationMode::Compound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming|Surnames")
    TArray<FString> PrefixPool;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming|Surnames")
    TArray<FString> SuffixPool;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming|Surnames")
    TArray<FString> ClanAffixes;
};

// --- SURNAMES & NAME LEXICON STRUCTS ---
USTRUCT(BlueprintType)
struct FNameLexiconEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FString Token = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    float GivenNameWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    float SurnameWeight = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    ECharacterGender GenderRestriction = ECharacterGender::Neutral;

    // Maps Character Template Tags -> How strongly this token applies to that race (e.g., Elves = 1.0, Humans = 0.01)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FDynamicTagFloatMapWrapper RacialPrevalenceWeights;
};

USTRUCT(BlueprintType)
struct FNameGenerationRuleConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FGameplayTag CultureOrTemplateTag;

    // Maps Character Template Tags -> Probability weight of this culture table being selected for a given race
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    FDynamicTagFloatMapWrapper CulturalAdoptionWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    TArray<FNameLexiconEntry> StandaloneNames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    TArray<FNameLexiconEntry> PrefixPool;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    TArray<FNameLexiconEntry> SuffixPool;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Naming")
    ENameGenerationMode GenerationMode = ENameGenerationMode::StaticPool;
};

USTRUCT(BlueprintType)
struct FEntityMemberRecord : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Entity")
    FGuid MemberGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Entity")
    FString MemberDisplayName;

    // e.g., Role.Family.Head, Role.Family.Heir, Role.Guild.Master, Role.Member
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Entity")
    FGameplayTag EntityRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Entity")
    FGuid ParentMemberGuid; // Used to structure family trees (Father/Mother GUID)

    void PreReplicatedRemove(const struct FFastEntityMemberArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastEntityMemberArray& InArrayOwner) {}
    void PostReplicatedChange(const struct FFastEntityMemberArray& InArrayOwner) {}
};

USTRUCT(BlueprintType)
struct FFastEntityMemberArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FEntityMemberRecord> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FEntityMemberRecord, FFastEntityMemberArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFastEntityMemberArray> : public TStructOpsTypeTraitsBase2<FFastEntityMemberArray>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct FDynamicVocabularyWordDef
{
    GENERATED_BODY()

    // The written word in its native tongue (e.g. "Mae", "Lok'tar")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FString NativeWord = TEXT("");

    // Minimum language skill required [0.0 to 100.0] to automatically decrypt in subtitles
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    float DifficultyRating = 10.0f;

    // Attribute score progression granted upon successful translation mini-game completion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    float ComprehensionXPGranted = 2.0f;

    // Translations mapped to target localization code (e.g., Localization.Text.en -> "Well")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FDynamicLocalizationProfile LocalizedTranslations;

    // False answers presented during the multiple-choice translation mini-game
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    TArray<FString> MiniGameDecoyOptions;
};

USTRUCT(BlueprintType)
struct FDynamicLanguageDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FGameplayTag LanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FDynamicLocalizationProfile Localization;

    // Maps to character's UDynamicAttributeSet (e.g., Attribute.Language.Elvish)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    FGameplayTag ComprehensionAttributeTag;

    // Syllables used to procedurally construct pseudo-words for unknown vocabularies
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    TArray<FString> PhoneticPhonemes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Language")
    TArray<FDynamicVocabularyWordDef> Vocabulary;
};

// ============================================================================
// LANGUAGE MINI-GAME CHALLENGE (ZERO CLIENT-EXPOSED ANSWERS)
// ============================================================================
USTRUCT(BlueprintType)
struct FTranslationMiniGameChallenge
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Language")
    FGuid ChallengeGuid;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Language")
    FGameplayTag LanguageTag;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Language")
    FString NativeWord = TEXT("");

    // Only randomized candidate options are replicated to the client
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Language")
    TArray<FString> ShuffledOptions;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Language")
    float Difficulty = 10.0f;
};

// Snapshot struct passed safely to async worker threads
struct FEnvironmentCellWorkerInput
{
    int32 CellIndex = 0;
    FGameplayTag CurrentBiomeTag;
    FGameplayTag TargetShiftBiomeTag;
    float BiomeBlendAlpha = 0.0f;
    float SustainedOutOfRangeHours = 0.0f;
    FVector2D WindDirection = FVector2D::ZeroVector;
    TArray<uint8> PackedDensities;
};

// Evaluated output returned from the worker thread pool
struct FEnvironmentCellWorkerOutput
{
    int32 CellIndex = 0;
    FGameplayTag CurrentBiomeTag;
    FGameplayTag TargetShiftBiomeTag;
    float BiomeBlendAlpha = 0.0f;
    float SustainedOutOfRangeHours = 0.0f;
    TArray<uint8> PackedDensities;
    FDynamicTagFloatMapWrapper DerivedMetrics;
    bool bModified = false;
};

USTRUCT(BlueprintType)
struct FCloudAICommandOutput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag CommandActionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag TargetEntityOrPlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FString UtteranceText = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    float PriorityWeight = 100.0f;
};

USTRUCT(BlueprintType)
struct FCloudAIPayloadPackage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGuid RequestGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGuid CharacterGuid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    ECloudAIRequestType RequestType = ECloudAIRequestType::ComplexDecision;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag ActiveArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FString ContextJsonPrompt = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    TArray<FCloudAICommandOutput> ResolvedCommands;
};

// ============================================================================
// DYNAMIC DIALOGUE & UTTERANCE STRUCTS
// ============================================================================

/**
 * Rich, data-driven utterance definition supporting self and target attribute checks,
 * relational metrics, mood gating, and dynamic lottery probability scaling.
 */
USTRUCT(BlueprintType)
struct GDB_API FDynamicDialogueUtteranceDef
{
    GENERATED_BODY()

    // The text template with wildcard tokens (%Target%, %FName%, %Speaker%, %Amt%, etc.)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FString UtteranceText;

    // Base lottery weight relative to other valid candidate utterances (Default: 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    float SelectionWeight = 1.0f;

    // Absolute attribute requirements for the speaker (Health, Stamina, Wealth, etc.)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    TArray<FCharacterAttributeRangeDef> AttributeRequirements;

    // Absolute attribute requirements for the target/listener (e.g. Target Health < 35 to notice wounds)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    TArray<FCharacterAttributeRangeDef> TargetAttributeRequirements;

    // Required relationship tags between speaker and target (e.g. Relation.Social.Spouse)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FGameplayTagContainer RequiredRelationshipTags;

    // Relational metric requirements between speaker and target (e.g. Affinity >= 60)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    TArray<FCharacterAttributeRangeDef> RelationalMetricRequirements;

    // Allowed active moods for the speaker (e.g. Need.Mood.Joyful, Need.Mood.Angry)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FGameplayTagContainer AllowedMoodTags;

    // Required active moods for the target/listener (e.g. Target is Upset, Distressed, or Angry)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FGameplayTagContainer TargetAllowedMoodTags;

    // Dynamic probability bias based on speaker attributes/needs (e.g. Need.Hunger * 1.5)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FDynamicTagFloatMapWrapper ProbabilityAttributeScalars;

    // Dynamic probability bias based on target attributes (e.g. Target Health * -0.4 raises concern when hurt)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FDynamicTagFloatMapWrapper ProbabilityTargetAttributeScalars;

    // Dynamic probability bias based on target moods (e.g. Target Need.Mood.Distressed * 2.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FDynamicTagFloatMapWrapper ProbabilityTargetMoodScalars;

    // Dynamic probability bias based on relationship metrics (e.g. Affinity * 2.5)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FDynamicTagFloatMapWrapper ProbabilityRelationalScalars;

    // Dynamic probability bias based on speaker mood values
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FDynamicTagFloatMapWrapper ProbabilityMoodScalars;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    TSoftObjectPtr<UAnimMontage> GestureMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FGameplayTag VoiceCueTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Dialogue")
    FGameplayTag EmotionTag;
};

// Updated FastPathDialogueEntry using the rich utterance definitions
USTRUCT(BlueprintType)
struct GDB_API FFastPathDialogueEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FGameplayTag CategoryTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FString UtteranceTemplate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    TArray<FDynamicDialogueUtteranceDef> UtteranceVariations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FGameplayTagContainer AllowedMoodTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    TArray<FCharacterAttributeRangeDef> RelationalAttributeRequirements;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FGameplayTagContainer AllowedRelationshipTypeTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    TSoftObjectPtr<UAnimMontage> GestureMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FGameplayTag VoiceCueTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    float SelectionWeight = 1.0f;
};

USTRUCT(BlueprintType)
struct GDB_API FFastPathDialogueDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    FGameplayTag DialogueProfileTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Dialogue")
    TArray<FFastPathDialogueEntry> Entries;
};

// ============================================================================
// DYNAMIC MOVEMENT STATE STRUCTS
// ============================================================================

UENUM(BlueprintType)
enum class EMovementLocomotionType : uint8
{
    Grounded,
    Airborne,
    Aquatic,
    WallAttached
};

USTRUCT(BlueprintType)
struct FMovementStatConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    float BaseValue = 0.0f;

    // Map of Attribute Tag -> Multiplicative Weight (e.g., Attribute.Stat.Agility -> 0.05)
    // Formula evaluated in Tick: BaseValue * FMath::Max(0.01f, 1.0f + Sum(AttributeValue * Weight))
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FDynamicTagFloatMapWrapper AttributeScalars;
};

USTRUCT(BlueprintType)
struct FDynamicMovementStateDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FGameplayTag StateTag;

    // Maps directly to the Mover 2.0 Internal Mode Name (e.g., "Walking", "Swimming", "Flying")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FName MoverModeName = TEXT("Walking");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    EMovementLocomotionType LocomotionType = EMovementLocomotionType::Grounded;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig MaxSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig Acceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig Deceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig GroundFriction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig GravityScale;

    // Defines the physical bounds for stances (88.0 = Standing, 44.0 = Crouching, 22.0 = Crawling)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FMovementStatConfig TargetCapsuleHalfHeight;

    // Prevents entering this state if the character has these tags (e.g., State.Debuff.Crippled blocks Sprint)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FGameplayTagContainer BlockedByTags;

    // Automatically granted to the ASC while in this mode (e.g., State.Moving.Crawl, State.Moving.Fly)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Movement")
    FGameplayTagContainer GrantsTags;
};

// ============================================================================
// ASYNC REGISTRY STAGING DATA (Zero GameThread Lock Contention)
// ============================================================================
USTRUCT(BlueprintType)
struct GDB_API FDynamicAIBehaviorDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FGameplayTag BehaviorTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FGameplayTag ActionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FGameplayTag TargetPlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    ENpcBehaviorExecutionType ExecutionType = ENpcBehaviorExecutionType::Ability;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FGameplayTag AbilityTagToTrigger;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FGameplayTag MovementStateTagToSet;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    TSoftObjectPtr<UAnimMontage> MontageToPlay;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    float BasePriority = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FName PrerequisiteConditionTreeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FDynamicTagFloatMapWrapper PriorityAttributeScalars;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    FDynamicTagFloatMapWrapper ExecutionMetrics;

    // Direct link to the shared Fast-Path Dialogue pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI|Dialogue")
    FGameplayTag DialogueCategoryTag;

    // Optional specific dialogue profile override (falls back to global registry if unset)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI|Dialogue")
    FGameplayTag DialogueProfileTag;

    // Inline custom utterances specific to this behavior (takes precedence over FastPath category)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI|Dialogue")
    TArray<FDynamicDialogueUtteranceDef> Utterances;

    // Legacy fallback array
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI|Dialogue")
    TArray<FString> UtteranceTemplates;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    float CooldownDuration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic AI")
    bool bInterruptible = true;
};

// ============================================================================
// ASYNC REGISTRY STAGING DATA (Zero GameThread Lock Contention)
// ============================================================================
USTRUCT(BlueprintType)
struct FMasterRegistryStagingData
{
    GENERATED_BODY()

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
    TMap<FGameplayTag, FDynamicLanguageDef> LanguageMasterRegistry;
    TMap<FGameplayTag, FNameGenerationRuleConfig> NameGenerationRulesRegistry;
    TMap<FGameplayTag, FDynamicRecipeList> RecipeMasterRegistry;
    TMap<FGameplayTag, FFastPathDialogueDef> FastPathDialogueRegistry;
    TMap<FGameplayTag, FDynamicMovementStateDef> MovementStateRegistry;
    TMap<FGameplayTag, FDynamicNetworkNodeDef> NetworkMasterRegistry;
    TMap<FGameplayTag, FDynamicAIBehaviorDef> AIBehaviorMasterRegistry;
    TMap<FGameplayTag, FDynamicVoxelTypeDef> VoxelMasterRegistry;
    TMap<FGameplayTag, FDynamicStructureMaterialDef> StructureMaterialRegistry;
    TMap<FGameplayTag, FDynamicStructurePieceDef> StructurePieceRegistry;
    TMap<FGameplayTag, FDynamicStructurePrefabDef> StructurePrefabRegistry;
    
    bool bHasVoxelGenerationConfig = false;
    FDynamicVoxelGenerationConfig VoxelGenerationConfig;

    bool bHasCalendarConfig = false;
    FDynamicCalendarDef CalendarConfig;

    bool bHasEnvironmentConfig = false;
    FDynamicEnvironmentSystemConfig EnvironmentConfig;

    bool bHasPhysicsConfig = false;
    FPhysicsDamageConfigData PhysicsConfig;

    bool bHasCalcSettings = false;
    FPhysicsCalculationSettings CalcSettings;

    bool bHasAbilityScoreConfig = false;
    FAbilityScoreConfigData AbilityScoreConfig;
};

USTRUCT()
struct FEnvironmentManaModificationRecord
{
    GENERATED_BODY()

    UPROPERTY()
    int32 CellIndex = INDEX_NONE;

    UPROPERTY()
    FGameplayTag ElementTag;

    UPROPERTY()
    float DeltaAmount = 0.0f;
};

// ============================================================================
// CLOUD AI LATENCY MASKING & TOKEN ECONOMY STRUCTS
// ============================================================================

USTRUCT(BlueprintType)
struct FCloudAIVampingConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Vamping")
    bool bEnableVamping = true;

    // Grace period before playing filler behavior so instant local responses do not glitch
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Vamping")
    float VampingStartDelay = 0.75f;

    // Contextual filler dialogue uttered while awaiting remote responses
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Vamping")
    TArray<FString> FillerUtterances;

    // Thinking / pondering / inspecting montages to loop during latency
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Vamping")
    TArray<TSoftObjectPtr<UAnimMontage>> ThinkingMontages;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Vamping")
    FGameplayTag VampingCueTag;
};

USTRUCT(BlueprintType)
struct FCloudAIContextFilterConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Token Economy")
    float SpatialEntityPerceptionRadius = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Token Economy")
    int32 MaxRelationshipsInPrompt = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Token Economy")
    bool bFilterUrgentNeedsOnly = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Token Economy")
    bool bDeltaOnlySerialization = true;
};

// Miner Node Statistics for Priority & Trust Routing
USTRUCT(BlueprintType)
struct GDB_API FMinerNodeStats
{
    GENERATED_BODY()
    UPROPERTY() float AveragePingMs = 50.0f;
    UPROPERTY() float TrustScore = 1.0f; // Drops on consensus mismatch
    UPROPERTY() int32 TasksCompleted = 0;
    UPROPERTY() bool bIsCurrentlyProcessing = false;
};