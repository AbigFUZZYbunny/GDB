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
class UDynamicInteractableComponent;

// --- ENUMS ---
UENUM(BlueprintType)
enum class EAbilityExecutionType : uint8 { Instant, Channeled, Charging, SpatialDelivery, Hitscan, Chain, Movement, Interaction };

UENUM(BlueprintType)
enum class EMovementPhysicsType : uint8 { LaunchVelocity, RootMotionSource, CustomWallRun };

UENUM(BlueprintType)
enum class ELogicOperator : uint8 { And, Or, Nand, Nor };

UENUM(BlueprintType)
enum class EConditionType : uint8 { LogicalGateOnly, TagExists, TagValuePair, AttributeValue };

UENUM(BlueprintType)
enum class EComparisonOp : uint8 { LessThan, LessThanOrEqual, Equal, NotEqual, GreaterThanOrEqual, GreaterThan };

// Extended with Recovery phase to support melee/spell reset-window combos
UENUM(BlueprintType)
enum class EValidationPhase : uint8 { Attempt, Charging, Release, Recovery };

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

// Defines how a numeric property contributes to the external Ability Score calculation
UENUM(BlueprintType)
enum class EAbilityScoreScalingMode : uint8
{
    DirectProportional, // Higher property value = Higher Ability Score (e.g., Damage, Radius, SpawnCount, Duration, Bounces)
    InverseProportional // Lower property value = Higher Ability Score (e.g., Cooldown, Mana Cost, Charge Time, Tick Interval)
};


// --- LOCALIZATION & UI METADATA WRAPPER ---
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


// --- ATTRIBUTE FAST ARRAY (1-BYTE LIMB ID: 0 = GLOBAL, 1..255 = LIMB) ---
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

template<> struct TStructOpsTypeTraits<FFastAttributeArray> : public TStructOpsTypeTraitsBase2<FFastAttributeArray> { enum { WithNetDeltaSerializer = true }; };


// --- INTERACTABLE STATE FAST ARRAY & KINEMATIC TARGET POINTS ---
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

template<> struct TStructOpsTypeTraits<FFastInteractableStateArray> : public TStructOpsTypeTraitsBase2<FFastInteractableStateArray> { enum { WithNetDeltaSerializer = true }; };


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
    FAbilityStagePresentation RecoveryStage;

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    FDynamicLocalizationProfile Localization;

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
};


// --- DYNAMIC INTERACTION PROFILE ---
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
};


// --- OTHER PROFILES (CHAIN, MOVEMENT, ATTRIBUTES, EFFECTS) ---
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


// --- STAGE-INTERVAL COMBO WINDOWS & MASTERY COMBO GATING ---
USTRUCT(BlueprintType)
struct FComboWindowRule
{
    GENERATED_BODY()

    // Ability stage during which this combo window opens (Attempt, Charging, Release, Recovery)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    EValidationPhase ActivePhase = EValidationPhase::Charging;

    // Normalized progress [0.0..1.0] or elapsed seconds within ActivePhase when the window opens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float WindowStartTimeOrAlpha = 0.5f;

    // Normalized progress [0.0..1.0] or elapsed seconds within ActivePhase when the window closes
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float WindowEndTimeOrAlpha = 1.0f;

    // If true, WindowStart/End are evaluated as [0..1] charge/phase alpha; if false, evaluated as raw seconds
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    bool bUseNormalizedAlpha = true;

    // Ability tag that the player is allowed to trigger during this window (e.g., Fireball_2, LargeFireball, MeleeSwing_2)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTag AllowedComboAbilityTag;

    // Optional condition tree checked when the player attempts to trigger the combo
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FName OptionalConditionTreeName = NAME_None;
};

USTRUCT(BlueprintType)
struct FComboGateRule
{
    GENERATED_BODY()

    // If true, this ability CANNOT be cast directly from neutral unless the Mastery Bypass is met
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    bool bRequiresComboTransitionToCast = false;

    // Prerequisite abilities that can transition into this ability
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTagContainer ValidPrerequisiteAbilityTags;

    // Mastery attribute checked to bypass the combo gate (e.g., Attribute.Mastery.Element.Fire)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FGameplayTag BypassGateMasteryAttributeTag;

    // Minimum mastery value required to cast this ability directly without charging/comboing into it
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    float BypassGateMasteryThreshold = 50.0f;
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

    // Base cost and cooldown metrics used for execution and external Ability Score evaluation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float BaseResourceCost = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float BaseCooldownDuration = 1.5f;

    // Duration of the post-release Recovery/Reset stage (allows melee/spell follow-up combos before ending)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float RecoveryWindowDuration = 0.35f;
};

USTRUCT(BlueprintType)
struct FDynamicChargeProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS")
    float MaxChargeTime = 0.0f;

    // If true, holding past MaxChargeTime keeps the ability in the charged state waiting for player release or combo input
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

    // Associated Mastery & Point Pool tags for learning requirements (e.g., Attribute.Mastery.Element.Fire & Attribute.Mastery.Element.Fire.Points)
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

    // Player-driven combo windows opened during Attempt, Charging, Release, or Recovery stages
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    TArray<FComboWindowRule> ComboWindows;

    // Gating rules requiring a combo transition unless bypassed by high Mastery
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Combo")
    FComboGateRule ComboGate;
};


// --- EXTERNAL ABILITY SCORE & DIFFICULTY SCALING SCHEMA ---
USTRUCT(BlueprintType)
struct FAbilityPropertyScoreRule
{
    GENERATED_BODY()

    // Property identifier (e.g. "BaseCooldown", "BaseCost", "MaxChargeTime", "DirectMagnitude", "MaxRadius", "Speed", "SpawnCount", "KineticImpulse", "Duration", "MaxBounces")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FName PropertyName = NAME_None;

    // Optional GameplayTag if scoring a dynamic entry inside NumericParams, MovementMetrics, or InteractionMetrics
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FGameplayTag MetricTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    EAbilityScoreScalingMode ScalingMode = EAbilityScoreScalingMode::DirectProportional;

    // Baseline reference value (for InverseProportional: Score += Weight * (ReferenceValue / Max(MinClamp, ActualValue))^Exponent)
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

    // Multiplies the computed Ability Score
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float GlobalScoreMultiplier = 1.0f;

    // Converts Ability Score -> Mastery Points Cost to Learn (Cost = Round(AbilityScore * LearnPointCostPerScoreUnit * DifficultyMult))
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float LearnPointCostPerScoreUnit = 0.05f;

    // Converts Ability Score -> Minimum Mastery Value Required to Learn (MinMastery = AbilityScore * MinMasteryRequiredPerScoreUnit)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    float MinMasteryRequiredPerScoreUnit = 0.25f;

    // Score ceiling that maps to Base Spell Level 10 (Levels 1..10 are interpolated between MinScoreForLevel1 and MaxScoreForLevel10)
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

    // Base score weights per EAbilityExecutionType (mapped by enum string name or tag)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    FDynamicTagFloatMapWrapper ExecutionTypeWeights;

    // Per-property scaling rules
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AbilityScore")
    TArray<FAbilityPropertyScoreRule> PropertyRules;

    // Difficulty presets
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

    // Dynamically derived base spell level between 1 and 10
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    int32 ComputedSpellLevel = 1;

    // Calculated Attribute.Mastery.X.Points cost to learn
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float LearnPointCost = 1.0f;

    // Calculated minimum Attribute.Mastery.X value required to learn
    UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|AbilityScore")
    float MinimumMasteryRequired = 1.0f;
};


// --- BIDIRECTIONAL MILESTONES ---
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


// --- DYNAMIC SKELETAL TOPOLOGY & MULTI-JOINT LIMB CONFIG ---
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

// --- ROUTING MAP PAIRS ---
USTRUCT(BlueprintType) struct FTagAbilityDefPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicAbilityDef ValueDef; };
USTRUCT(BlueprintType) struct FTagDeliveryArchetypePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicDeliveryArchetype ValueArchetype; };
USTRUCT(BlueprintType) struct FNameConditionPair { GENERATED_BODY() UPROPERTY() FName KeyName; UPROPERTY() FDynamicConditionTreeContainer ValueNode; };
USTRUCT(BlueprintType) struct FTagMilestonePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicMilestoneContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagThresholdPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicThresholdContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagEffectPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicEffectDef ValueDef; };