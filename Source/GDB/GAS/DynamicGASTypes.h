#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.generated.h"

class UDynamicAttributeSet;
class ADynamicGameState;
class UDynamicForceComponent;

// --- ENUMS ---
UENUM(BlueprintType)
enum class EAbilityExecutionType : uint8 { Instant, Channeled, Charging, Projectile, AreaOfEffect, Hitscan, Aura, Chain, Movement };

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
enum class EAoeSpreadType : uint8 { Single, Pattern, RandomInRange, Sequence };

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
    UPROPERTY() uint8 ReactionState = 0; // 0 = Flinch, 1 = Stumble, 2 = Ragdoll

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


// --- DATA ARCHETYPES & PROFILES ---
USTRUCT(BlueprintType)
struct FDynamicAuraProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aura")
    FGameplayTag AuraLinkTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aura")
    FDynamicTagFloatMapWrapper AuraMetrics;
};

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
struct FDynamicProjectileArchetype
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TSoftObjectPtr<UStaticMesh> ProjectileMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TSoftObjectPtr<UParticleSystem> LegacyParticleSystem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicTagFloatMapWrapper Metrics;
};

USTRUCT(BlueprintType)
struct FDynamicAoeArchetype
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TSoftObjectPtr<UParticleSystem> GroundTargetVisualFx;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    TSoftObjectPtr<UParticleSystem> ImpactExplosionFx;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicTagFloatMapWrapper Metrics;
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

// --- LIFE CYCLES & ABILITIES ---
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
struct FDynamicPayloadProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTag ArchetypeLinkTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicTagFloatMapWrapper RuntimeMetricModifiers;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FGameplayTagContainer PropagatedTriggerTags;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicPayloadProfile ProjectileConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicPayloadProfile AoeConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicAuraProfile AuraConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicChainProfile ChainConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicMovementProfile MovementConfig;
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
USTRUCT(BlueprintType) struct FTagProjectilePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicProjectileArchetype ValueArchetype; };
USTRUCT(BlueprintType) struct FTagAoePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicAoeArchetype ValueArchetype; };
USTRUCT(BlueprintType) struct FNameConditionPair { GENERATED_BODY() UPROPERTY() FName KeyName; UPROPERTY() FDynamicConditionTreeContainer ValueNode; };
USTRUCT(BlueprintType) struct FTagMilestonePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicMilestoneContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagThresholdPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicThresholdContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagEffectPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicEffectDef ValueDef; };