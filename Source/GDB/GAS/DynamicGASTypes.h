// DynamicGASTypes.h
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