Here are the complete, unified, and optimized classes for the Spatial Delivery Pipeline. This replaces the redundant ProjectileConfig, AoeConfig, AuraConfig, and NovaConfig properties and duplicated GameState registries (ProjectileArchetypes and AoeArchetypes) with a single flat FDynamicSpatialDeliveryDef and a pooled ADynamicSpatialActor.   1. Source/GDB/GAS/DynamicGASTypes.hC++// DynamicGASTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimMontage.h"
#include "DynamicGASTypes.generated.h"

class UDynamicAttributeSet;
class ADynamicGameState;
class UDynamicForceComponent;

// --- ENUMS ---
UENUM(BlueprintType)
enum class EAbilityExecutionType : uint8 { Instant, Channeled, Charging, SpatialDelivery, Hitscan, Chain, Movement };

UENUM(BlueprintType)
enum class EMovementPhysicsType : uint8 { LaunchVelocity, RootMotionSource, CustomWallRun };

UENUM(BlueprintType)
enum class EMaxChargeResolution : uint8 { LevelUpCurrent, MutateToOtherAbility, AutoRelease };

UENUM(BlueprintType)
enum class ELogicOperator : uint8 { And, Or, Nand, Nor };

UENUM(BlueprintType)
enum class EConditionType : uint8 { LogicalGateOnly, TagExists, TagValuePair, AttributeValue };

UENUM(BlueprintType)
enum class EComparisonOp : uint8 { LessThan, LessThanOrEqual, Equal, NotEqual, GreaterThanOrEqual, GreaterThan };

UENUM(BlueprintType)
enum class EValidationPhase : uint8 { Attempt, Charging, Release };

UENUM(BlueprintType)
enum class EMultiShotType : uint8 { Single, Repeated, Spread, MultiRepeated };

UENUM(BlueprintType)
enum class ESequenceTriggerTiming : uint8 { TriggerWithEachSelection, TriggerAfterAllSelections };

UENUM(BlueprintType)
enum class EThresholdTriggerType : uint8 { LevelUpAttribute, EvolveAttribute };

UENUM(BlueprintType)
enum class EEffectDurationPolicy : uint8 { Instant, Infinite, HasDuration };

UENUM(BlueprintType)
enum class EEffectModifierOp : uint8 { Additive, Multiplicative, Override };

UENUM(BlueprintType)
enum class EGameplayEffectInhibitionPolicy : uint8 { NeverInhibit, InhibitWithTags };

UENUM(BlueprintType)
enum class EGameplayEffectStackingPolicy : uint8 { None, AggregateBySource, AggregateByTarget };

UENUM(BlueprintType)
enum class EGameplayEffectStackExpirationPolicy : uint8 { ClearEntireStack, RemoveSingleStack, RefreshToZero };

UENUM(BlueprintType)
enum class EGameplayEffectStackDurationRefreshPolicy : uint8 { RefreshDuration, NeverRefresh };

// --- UNIFIED SPATIAL DELIVERY ENUMS ---
UENUM(BlueprintType)
enum class ESpatialDeliveryMotion : uint8
{
    StationaryGround,   // Ground AoE
    AttachedToCaster,   // Aura or Caster-Centered Nova
    StraightVelocity,   // Standard Ballistic / Linear Projectile
    SourceGuided,       // Guided in real-time by Source Character Aim
    Homing              // Seeks locked target actor
};

UENUM(BlueprintType)
enum class ESpatialShapeBehavior : uint8
{
    PointImpact,        // Standard Projectile tip collision
    StayFullSize,       // Constant radius Aura / AoE
    ExpandOut,          // Nova wave or growing Aura (MinRadius -> MaxRadius)
    ContractIn,         // Starts at MaxRadius and shrinks to MinRadius
    ExpandAndContract,  // Ping-pongs between MinRadius and MaxRadius
    PulseAtFullSize     // Sinusoidal pulse around MaxRadius
};

UENUM(BlueprintType)
enum class EAuraEffectApplicationType : uint8
{
    SingleHit,      // Projectile impact or single-sweep Nova hit
    TickInterval,   // Periodic application every TickInterval seconds (e.g., 0.5s)
    ConstantRate,   // Continuous per-frame attribute change (+5/sec * DeltaTime)
    PassiveWhileIn  // Grants flat bonus (+15 Strength) while inside radius, removes on exit
};

UENUM(BlueprintType)
enum class EPassBackTriggerEvent : uint8
{
    OnCast,
    OnCollision,
    OnDamageDealt
};


// --- UHT REPLICATION MAP WORKAROUND WRAPPERS ---
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
        LocalLookupMap.Empty();
        for (const FTagFloatPair& Pair : ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid()) LocalLookupMap.Add(Pair.KeyTag, Pair.Value);
        }
    }
    void UpdateEntry(FGameplayTag Key, float NewValue)
    {
        LocalLookupMap.Add(Key, NewValue);
        int32 FoundIdx = ReplicatedEntries.IndexOfByPredicate([Key](const FTagFloatPair& Element) { return Element.KeyTag == Key; });
        if (FoundIdx != INDEX_NONE) ReplicatedEntries[FoundIdx].Value = NewValue;
        else { FTagFloatPair NewPair; NewPair.KeyTag = Key; NewPair.Value = NewValue; ReplicatedEntries.Add(NewPair); }
    }
};


// --- ATTRIBUTE FAST ARRAY ---
USTRUCT(BlueprintType)
struct FDynamicAttributeItem : public FFastArraySerializerItem
{
    GENERATED_BODY()
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

template<> struct TStructOpsTypeTraits<FFastAttributeArray> : public TStructOpsTypeTraitsBase2<FFastAttributeArray> { enum { WithNetDeltaSerializer = true }; };


// --- REPLICATED PHYSICS HIT PAYLOAD ---
USTRUCT(BlueprintType)
struct FFastPhysicsHitRepItem : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() uint8 TargetLimbId = 0;
    UPROPERTY() uint16 PackedImpulseMagnitude = 0;
    UPROPERTY() FVector_NetQuantizeNormal HitNormal = FVector_NetQuantizeNormal::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 HitLocation = FVector_NetQuantize10::ZeroVector;
    UPROPERTY() uint8 ReactionState = 0;

    void PreReplicatedRemove(const struct FFastPhysicsHitRepArray& InArrayOwner) {}
    void PostReplicatedAdd(const struct FFastPhysicsHitRepArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastPhysicsHitRepArray& InArrayOwner);
};

USTRUCT(BlueprintType)
struct FFastPhysicsHitRepArray : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FFastPhysicsHitRepItem> Items;
    UPROPERTY(NotReplicated) UDynamicForceComponent* OwnerForceComponent = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FFastPhysicsHitRepItem, FFastPhysicsHitRepArray>(Items, DeltaParms, *this);
    }
};

template<> struct TStructOpsTypeTraits<FFastPhysicsHitRepArray> : public TStructOpsTypeTraitsBase2<FFastPhysicsHitRepArray> { enum { WithNetDeltaSerializer = true }; };


// --- GASP STAGE ANIMATION & VFX PRESENTATION ---
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
    FAbilityStagePresentation ImpactStage;
};


// --- SOURCE PASS-BACK ATTRIBUTE REWARDS ---
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


// --- UNIFIED SPATIAL DELIVERY ARCHETYPE & CONFIG ---
USTRUCT(BlueprintType)
struct FDynamicDeliveryArchetype
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    TSoftObjectPtr<UStaticMesh> MeshAsset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag PrimaryGameplayCue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FDynamicTagFloatMapWrapper Metrics;
};

USTRUCT(BlueprintType)
struct FDynamicSpatialDeliveryDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    FGameplayTag ArchetypeLinkTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    ESpatialDeliveryMotion MotionType = ESpatialDeliveryMotion::StraightVelocity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    ESpatialShapeBehavior ShapeBehavior = ESpatialShapeBehavior::PointImpact;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    EAuraEffectApplicationType ApplicationType = EAuraEffectApplicationType::SingleHit;

    // Spatial Dimensions & Timing
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

    // Multi-Cast / Spread
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    EMultiShotType MultiShotType = EMultiShotType::Single;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    int32 SpawnCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float SpreadAngleDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Delivery")
    float PowerDecayPerAdditional = 0.85f;

    // Direct Attribute / Effect / Kinetic Payload
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
};


// --- OTHER PROFILES (CHAIN, MOVEMENT, ATTRIBUTES, EFFECTS) ---
USTRUCT(BlueprintType)
struct FDynamicChainProfile
{
    GENERATED_BODY()
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
    FDynamicTagFloatMapWrapper MovementMetrics;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|GASP Integration")
    FGaspMotorConfig GaspMotorSettings;
};

USTRUCT(BlueprintType)
struct FDynamicAttributeDef
{
    GENERATED_BODY()
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


// --- FLATTENED TRUTH LOGIC TREES ---
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


// --- ABILITY DEFINITIONS ---
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
};

USTRUCT(BlueprintType)
struct FDynamicChargeProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float MaxChargeTime = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    EMaxChargeResolution MaxChargeResolution = EMaxChargeResolution::AutoRelease;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag MutationAbilityTag;
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
    EAbilityExecutionType ExecutionType = EAbilityExecutionType::Instant;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicTagFloatMapWrapper NumericParams;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicAbilityLifeCycleRules LifeCycleRequirements;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicChargeProfile ChargeProfile;

    // Unified Spatial Delivery (replaces separate Projectile, AoE, Aura, and Nova structs)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicSpatialDeliveryDef SpatialDelivery;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicChainProfile ChainConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicMovementProfile MovementConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicAbilityStageVisuals StageVisuals;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TArray<FSourcePassBackRule> SourcePassBackRules;
};

USTRUCT(BlueprintType)
struct FDynamicAbilitySlotChain
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Progression")
    FGameplayTag SlotInputTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Progression")
    TArray<FGameplayTag> ChainedAbilityTags;
};

USTRUCT(BlueprintType)
struct FDynamicMilestoneAction
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag ExecutionTriggerConditionTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TArray<FDynamicModifierDef> EffectsToApply;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    bool bIsIntervalRepeating = false;
};

USTRUCT(BlueprintType)
struct FDynamicMilestoneContainer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
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


// --- PHYSICS SCHEMA FOR GAMESTATE ---
USTRUCT(BlueprintType)
struct FPhysicsCalculationSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CalculationMode = TEXT("ImpulseMagnitude");
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
};

USTRUCT(BlueprintType)
struct FLimbAttributeModifiers
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HealthReductionMultiplier = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StabilityDamage = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StunDuration = 0.0f;
};

USTRUCT(BlueprintType)
struct FPhysicsLimbConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 LimbId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag LimbTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ImpulseMultiplier = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StumbleThreshold = 1000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BreakRagdollThreshold = 2000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> Bones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLimbAttributeModifiers AssociatedAttributes;
};

USTRUCT(BlueprintType)
struct FPhysicsDamageConfigData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Version = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ConfigName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPhysicsCalculationSettings CalculationSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPhysicsLimbConfig> Limbs;
};

// --- ROUTING MAP PAIRS ---
USTRUCT(BlueprintType) struct FTagAbilityDefPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicAbilityDef ValueDef; };
USTRUCT(BlueprintType) struct FTagDeliveryArchetypePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicDeliveryArchetype ValueArchetype; };
USTRUCT(BlueprintType) struct FNameConditionPair { GENERATED_BODY() UPROPERTY() FName KeyName; UPROPERTY() FDynamicConditionTreeContainer ValueNode; };
USTRUCT(BlueprintType) struct FTagMilestonePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicMilestoneContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagThresholdPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicThresholdContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagEffectPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicEffectDef ValueDef; };
2. Source/GDB/GAS/DynamicGameState.h & .cppConsolidates ProjectileArchetypes and AoeArchetypes into DeliveryArchetypes and adds an object pool for ADynamicSpatialActor so projectiles, persistent auras, and ground zones never hitch the server with runtime allocations or garbage collection.   DynamicGameState.hC++// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

class ADynamicSpatialActor;

USTRUCT(BlueprintType)
struct FTagAttributePair
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag KeyTag;
    UPROPERTY() FDynamicAttributeDef ValueDef;
};

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- UHT-Safe Transport Containers ---
    UPROPERTY(ReplicatedUsing = OnRep_AttributeMasterRegistryArr) TArray<FTagAttributePair> AttributeMasterRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_DeliveryArchetypesArr) TArray<FTagDeliveryArchetypePair> DeliveryArchetypesArr;
    UPROPERTY(ReplicatedUsing = OnRep_AbilityMasterRegistryArr) TArray<FTagAbilityDefPair> AbilityMasterRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_SharedConditionTreesArr) TArray<FNameConditionPair> SharedConditionTreesArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterMilestoneRegistryArr) TArray<FTagMilestonePair> MasterMilestoneRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterThresholdRegistryArr) TArray<FTagThresholdPair> MasterThresholdRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterEffectRegistryArr) TArray<FTagEffectPair> MasterEffectRegistryArr;

    // --- Local Fast Authoritative Lookups ---
    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;
    TMap<FGameplayTag, FDynamicDeliveryArchetype> DeliveryArchetypes;
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;
    TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;

    void RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);
    void RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);

    // --- Unified Spatial Actor Pooling (Server Authority) ---
    ADynamicSpatialActor* AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn);
    void ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn);

    // --- Physics Mappings ---
    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
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

private:
    UPROPERTY(Replicated) FPhysicsCalculationSettings CalculationSettings;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;

    UPROPERTY()
    TArray<TObjectPtr<ADynamicSpatialActor>> InactiveSpatialActorPool;
};
DynamicGameState.cppC++// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
}

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicGameState, AttributeMasterRegistryArr);
    DOREPLIFETIME(ADynamicGameState, DeliveryArchetypesArr);
    DOREPLIFETIME(ADynamicGameState, AbilityMasterRegistryArr);
    DOREPLIFETIME(ADynamicGameState, SharedConditionTreesArr);
    DOREPLIFETIME(ADynamicGameState, MasterMilestoneRegistryArr);
    DOREPLIFETIME(ADynamicGameState, MasterThresholdRegistryArr);
    DOREPLIFETIME(ADynamicGameState, MasterEffectRegistryArr);
    DOREPLIFETIME(ADynamicGameState, CalculationSettings);
}

void ADynamicGameState::RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !AbilityTag.IsValid()) return;
    AbilityMasterRegistry.Add(AbilityTag, AbilityDef);
    FTagAbilityDefPair NewAbPair; NewAbPair.KeyTag = AbilityTag; NewAbPair.ValueDef = AbilityDef;
    AbilityMasterRegistryArr.Add(NewAbPair);
    for (const auto& CondPair : AssociatedConditions)
    {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair; NewCondPair.KeyName = CondPair.Key; NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

void ADynamicGameState::RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !EffectTag.IsValid()) return;
    MasterEffectRegistry.Add(EffectTag, EffectDef);
    FTagEffectPair NewEffectPair; NewEffectPair.KeyTag = EffectTag; NewEffectPair.ValueDef = EffectDef;
    MasterEffectRegistryArr.Add(NewEffectPair);
    for (const auto& CondPair : AssociatedConditions)
    {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair; NewCondPair.KeyName = CondPair.Key; NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

ADynamicSpatialActor* ADynamicGameState::AcquireSpatialActor(const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn)
{
    if (!HasAuthority()) return nullptr;

    while (InactiveSpatialActorPool.Num() > 0)
    {
        ADynamicSpatialActor* PooledActor = InactiveSpatialActorPool.Pop(EAllowShrinking::No);
        if (IsValid(PooledActor))
        {
            PooledActor->SetOwner(OwnerActor);
            PooledActor->SetInstigator(InstigatorPawn);
            PooledActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
            return PooledActor;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = OwnerActor;
    SpawnParams.Instigator = InstigatorPawn;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return GetWorld()->SpawnActor<ADynamicSpatialActor>(ADynamicSpatialActor::StaticClass(), SpawnTransform, SpawnParams);
}

void ADynamicGameState::ReleaseSpatialActorToPool(ADynamicSpatialActor* ActorToReturn)
{
    if (!HasAuthority() || !IsValid(ActorToReturn)) return;
    InactiveSpatialActorPool.AddUnique(ActorToReturn);
}

void ADynamicGameState::SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData)
{
    CalculationSettings = ConfigData.CalculationSettings;
    LimbLookupMap.Empty();
    BoneToLimbMap.Empty();
    for (const FPhysicsLimbConfig& Limb : ConfigData.Limbs)
    {
        LimbLookupMap.Add(Limb.LimbId, Limb);
        for (const FName& BoneName : Limb.Bones) BoneToLimbMap.Add(BoneName, Limb.LimbId);
    }
}

uint8 ADynamicGameState::GetLimbIdForBone(const FName& BoneName) const
{
    if (const uint8* FoundId = BoneToLimbMap.Find(BoneName)) return *FoundId;
    return 1;
}

const FPhysicsLimbConfig* ADynamicGameState::GetLimbData(uint8 LimbId) const
{
    return LimbLookupMap.Find(LimbId);
}

void ADynamicGameState::OnRep_AttributeMasterRegistryArr()
{
    AttributeMasterRegistry.Empty();
    for (const auto& Element : AttributeMasterRegistryArr) AttributeMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
}

void ADynamicGameState::OnRep_DeliveryArchetypesArr()
{
    DeliveryArchetypes.Empty();
    for (auto& Element : DeliveryArchetypesArr)
    {
        Element.ValueArchetype.Metrics.SyncMapFromChangesArray();
        DeliveryArchetypes.Add(Element.KeyTag, Element.ValueArchetype);
    }
}

void ADynamicGameState::OnRep_AbilityMasterRegistryArr()
{
    AbilityMasterRegistry.Empty();
    for (auto& Element : AbilityMasterRegistryArr)
    {
        Element.ValueDef.NumericParams.SyncMapFromChangesArray();
        Element.ValueDef.SpatialDelivery.RuntimeMetricModifiers.SyncMapFromChangesArray();
        Element.ValueDef.MovementConfig.MovementMetrics.SyncMapFromChangesArray();
        Element.ValueDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds.SyncMapFromChangesArray();
        AbilityMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
    }
}

void ADynamicGameState::OnRep_SharedConditionTreesArr()
{
    SharedConditionTrees.Empty();
    for (const auto& Element : SharedConditionTreesArr) SharedConditionTrees.Add(Element.KeyName, Element.ValueNode);
}

void ADynamicGameState::OnRep_MasterMilestoneRegistryArr()
{
    MasterMilestoneRegistry.Empty();
    for (const auto& Element : MasterMilestoneRegistryArr) MasterMilestoneRegistry.Add(Element.KeyTag, Element.ValueContainer);
}

void ADynamicGameState::OnRep_MasterThresholdRegistryArr()
{
    MasterThresholdRegistry.Empty();
    for (const auto& Element : MasterThresholdRegistryArr) MasterThresholdRegistry.Add(Element.KeyTag, Element.ValueContainer);
}

void ADynamicGameState::OnRep_MasterEffectRegistryArr()
{
    MasterEffectRegistry.Empty();
    for (const FTagEffectPair& Element : MasterEffectRegistryArr) MasterEffectRegistry.Add(Element.KeyTag, Element.ValueDef);
}
3. Source/GDB/GAS/DynamicGASManagerComponent.h & .cppEliminates branching across separate projectile/AoE registries in ResolveMetric and provides GrantChainedAbilitySpec for multi-spec ability progression.   DynamicGASManagerComponent.hC++// DynamicGASManagerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "DynamicGASTypes.h"
#include "DynamicGASManagerComponent.generated.h"

class UAbilitySystemComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicGASManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicGASManagerComponent();

    static const FDynamicConditionTreeContainer* GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName);
    float ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Progression")
    FGameplayAbilitySpecHandle GrantChainedAbilitySpec(UAbilitySystemComponent* TargetASC, const FDynamicAbilitySlotChain& SlotChain);
};
DynamicGASManagerComponent.cppC++// DynamicGASManagerComponent.cpp
#include "DynamicGASManagerComponent.h"
#include "DynamicGameState.h"
#include "DynamicGameplayAbility.h"
#include "AbilitySystemComponent.h"

UDynamicGASManagerComponent::UDynamicGASManagerComponent() {}

float UDynamicGASManagerComponent::ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const
{
    float CombinedValue = 0.0f;
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicDeliveryArchetype* BaseArch = GS->DeliveryArchetypes.Find(ConfigProfileTag))
            {
                CombinedValue = BaseArch->Metrics.LocalLookupMap.FindRef(MetricTag);
            }
        }
    }
    if (const float* ModifierVal = Modifiers.LocalLookupMap.Find(MetricTag))
    {
        CombinedValue += *ModifierVal;
    }
    return CombinedValue;
}

const FDynamicConditionTreeContainer* UDynamicGASManagerComponent::GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName)
{
    if (!WorldContextObject || ConditionName.IsNone()) return nullptr;
    if (UWorld* World = WorldContextObject->GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            return GS->SharedConditionTrees.Find(ConditionName);
        }
    }
    return nullptr;
}

FGameplayAbilitySpecHandle UDynamicGASManagerComponent::GrantChainedAbilitySpec(UAbilitySystemComponent* TargetASC, const FDynamicAbilitySlotChain& SlotChain)
{
    if (!TargetASC || !TargetASC->IsOwnerActorAuthoritative() || SlotChain.ChainedAbilityTags.Num() == 0)
    {
        return FGameplayAbilitySpecHandle();
    }

    FGameplayAbilitySpec Spec(UDynamicGameplayAbility::StaticClass(), 1, INDEX_NONE, this);
    Spec.GetDynamicSpecSourceTags().AddTag(SlotChain.SlotInputTag);

    FGameplayAbilitySpecHandle SpecHandle = TargetASC->GiveAbility(Spec);
    if (FGameplayAbilitySpec* GrantedSpec = TargetASC->FindAbilitySpecFromHandle(SpecHandle))
    {
        if (UDynamicGameplayAbility* InstancedAbility = Cast<UDynamicGameplayAbility>(GrantedSpec->GetPrimaryInstance()))
        {
            InstancedAbility->SetCustomSpecChain(SlotChain.ChainedAbilityTags);
        }
    }
    return SpecHandle;
}
4. Source/GDB/GAS/DynamicDataSerializer.h & .cppUses a single unified ParseSpatialDelivery / SerializeSpatialDelivery helper and maps DeliveryArchetypes in one clean pass.   DynamicDataSerializer.hC++// DynamicDataSerializer.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "Dom/JsonObject.h"
#include "DynamicDataSerializer.generated.h"

class ADynamicGameState;

UCLASS()
class GDB_API UDynamicDataSerializer : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes);

private:
    static int32 FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer);
    static TSharedPtr<FJsonObject> ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer);

    static void DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper);
    static TSharedPtr<FJsonObject> SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper);
};
DynamicDataSerializer.cppC++// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"

template<typename TEnum>
FString EnumToString(const FString& EnumName, TEnum Value)
{
    const UEnum* EnumPtr = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::None);
    return EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(Value)) : FString("Invalid");
}

template<typename TEnum>
TEnum StringToEnum(const FString& EnumName, const FString& ValueStr)
{
    const UEnum* EnumPtr = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::None);
    return EnumPtr ? static_cast<TEnum>(EnumPtr->GetValueByNameString(ValueStr)) : static_cast<TEnum>(0);
}

void UDynamicDataSerializer::DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper)
{
    OutWrapper.ReplicatedEntries.Empty();
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;

    TSharedPtr<FJsonObject> MapSection = InObject->GetObjectField(FieldName);
    if (!MapSection.IsValid()) return;

    if (MapSection->HasField(TEXT("ReplicatedEntries")))
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayField = MapSection->GetArrayField(TEXT("ReplicatedEntries"));
        for (const auto& Element : ArrayField)
        {
            TSharedPtr<FJsonObject> EntryObj = Element->AsObject();
            if (EntryObj.IsValid())
            {
                FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("KeyTag"))), false);
                if (KeyTag.IsValid())
                {
                    FTagFloatPair Entry;
                    Entry.KeyTag = KeyTag;
                    Entry.Value = EntryObj->GetNumberField(TEXT("Value"));
                    OutWrapper.ReplicatedEntries.Add(Entry);
                }
            }
        }
    }
    OutWrapper.SyncMapFromChangesArray();
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper)
{
    TSharedRef<FJsonObject> WrapperObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> ArrayJson;
    for (const FTagFloatPair& Pair : InWrapper.ReplicatedEntries)
    {
        TSharedRef<FJsonObject> EntryObj = MakeShareable(new FJsonObject());
        EntryObj->SetStringField(TEXT("KeyTag"), Pair.KeyTag.ToString());
        EntryObj->SetNumberField(TEXT("Value"), Pair.Value);
        ArrayJson.Add(MakeShareable(new FJsonValueObject(EntryObj)));
    }
    WrapperObj->SetArrayField(TEXT("ReplicatedEntries"), ArrayJson);
    return WrapperObj;
}

bool UDynamicDataSerializer::DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState)
{
    if (!TargetGameState) return false;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    // 1. Deserialize Master Attributes Registry
    if (RootObject->HasField(TEXT("AttributeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AttributeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicAttributeDef AttrDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &AttrDef, 0, 0);
            TargetGameState->AttributeMasterRegistry.Add(KeyTag, AttrDef);
            FTagAttributePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = AttrDef;
            TargetGameState->AttributeMasterRegistryArr.Add(SyncPair);
        }
    }

    // 2. Deserialize Unified Delivery Archetypes
    if (RootObject->HasField(TEXT("DeliveryArchetypes")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("DeliveryArchetypes"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> ArchObj = Pair.Value->AsObject();
            FDynamicDeliveryArchetype ArchDef;
            FJsonObjectConverter::JsonObjectToUStruct(ArchObj.ToSharedRef(), &ArchDef, 0, 0);
            DeserializeMapWrapper(ArchObj, TEXT("Metrics"), ArchDef.Metrics);

            TargetGameState->DeliveryArchetypes.Add(KeyTag, ArchDef);
            FTagDeliveryArchetypePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueArchetype = ArchDef;
            TargetGameState->DeliveryArchetypesArr.Add(SyncPair);
        }
    }

    // 3. Deserialize Complete Data-Driven Abilities Map
    if (RootObject->HasField(TEXT("AbilityMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AbilityMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> AbilityObj = Pair.Value->AsObject();
            FDynamicAbilityDef AbilityDef;
            FJsonObjectConverter::JsonObjectToUStruct(AbilityObj.ToSharedRef(), &AbilityDef, 0, 0);

            DeserializeMapWrapper(AbilityObj, TEXT("NumericParams"), AbilityDef.NumericParams);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("SpatialDelivery")), TEXT("RuntimeMetricModifiers"), AbilityDef.SpatialDelivery.RuntimeMetricModifiers);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig")), TEXT("MovementMetrics"), AbilityDef.MovementConfig.MovementMetrics);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig"))->GetObjectField(TEXT("GaspMotorSettings")), TEXT("RequiredAttributeThresholds"), AbilityDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds);

            TargetGameState->AbilityMasterRegistry.Add(KeyTag, AbilityDef);
            FTagAbilityDefPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = AbilityDef;
            TargetGameState->AbilityMasterRegistryArr.Add(SyncPair);
        }
    }

    // 4. Deserialize Flattened Multi-Conditional Look-up Trees
    if (RootObject->HasField(TEXT("SharedConditionTrees")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SharedConditionTrees"))->Values)
        {
            FDynamicConditionTreeContainer TreeContainer;
            FlattenAndParseJsonNode(Pair.Value->AsObject(), TreeContainer);
            TargetGameState->SharedConditionTrees.Add(FName(*Pair.Key), TreeContainer);
            FNameConditionPair SyncPair; SyncPair.KeyName = FName(*Pair.Key); SyncPair.ValueNode = TreeContainer;
            TargetGameState->SharedConditionTreesArr.Add(SyncPair);
        }
    }

    // 5. Deserialize Milestone Advancement Rules
    if (RootObject->HasField(TEXT("MasterMilestoneRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterMilestoneRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicMilestoneContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            TargetGameState->MasterMilestoneRegistry.Add(KeyTag, Container);
            FTagMilestonePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueContainer = Container;
            TargetGameState->MasterMilestoneRegistryArr.Add(SyncPair);
        }
    }

    // 6. Deserialize Structural Automated Cap Threshold Rules
    if (RootObject->HasField(TEXT("MasterThresholdRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterThresholdRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicThresholdContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            TargetGameState->MasterThresholdRegistry.Add(KeyTag, Container);
            FTagThresholdPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueContainer = Container;
            TargetGameState->MasterThresholdRegistryArr.Add(SyncPair);
        }
    }

    // 7. Deserialize Master Gameplay Effects Registry
    if (RootObject->HasField(TEXT("EffectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EffectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicEffectDef EffectDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &EffectDef, 0, 0);
            TargetGameState->MasterEffectRegistry.Add(KeyTag, EffectDef);
            FTagEffectPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = EffectDef;
            TargetGameState->MasterEffectRegistryArr.Add(SyncPair);
        }
    }

    // 8. Deserialize Dedicated Server Physics Damage & Limb Configurations
    if (RootObject->HasField(TEXT("CalculationSettings")) && RootObject->HasField(TEXT("Limbs")))
    {
        FPhysicsDamageConfigData PhysicsConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(RootObject.ToSharedRef(), &PhysicsConfig, 0, 0))
        {
            TargetGameState->SetPhysicsConfig(PhysicsConfig);
        }
    }

    return true;
}

bool UDynamicDataSerializer::SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString)
{
    if (!InGameState) return false;
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());

    // 1. Serialize AttributeMasterRegistry
    if (InGameState->AttributeMasterRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> AttrMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AttributeMasterRegistry)
        {
            AttrMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("AttributeMasterRegistry"), AttrMap);
    }

    // 2. Serialize Unified DeliveryArchetypes
    if (InGameState->DeliveryArchetypes.Num() > 0)
    {
        TSharedPtr<FJsonObject> ArchMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->DeliveryArchetypes)
        {
            TSharedPtr<FJsonObject> ArchObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            ArchObj->SetObjectField(TEXT("Metrics"), SerializeMapWrapper(Pair.Value.Metrics));
            ArchMap->SetObjectField(Pair.Key.ToString(), ArchObj);
        }
        Root->SetObjectField(TEXT("DeliveryArchetypes"), ArchMap);
    }

    // 3. Serialize AbilityMasterRegistry
    if (InGameState->AbilityMasterRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> AbMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AbilityMasterRegistry)
        {
            TSharedPtr<FJsonObject> AbObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (!AbObj.IsValid()) continue;

            AbObj->SetObjectField(TEXT("NumericParams"), SerializeMapWrapper(Pair.Value.NumericParams));
            if (AbObj->HasField(TEXT("SpatialDelivery")))
            {
                AbObj->GetObjectField(TEXT("SpatialDelivery"))->SetObjectField(TEXT("RuntimeMetricModifiers"), SerializeMapWrapper(Pair.Value.SpatialDelivery.RuntimeMetricModifiers));
            }
            if (AbObj->HasField(TEXT("MovementConfig")))
            {
                TSharedPtr<FJsonObject> MovObj = AbObj->GetObjectField(TEXT("MovementConfig"));
                MovObj->SetObjectField(TEXT("MovementMetrics"), SerializeMapWrapper(Pair.Value.MovementConfig.MovementMetrics));
                if (MovObj->HasField(TEXT("GaspMotorSettings")))
                {
                    MovObj->GetObjectField(TEXT("GaspMotorSettings"))->SetObjectField(TEXT("RequiredAttributeThresholds"), SerializeMapWrapper(Pair.Value.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds));
                }
            }
            AbMap->SetObjectField(Pair.Key.ToString(), AbObj);
        }
        Root->SetObjectField(TEXT("AbilityMasterRegistry"), AbMap);
    }

    // 4. Serialize SharedConditionTrees
    if (InGameState->SharedConditionTrees.Num() > 0)
    {
        TSharedPtr<FJsonObject> CondMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->SharedConditionTrees)
        {
            if (Pair.Value.FlatNodes.Num() > 0)
            {
                CondMap->SetObjectField(Pair.Key.ToString(), ReconstructJsonFromFlatNode(0, Pair.Value));
            }
        }
        Root->SetObjectField(TEXT("SharedConditionTrees"), CondMap);
    }

    // 5. Serialize MasterMilestoneRegistry
    if (InGameState->MasterMilestoneRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> MilestoneMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterMilestoneRegistry)
        {
            MilestoneMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("MasterMilestoneRegistry"), MilestoneMap);
    }

    // 6. Serialize MasterThresholdRegistry
    if (InGameState->MasterThresholdRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> ThresholdMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterThresholdRegistry)
        {
            ThresholdMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("MasterThresholdRegistry"), ThresholdMap);
    }

    // 7. Serialize EffectMasterRegistry
    if (InGameState->MasterEffectRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> EffectMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterEffectRegistry)
        {
            EffectMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("EffectMasterRegistry"), EffectMap);
    }

    // 8. Serialize Physics Calculation Settings & Limbs
    Root->SetObjectField(TEXT("CalculationSettings"), FJsonObjectConverter::UStructToJsonObject(InGameState->GetCalculationSettings()));
    TArray<TSharedPtr<FJsonValue>> LimbsJson;
    for (const auto& Pair : InGameState->GetLimbLookupMap())
    {
        LimbsJson.Add(MakeShareable(new FJsonValueObject(FJsonObjectConverter::UStructToJsonObject(Pair.Value))));
    }
    Root->SetArrayField(TEXT("Limbs"), LimbsJson);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(Root, Writer);
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer)
{
    TSharedRef<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    if (!TreeContainer.FlatNodes.IsValidIndex(NodeIndex)) return JsonObj;

    const FDynamicConditionNode& Node = TreeContainer.FlatNodes[NodeIndex];
    JsonObj->SetStringField(TEXT("ConditionType"), EnumToString(TEXT("EConditionType"), Node.ConditionType));
    JsonObj->SetStringField(TEXT("LogicOperator"), EnumToString(TEXT("ELogicOperator"), Node.LogicOperator));
    JsonObj->SetStringField(TEXT("ComparisonOperator"), EnumToString(TEXT("EComparisonOp"), Node.ComparisonOperator));
    JsonObj->SetStringField(TEXT("UserDefinedKey"), Node.UserDefinedKey.ToString());
    JsonObj->SetBoolField(TEXT("bCheckSource"), Node.bCheckSource);
    JsonObj->SetNumberField(TEXT("ComparisonValue"), Node.ComparisonValue);

    TArray<TSharedPtr<FJsonValue>> ChildrenArr;
    for (int32 ChildIdx : Node.ChildIndices)
    {
        ChildrenArr.Add(MakeShareable(new FJsonValueObject(ReconstructJsonFromFlatNode(ChildIdx, TreeContainer))));
    }
    JsonObj->SetArrayField(TEXT("ChildNodes"), ChildrenArr);
    return JsonObj;
}

bool UDynamicDataSerializer::SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString)
{
    TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    TSharedPtr<FJsonObject> ArrayObject = MakeShareable(new FJsonObject());
    for (const FDynamicAttributeItem& Entry : InFastAttributes.Items)
    {
        if (Entry.AttributeTag.IsValid())
        {
            ArrayObject->SetNumberField(Entry.AttributeTag.ToString(), Entry.Value);
        }
    }
    RootObject->SetObjectField(TEXT("FastAttributes"), ArrayObject);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(RootObject, Writer);
}

bool UDynamicDataSerializer::DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    if (RootObject->HasField(TEXT("FastAttributes")))
    {
        OutFastAttributes.Items.Empty();
        TSharedPtr<FJsonObject> AttrMapObj = RootObject->GetObjectField(TEXT("FastAttributes"));
        for (auto& Pair : AttrMapObj->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (KeyTag.IsValid())
            {
                FDynamicAttributeItem NewEntry;
                NewEntry.AttributeTag = KeyTag;
                NewEntry.Value = Pair.Value->AsNumber();
                OutFastAttributes.Items.Add(NewEntry);
            }
        }
        OutFastAttributes.MarkArrayDirty();
        return true;
    }
    return false;
}

int32 UDynamicDataSerializer::FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer)
{
    if (!JsonObj.IsValid()) return INDEX_NONE;
    int32 NewNodeIndex = OutContainer.FlatNodes.AddDefaulted();
    FDynamicConditionNode& WorkingNode = OutContainer.FlatNodes[NewNodeIndex];
    WorkingNode.ConditionType = StringToEnum<EConditionType>(TEXT("EConditionType"), JsonObj->GetStringField(TEXT("ConditionType")));
    WorkingNode.LogicOperator = StringToEnum<ELogicOperator>(TEXT("ELogicOperator"), JsonObj->GetStringField(TEXT("LogicOperator")));
    WorkingNode.ComparisonOperator = StringToEnum<EComparisonOp>(TEXT("EComparisonOp"), JsonObj->GetStringField(TEXT("ComparisonOperator")));
    WorkingNode.UserDefinedKey = FName(*JsonObj->GetStringField(TEXT("UserDefinedKey")));
    WorkingNode.bCheckSource = JsonObj->GetBoolField(TEXT("bCheckSource"));
    WorkingNode.ComparisonValue = JsonObj->GetNumberField(TEXT("ComparisonValue"));

    if (JsonObj->HasField(TEXT("ChildNodes")))
    {
        for (const auto& ElementValue : JsonObj->GetArrayField(TEXT("ChildNodes")))
        {
            int32 ChildIndex = FlattenAndParseJsonNode(ElementValue->AsObject(), OutContainer);
            if (ChildIndex != INDEX_NONE) OutContainer.FlatNodes[NewNodeIndex].ChildIndices.Add(ChildIndex);
        }
    }
    return NewNodeIndex;
}