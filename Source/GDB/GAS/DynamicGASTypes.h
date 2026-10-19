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
class UDynamicContainerComponent;
class ADynamicPhysicsActor;
class UDynamicSocialComponent;
class AAIController;

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

UENUM(BlueprintType)
enum class ECharacterGender : uint8
{
    Neutral,
    Male,
    Female,
    Other
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

// --- DYNAMIC ITEM DEFINITION (Tarkov Grid & Stacking) ---
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

// --- DYNAMIC CONTAINER SLOT ITEM (Grid Coordinates) ---
USTRUCT(BlueprintType)
struct FDynamicContainerSlotItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    int32 GridX = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    int32 GridY = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    bool bIsRotated = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FGameplayTag StoredActorArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    int32 StackCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    TArray<FDynamicAttributeItem> SavedAttributeState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FGameplayTagContainer SavedInteractableStates;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Container")
    FString NestedContainerJsonData;

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

// --- CONTAINER DEFINITION ---
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

// --- CRAFTING RECIPE SYSTEM ---
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
    bool bScaleWithTargetSize = false; // Multiplies the RequiredQuantity by the target item's fitted size delta during adjustments.
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

    // Unique identifier for this specific recipe (e.g., Recipe.Weapon.Longsword.Forge)
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
    
    // Additional byproduct outputs (the primary created item is inherently the ArchetypeTag hosting this recipe)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FRecipeIngredient> AdditionalProducts;
};

// Wrapper for the GameState Map value
USTRUCT(BlueprintType)
struct FDynamicRecipeList
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FDynamicRecipeDef> Recipes;
};

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

// --- UNIFIED EVOLUTION & AGING SCHEMAS ---
USTRUCT(BlueprintType)
struct FEvolutionStageKeyframe
{
    GENERATED_BODY()

    // Normalized progression through lifespan/durability/XP [0.0 = Birth/Raw, 1.0 = Max/Rotted/Mastered]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float NormalizedProgress = 0.0f;

    // Descriptive tag for this phase (e.g. Stage.Apple.Unripe, Stage.Apple.Ripe, Stage.Apple.Rotted; or Item.Rank.Heroic)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag StageTag;

    // Mesh scale or asset transform adjustment over time
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float ScaleMultiplier = 1.0f;

    // Additive attributes or stat bonuses granted by this evolution stage (e.g. +Damage on Heroic sword, or nutritional value on ripe apple)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FTagFloatPair> StageAttributeModifiers;

    // Dynamic tags added to the actor/item while in this stage (e.g. State.Item.Edible, State.Item.Decayed)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FGameplayTag> TagsToApply;
};

USTRUCT(BlueprintType)
struct FBiomeShiftRule
{
    GENERATED_BODY()

    // The biome this cell will transition into if elemental conditions are held out of range
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag TargetBiomeTag;

    // Flat condition tree name evaluated against the cell's current Elemental & Physical metrics
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FName ShiftConditionTreeName = NAME_None;

    // How many in-game hours the condition must be sustained before the biome permanently shifts
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float RequiredSustainedGameHours = 48.0f;
};

USTRUCT(BlueprintType)
struct FDynamicBiomeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag BiomeTag; // e.g., Biome.Temperate.Forest, Biome.Arid.Desert, Biome.Arctic.Tundra

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FDynamicTagStringMapWrapper Localization;

    // Baseline equilibrium values for the 6 elements (Fire, Water, Earth, Air, Light, Dark)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FTagFloatPair> DefaultElementalEquilibrium;

    // Rate per game-hour at which disturbed elemental levels pull back toward biome defaults
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float HomeostasisPullRate = 2.5f;

    // Rules for permanent terraforming / biome shifting when elements are forced out of range
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    TArray<FBiomeShiftRule> BiomeShiftRules;
};

USTRUCT(BlueprintType)
struct FDynamicWeatherPatternDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    FGameplayTag WeatherTag; // e.g., Weather.Storm.Rain, Weather.Heatwave, Weather.Blizzard

    // Condition tree evaluated against cell elements (e.g., Air >= 60 && Water >= 70)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    FName SpawnConditionTreeName = NAME_None;

    // How much Air Mana (Water vapor) is transferred to Ground/Earth per game-hour while active
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FTagFloatPair> ElementalTransferRates;

    // Direct modifiers to physical environment metrics (e.g., Wetness +50, Temperature -10)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FTagFloatPair> EnvironmentMetricOffsets;

    // Gameplay effects applied to characters/objects exposed to this weather
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Weather")
    TArray<FGameplayTag> ExposedEffectsToApply;
};

// --- ENVIRONMENTAL EVOLUTION SCALING (REFRIGERATION / SPOILAGE / GROWTH) ---

USTRUCT(BlueprintType)
struct FEnvironmentalRateModifier
{
    GENERATED_BODY()

    // e.g., Env.Metric.Temperature, Env.Metric.Wetness, Env.Metric.Sunlight, Element.Fire
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag EnvironmentMetricTag;

    // Below MinThreshold, rate scales toward MinRateMultiplier (e.g. 0.0x spoilage at <= 2.0 Celsius)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MinThreshold = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MaxThreshold = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MinRateMultiplier = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float MaxRateMultiplier = 2.0f;

    // If true, values outside [MinThreshold, MaxThreshold] halt evolution completely (e.g., frozen saplings)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    bool bPauseOutsideRange = false;
};

// --- FAST ARRAY: SPATIAL ENVIRONMENT CELLS & MOVING WEATHER FRONTS ---

USTRUCT(BlueprintType)
struct FEnvironmentGridCellItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    int32 CellIndex = 0; // Flattened 2D Grid Index: (Y * GridWidth) + X

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag CurrentBiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag TargetShiftBiomeTag;

    // 0.0 = Pure CurrentBiome, 1.0 = Shifted to TargetShiftBiome
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float BiomeBlendAlpha = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FGameplayTag ActiveWeatherTag;

    // 6 Elemental Mana Pools in this spatial cell:
    // Index 0: Fire, 1: Water, 2: Earth, 3: Air, 4: Light, 5: Dark
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float ElementalDensities[6] = { 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f };

    // Derived Physical Metrics:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float TemperatureCelsius = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float Wetness = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float WindSpeed = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    FVector2D WindDirection = FVector2D(1.0f, 0.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
    float GroundMoisture = 50.0f;

    UPROPERTY(NotReplicated)
    float SustainedOutOfRangeHours = 0.0f;

    void PreReplicatedRemove(const struct FFastEnvironmentGridArray& InArrayOwner);
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
    class UDynamicEnvironmentSubsystem* OwningSubsystem = nullptr;

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

    // Total time (in real-world seconds scaled by game time) required to complete the full 0.0 to 1.0 lifecycle
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float TotalLifespanOrMaxXP = 100.0f;

    // Time-series phases governing progression
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FEvolutionStageKeyframe> ProgressionPhases;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FEnvironmentalRateModifier> EnvironmentalModifiers;
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

// --- EQUIPMENT FITTING & VISUALS ---
USTRUCT(BlueprintType)
struct FDynamicEquipmentRequirementDef
{
    GENERATED_BODY()

    // Required equipment slots (e.g., Slot.Chest.Outer, Slot.Arm.Left.Outer)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TArray<FGameplayTag> RequiredEquipmentSlots;

    // Physical sizing limits. Checked against the character's primary height/girth morphs.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    float FittedSizeMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    float FittedSizeMax = 1.0f;

    // If true, the item's SkeletalMesh will bind to the Character's Leader Pose Component, inheriting all animations and morph targets automatically (crucial for clothing layers).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    bool bUseLeaderPoseComponent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TSoftObjectPtr<UAnimMontage> EquipAnimMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TSoftObjectPtr<UAnimMontage> UnequipAnimMontage;
};

// --- NON-CHARACTER PHYSICS ACTOR DEFINITION ---
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

    // The recipes used to craft this specific actor archetype
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Crafting")
    TArray<FDynamicRecipeDef> CraftingRecipes;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FDynamicEquipmentRequirementDef EquipmentRequirement;
};

USTRUCT(BlueprintType)
struct FTagPhysicsActorDefPair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FDynamicPhysicsActorDef ValueDef;
};

// --- ENUMS FOR EQUIPMENT LAYERS & BODY REGIONS ---

USTRUCT(BlueprintType)
struct FDynamicEquipmentSlotItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Unique slot identifier tag (e.g. Slot.Torso.Outer, Slot.Arm.Left.Outer)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag SlotTag;

    // Associated LimbId (1..255) anchoring this equipment slot to physical bone topology
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    uint8 LinkedLimbId = 0;

    // Archetype tag of the currently equipped item (e.g. Actor.Combat.Armor.Chestplate)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag EquippedItemArchetypeTag;

    // Saved runtime attributes & stats of the equipped item
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    TArray<FDynamicAttributeItem> SavedItemAttributeState;

    // Saved interactable states of the equipped item
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
    class UDynamicEquipmentComponent* OwningEquipmentComp = nullptr;

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

// Defines an equipment slot definition profile for a character or actor
USTRUCT(BlueprintType)
struct FDynamicEquipmentSlotProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag SlotTag; // e.g. Slot.Torso.Outer

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    uint8 LimbId = 0; // Maps to skeletal limb

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTag LayerTag; // e.g. Layer.Outer, Layer.Clothing, Layer.Underwear, Layer.Covering

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Equipment")
    FGameplayTagContainer AllowedItemTags; // Whitelist restriction
};

// --- CUSTOMIZATION: COLORS & HAIR ---
USTRUCT(BlueprintType)
struct FDynamicColorGradient
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Customization")
    FLinearColor PrimaryColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Customization")
    FLinearColor SecondaryColor = FLinearColor::White; // Used for gradient transitions (e.g., White to Blue)
};

// --- CHARACTER TEMPLATE GENERATION TYPES ---

USTRUCT(BlueprintType)
struct FCharacterAttributeRangeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag AttributeTag;

    // Minimum possible starting value (maps to Base Value)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float MinValue = 1.0f;

    // Maximum possible starting value (maps to Max Value attribute)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float MaxValue = 100.0f;
};

USTRUCT(BlueprintType)
struct FCharacterWeightedAttributeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FGameplayTag AttributeTag;

    // Probability weight or float value chance [0.0 to 1.0] for this attribute/trait to be applied
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float SelectionChance = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    float AssignedValue = 1.0f;
};

// --- RACIAL SLIDER CLAMP OVERRIDE ---
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
    FGameplayTag TargetAttributeTag; // e.g. Attribute.Stat.Agility

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    EEffectModifierOp ModifierOp = EEffectModifierOp::Multiplicative; // Additive, Multiplicative, Override

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    float Magnitude = 0.0f; // e.g. 0.10 for +10% agility
};

USTRUCT(BlueprintType)
struct FCharacterGenderProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    ECharacterGender Gender = ECharacterGender::Neutral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    FDynamicLocalizationProfile Localization;

    // Modifiers applied to attributes when this gender is selected
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FGenderAttributeModifier> StatModifiers;

    // Gender-specific Bodyslide slider clamp overrides (e.g. restricting height or muscle mass bounds per gender)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FRacialSliderClampOverride> GenderSliderClamps;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Gender Modifiers")
    TArray<FGameplayTag> CulturalHairArchetypes;
};

USTRUCT(BlueprintType)
struct FUndeadTemplateConversionRule
{
    GENERATED_BODY()

    // If true, converts any base character template into an undead variant on spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    bool bCanBeUndead = true;

    // Suffix or prefix applied to localization name (e.g., "Undead Elf" or "Skeleton Wolf")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    FString NamePrefix = TEXT("Undead ");

    // Overridden AI Agent Profile (e.g., switching from Grazer or Villager AI to Haunt/Necromancer AI)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    FGameplayTag OverrideAIAgentTag = FGameplayTag::RequestGameplayTag(FName("AI.Type.Monster.Undead.Spectral"));

    // Multiplicative or Additive Stat Modifiers applied during conversion (e.g., Agility penalty, Health bonus)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    TArray<FGenderAttributeModifier> UndeadStatModifiers;

    // Forced Color Gradient Tint applied to skin/fur/scales to turn them ashen/decayed
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    FDynamicColorGradient UndeadColorTint;

    // Granted special abilities upon conversion (e.g., Life Drain, Immunity to Poison)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template|Undead")
    TArray<FGameplayTag> GrantedUndeadAbilities;
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

// --- MULTI-PROFESSION FAST ARRAY ---

USTRUCT(BlueprintType)
struct FDynamicAssignedProfessionItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Reference to master profession definition (e.g., Profession.Labor.Woodcutter, Profession.Crafting.Carpenter)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag ProfessionTag;

    // Skill level or mastery rank in this specific profession [1.0 to 100.0]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float ProficiencyLevel = 1.0f;

    // Relative priority weight when multiple professions overlap in schedule (Higher = Takes precedence)
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

// --- DYNAMIC CALENDAR & TIME SYSTEM ---
USTRUCT(BlueprintType)
struct FDynamicCalendarDef
{
    GENERATED_BODY()

    // How many game-world seconds pass per real-world second (e.g. 60.0 = 1 minute game time per 1 second real time)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    float TimeScalar = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 HoursPerDay = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 DaysPerSeason = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Calendar")
    int32 SeasonsPerYear = 4;
};

// --- AGING & LIFE STAGES ---
USTRUCT(BlueprintType)
struct FDynamicLifeStageDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    FGameplayTag StageTag; // e.g. Age.Baby, Age.Adult, Age.Elder

    // Minimum age in Game Years to reach this stage
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float MinimumAgeYears = 0.0f;

    // Physical mesh scaling for this life stage (e.g. Baby = 0.35)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float BaseScaleMultiplier = 1.0f;

    // Modifiers applied while in this life stage (e.g. Elder loses Strength, gains Intelligence)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FGenderAttributeModifier> StageAttributeModifiers;
};

// --- GENETIC INHERITANCE: DISCRETE ALLELES (Traits, Hair Color, Eye Color) ---
USTRUCT(BlueprintType)
struct FDynamicAlleleDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag AlleleTag; // e.g. Gene.Hair.Blonde, Trait.Congenital.Genius

    // Higher value determines expression dominance when paired against a recessive allele
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    float DominanceWeight = 1.0f;

    // Optional physical color expression (if this is a color gene)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FDynamicColorGradient PhenotypeColor;

    // Optional abilities granted by this gene (e.g. dragon-blood breath attack)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FGameplayTag> PhenotypeAbilities;

    // Optional stat modifiers granted when this allele is EXPRESSED
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FGenderAttributeModifier> PhenotypeModifiers;
};

USTRUCT(BlueprintType)
struct FDynamicGeneLocusDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag LocusTag; // e.g. GeneSlot.HairColor, GeneSlot.CongenitalTrait

    // Chance to randomly mutate into any other allele defined in this locus during inheritance
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    float MutationChance = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    TArray<FDynamicAlleleDef> PossibleAlleles;
};

// --- FAST ARRAY: PER-CHARACTER GENOME ---
USTRUCT(BlueprintType)
struct FDynamicGenePairItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag LocusTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag MaternalAllele;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag PaternalAllele;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Genetics")
    FGameplayTag ExpressedAllele; // Derived via dominance logic

    void PreReplicatedRemove(const struct FFastGenomeArray& InArrayOwner);
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
    class UDynamicLifeComponent* OwningLifeComp = nullptr;

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

// --- BODYSWAP / BONE SCALING & SLIDER DEFINITIONS ---

USTRUCT(BlueprintType)
struct FBoneScaleConstraint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    FName BoneName = NAME_None; // e.g. spine_01, upperarm_l, head

    // Axis or uniform scaling multiplier bounds for this bone
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MinScaleMultiplier = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MaxScaleMultiplier = 1.25f;

    // Bone length adjustment bounds (along translation axis)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    bool bAllowLengthAdjustment = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MinLengthOffset = -10.0f; // cm

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Bones")
    float MaxLengthOffset = 10.0f; // cm
};

USTRUCT(BlueprintType)
struct FBodyslideSliderDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    FGameplayTag SliderTag; // e.g. Slider.Body.Height, Slider.Face.JawWidth

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float DefaultValue = 0.5f; // [0.0 to 1.0] normalized slider position

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float MinLimit = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    float MaxLimit = 1.0f;

    // Associated bones driven by this slider
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Creator|Sliders")
    TArray<FBoneScaleConstraint> TargetBones;

    // Associated Mesh Morph Targets driven by this slider (e.g. "Body_Muscular", "Ear_Elf_Long")
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

    // Personality trait tags (e.g., Personality.Trait.Glutton, Personality.Trait.Ascetic, Personality.Trait.Industrious)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FGameplayTagContainer PersonalityTraitTags;

    // Food & Drink taste multipliers: Item/Category Tag -> Mood/Satisfaction multiplier
    // e.g., { "Item.Food.Meat.Roasted": 1.5, "Item.Drink.Alcohol.DwarvenAle": 2.0, "Item.Food.Vegetable.Raw": -0.5 }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FDynamicTagFloatMapWrapper FoodAndDrinkPreferences;

    // Activity & Social preferences: Activity/Topic Tag -> Affinity/Mood weight
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Personality")
    FDynamicTagFloatMapWrapper ActivityPreferences;
};

// --- GENETICS & CONTINUOUS AGING ---

USTRUCT(BlueprintType)
struct FContinuousGrowthKeyframe
{
    GENERATED_BODY()

    // Normalized progression through the expected lifespan [0.0 = Birth, 1.0 = Death]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float NormalizedAge = 0.0f;

    // Physical mesh scale at this age
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float ScaleMultiplier = 1.0f;

    // Multiplier applied to GASP physics motors (e.g. 0.1 for weak crawling babies, 1.0 for prime adults)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float PhysicsMotorMultiplier = 1.0f;

    // Animation state tag read by the AnimBP (e.g., Anim.Locomotion.Crawl, Anim.Locomotion.Toddle, Anim.Locomotion.Walk)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    FGameplayTag LocomotionAnimTag;

    // Additive overrides to Bodyslide sliders (e.g., adding wrinkles, drooping ears, or growing horns over time)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FTagFloatPair> MorphTargetOffsets;

    // Continuous stat modifiers lerped over time (e.g. Strength increases to adulthood, then decays in elder years)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    TArray<FTagFloatPair> StatModifiers;
};

USTRUCT(BlueprintType)
struct FAgingProfileDef
{
    GENERATED_BODY()

    // Absolute lifespan in game-years (e.g., Goblin = 0.08, Human = 80.0, Elf = 800.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Aging")
    float ExpectedLifespanYears = 80.0f;

    // Time-series array defining the physical and statistical lifecycle of the character
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

    // Maps AlleleTag -> DominanceWeight. Overrides the global dominance weight for this specific race.
    // e.g. Elves map Gene.Hair.Blonde to 100.0, Humans map Gene.Hair.Black to 10.0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Template")
    FDynamicTagFloatMapWrapper AlleleDominanceOverrides;
};

USTRUCT(BlueprintType)
struct FTagCharacterTemplatePair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FCharacterTemplateDef ValueDef;
};

USTRUCT(BlueprintType)
struct FDynamicCharacterMorphItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Customization")
    FGameplayTag SliderTag; // e.g. Slider.Body.Height, Slider.Body.Muscularity

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Character Customization")
    float SliderValue = 0.5f; // [0.0 to 1.0] normalized value

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
    None,           // Permanent bond (e.g., Mother, Bloodline)
    Linear,         // Constant erosion per in-game day
    Exponential,    // Rapid initial drop that flattens out
    StepThreshold   // Drops in discrete tiers after inactivity periods
};

// --- 1. RELATIONSHIP TYPE DEFINITION (Static Registry in GameState) ---

USTRUCT(BlueprintType)
struct FDynamicRelationshipTypeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag RelationshipTag; // e.g., Relation.Family.Mother, Relation.Social.Acquaintance, Relation.Object.Owner

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FDynamicLocalizationProfile Localization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    ERelationshipDecayCurve DecayCurve = ERelationshipDecayCurve::Exponential;

    // Rate at which Affinity/Debt erodes per in-game day (24h) of zero interaction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float AffinityDecayRatePerDay = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float FamiliarityDecayRatePerDay = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DebtDecayRatePerDay = 0.15f;

    // Minimum floor that decay cannot erode below (e.g., 0.80 for Mother, 0.0 for Tavern Acquaintance)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float MinAffinityDecayFloor = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float MinFamiliarityDecayFloor = 0.0f;

    // Baseline starting values when this relationship is first established
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DefaultAffinity = 0.0f; // [-1.0 .. 1.0]

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DefaultTrust = 0.0f; // [-1.0 .. 1.0]

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float DefaultFamiliarity = 0.1f; // [0.0 .. 1.0]

    // Multiplier applied to positive/negative social events on this bond
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float VolatilityScalar = 1.0f;
};

// --- 2. RUNTIME RELATIONSHIP MATRIX FAST ARRAY (Per-Character) ---

USTRUCT(BlueprintType)
struct FDynamicRelationshipEdgeItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Unique persistent ID of the target Character or ADynamicPhysicsActor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGuid TargetEntityGuid;

    // Optional cached weak pointer for O(1) runtime access when target is loaded in world
    UPROPERTY(NotReplicated)
    TWeakObjectPtr<AActor> CachedTargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    ERelationshipTargetKind TargetKind = ERelationshipTargetKind::Character;

    // Primary relationship classification (e.g., Relation.Family.Mother, Relation.Social.Acquaintance)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTag PrimaryRelationshipTag;

    // Additional contextual tags (e.g., Relation.Context.Coworker, Relation.Context.Rival, Relation.Object.FavoriteTool)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    FGameplayTagContainer SecondaryContextTags;

    // Core normalized dimensions [-1.0 to 1.0] or [0.0 to 1.0] at LastInteractionWorldTime
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float BaseAffinity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float BaseTrust = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float BaseFamiliarity = 0.0f;

    // Positive = Target owes this character; Negative = This character feels indebted to Target
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float BaseIndebtedness = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    float FearOrRespect = 0.0f;

    // World timestamp (in seconds) when this bond was last reinforced or interacted with
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Social")
    double LastInteractionWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastRelationshipEdgeArray& InArrayOwner);
    void PostReplicatedAdd(const struct FFastRelationshipEdgeArray& InArrayOwner);
    void PostReplicatedChange(const struct FFastRelationshipEdgeArray& InArrayOwner);
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

// --- 3. SPATIAL KNOWLEDGE, LANDMARKS & DAILY SCHEDULE ---

USTRUCT(BlueprintType)
struct FDynamicKnownPlaceItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Role of the place for this character (e.g., Place.Home, Place.Workstation, Place.Bed, Place.FoodSource, Place.Tavern)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGameplayTag PlaceRoleTag;

    // Specific landmark or building tag (e.g., Landmark.Village.BlacksmithForge, Actor.Workstation.Table.Alchemy)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGameplayTag PlaceArchetypeOrLandmarkTag;

    // Persistent GUID of the specific ADynamicPhysicsActor (e.g., specific bed, workbench, or chest)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FGuid LinkedActorGuid;

    // Last known world coordinates (quantized for fast network transport)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    FVector_NetQuantize10 LastKnownLocation = FVector_NetQuantize10::ZeroVector;

    // [0.0 .. 1.0] certainty of this memory (decays if moveable actor is relocated while unseen)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    float ConfidenceScore = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|SpatialKnowledge")
    double LastVisitedWorldTime = 0.0;

    void PreReplicatedRemove(const struct FFastKnownPlaceArray& InArrayOwner);
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

// --- 4. PROFESSIONS, TASK PRIORITIES & EXPECTED SCHEDULES ---

USTRUCT(BlueprintType)
struct FDynamicScheduleBlockDef
{
    GENERATED_BODY()

    // Start and end hour in 24-hour game time (e.g., 6.0 = 06:00 AM, 14.5 = 02:30 PM)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float StartHour24 = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float EndHour24 = 17.0f;

    // High-level activity (e.g., Activity.Work, Activity.Sleep, Activity.Eat, Activity.Socialize)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    FGameplayTag ScheduledActivityTag;

    // Where the character should go for this block (e.g., Place.Workstation, Place.Home, Place.Tavern)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    FGameplayTag PreferredPlaceRoleTag;

    // Priority weight [0.0 .. 10.0]; survival emergencies (Health/Starvation) can override low-priority schedule blocks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Schedule")
    float ScheduleAdherenceWeight = 1.0f;
};

USTRUCT(BlueprintType)
struct FDynamicTaskPriorityRule
{
    GENERATED_BODY()

    // Task identifier (e.g., Task.Crafting.ChopWood, Task.Crafting.SplitFirewood, Task.Farming.TendCrops, Task.Farming.SellProduce)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag TaskTag;

    // Base utility weight [0.0 .. 100.0]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float BasePriorityWeight = 50.0f;

    // Optional recipe or ability tag executed by this task
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag LinkedAbilityOrRecipeTag;

    // Required tool/item tags in inventory or hands (e.g., Actor.Tool.Axe.Felling)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTagContainer RequiredItemTags;

    // Output item tags produced for the local economy (e.g., Actor.Material.WoodLog, Actor.Material.Firewood)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTagContainer ProducedOutputTags;

    // Optional SharedConditionTree name to gate task validity (e.g., checking if local sawmill needs timber)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FName TaskConditionTreeName = NAME_None;
};

USTRUCT(BlueprintType)
struct FDynamicStatPriorityRule
{
    GENERATED_BODY()

    // Monitored attribute or need tag (e.g., Attribute.Resource.Health, Need.Biological.Hunger)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag AttributeOrNeedTag;

    // Normalized threshold [0.0 .. 1.0] below which emergency self-preservation overrides profession tasks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float CriticalThresholdRatio = 0.25f;

    // Priority multiplier when in critical state (e.g., Health = 1000.0 so survival always wins)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    float EmergencyPriorityWeight = 500.0f;
};

USTRUCT(BlueprintType)
struct FDynamicProfessionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FGameplayTag ProfessionTag; // e.g., Profession.Labor.Woodcutter, Profession.Labor.Farmer

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FDynamicLocalizationProfile Localization;

    // Ordered task priorities defining what is expected of this profession
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicTaskPriorityRule> ExpectedTasks;

    // Archetype tags of tools, raw materials, and commodities this profession prioritizes acquiring/keeping
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    FDynamicTagFloatMapWrapper PrioritizedItemWeights;

    // Stat & Need preservation priorities (e.g., Health > Stamina > Hunger > Work)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicStatPriorityRule> StatPriorities;

    // Default 24-hour daily routine for this profession
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Profession")
    TArray<FDynamicScheduleBlockDef> DefaultDailySchedule;
};

// --- 5. NEEDS, MOODS & PERSONALITY PREFERENCES (Food/Drink/Comfort) ---

USTRUCT(BlueprintType)
struct FDynamicNeedStateItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // e.g., Need.Biological.Hunger, Need.Biological.Thirst, Need.Biological.Sleep, Need.Social.Companionship, Mood.State.Happy
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Needs")
    FGameplayTag NeedOrMoodTag;

    // Normalized [0.0 .. 100.0] value (100 = Fully Satisfied, 0 = Starving/Exhausted)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Needs")
    float CurrentValue = 100.0f;

    // Change per in-game hour (e.g., -4.0f for Hunger)
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

USTRUCT(BlueprintType)
struct FDynamicAIAbilityRule
{
    GENERATED_BODY()

    // The ability the AI wants to use (e.g., Ability.Magic.Fireball, Ability.Monster.SpinWeb)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag AbilityTag;

    // Condition tree that MUST evaluate to true for the AI to cast this (e.g., Trigger.Condition.TargetIsStunned)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FName RequiredConditionTreeName = NAME_None;

    // Priority weight [0.0 .. 100.0] if multiple abilities are valid
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float PriorityWeight = 50.0f;

    // Minimum distance to target to consider using this ability
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float MinRange = 0.0f;

    // Maximum distance to target to consider using this ability
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float MaxRange = 1500.0f;
};

// --- AI GROUP & PACK DYNAMICS ---

USTRUCT(BlueprintType)
struct FDynamicAIGroupDynamics
{
    GENERATED_BODY()

    // Identifies the pack/group type (e.g., Faction.Beast.WolfPack)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag GroupArchetypeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bFormsPacks = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    int32 MaxGroupSize = 1;

    // Distance within which AI will share aggro or call for help
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float CohesionRadius = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bDefendsPackMembers = true;
};

// --- MASTER AI AGENT DEFINITION ---

USTRUCT(BlueprintType)
struct FDynamicAIAgentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTag AIAgentTag; // e.g., AI.Type.Beast.Wolf, AI.Type.Humanoid.Villager

    // The base Unreal Controller class to spawn (handles standard pathfinding, perception, or LLM bridging)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TSoftClassPtr<AAIController> AIControllerClass;

    // Defines pack mentality, swarming, or solitary behavior
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FDynamicAIGroupDynamics GroupDynamics;

    // Determines what the AI considers "Food" (e.g., Item.Food.Meat, Tag.Corpse)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    FGameplayTagContainer DietaryTags;

    // If true, the AI will actively hunt entities that drop items matching its DietaryTags
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    bool bHuntsForFood = false;

    // Rules for when and how to use specific abilities in combat or utility
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TArray<FDynamicAIAbilityRule> AbilityUsageRules;

    // Reusing our Profession Task rules to dictate idle/utility AI behavior (e.g., Spiders building webs, Wolves patrolling)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    TArray<FDynamicTaskPriorityRule> IdleTasks;

    // Morale / Flee threshold (0.0 to 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|AI")
    float FleeHealthThreshold = 0.15f;
};

// Wrapper for the GameState Map replication
USTRUCT(BlueprintType)
struct FTagAIAgentDefPair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FDynamicAIAgentDef ValueDef;
};

// --- DYNAMIC TRANSFORMATION & CONVERSION SYSTEM ---

USTRUCT(BlueprintType)
struct FDynamicTransformationRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTag TransformationTag; // e.g. Transformation.Vampirism, Transformation.Lycanthropy.Werewolf, Transformation.Undead, Transformation.Lichdom

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FDynamicLocalizationProfile Localization;

    // Sub-category tags (e.g. SuperCategory.Undead applied to Vampires)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer InheritedCategoryTags; 

    // Restrictions on what base character templates can undergo this transformation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer AllowedBaseTemplateTags; // e.g. Character.Humanoid (Empty = Any Skeleton)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FGameplayTag OverrideAIAgentTag;

    // Color gradient tint override (e.g. Ashen pale skin for vampires/undead, fur coloration for werewolves)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    bool bApplyColorTint = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    FDynamicColorGradient TransformationColorTint;

    // Attribute modifiers applied upon transformation (e.g. Vampires gain agility/strength, lose max HP when siring)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    TArray<FGenderAttributeModifier> TransformationStatModifiers;

    // Granted special abilities (e.g. BatForm, BloodSiphon, Howl)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation")
    TArray<FGameplayTag> GrantedAbilities;

    // --- Systemic Rules ---
    // If true, healing magic deals damage instead of restoring health (Undead/Vampire rule)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bInvertsHealingToDamage = false;

    // If true, character can consume blood/meat to restore health
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bCanSustainViaBloodDrain = false;

    // If true, siring/turning another character permanently drains this character's Max HP (Sharing eternal life)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    bool bSiringDrainsMaxHP = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Transformation|Rules")
    float SiringMaxHPPenaltyAmount = 15.0f;
};

// Wrapper for GameState Master Registry Map
USTRUCT(BlueprintType)
struct FTagTransformationPair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FDynamicTransformationRule ValueDef;
};

USTRUCT(BlueprintType)
struct FDynamicEvolutionBranch
{
    GENERATED_BODY()

    // Unique identifier for this evolutionary path (e.g. Evolution.Goblin.ToHobgoblin)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag EvolutionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FDynamicLocalizationProfile Localization;

    // Target Character Template Tag to mutate into (e.g. Character.Monster.Goblin.Hobgoblin)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag TargetCharacterTemplateTag;

    // Optional ConditionTree evaluated on the character (e.g. checking Experience >= 1000 or DaysWithoutEatingFlesh >= 5)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FName PrerequisiteConditionTreeName = NAME_None;

    // Direct attribute milestone checks required (e.g. Attribute.Stat.Level >= 5.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag RequiredAttributeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    float RequiredAttributeMinimumValue = 0.0f;

    // Stat bonuses or attribute resets applied upon successful evolution
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    TArray<FDynamicModifierDef> EvolutionStatModifiers;

    // New AI Agent Profile applied upon evolution (e.g. switching from scavenger to berserker AI)
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

USTRUCT(BlueprintType)
struct FTagEvolutionPair
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag KeyTag;

    UPROPERTY()
    FDynamicEvolutionContainer ValueContainer;
};

// --- ROUTING MAP PAIRS ---
USTRUCT(BlueprintType) struct FTagAbilityDefPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicAbilityDef ValueDef; };
USTRUCT(BlueprintType) struct FTagDeliveryArchetypePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicDeliveryArchetype ValueArchetype; };
USTRUCT(BlueprintType) struct FNameConditionPair { GENERATED_BODY() UPROPERTY() FName KeyName; UPROPERTY() FDynamicConditionTreeContainer ValueNode; };
USTRUCT(BlueprintType) struct FTagMilestonePair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicMilestoneContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagThresholdPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicThresholdContainer ValueContainer; };
USTRUCT(BlueprintType) struct FTagEffectPair { GENERATED_BODY() UPROPERTY() FGameplayTag KeyTag; UPROPERTY() FDynamicEffectDef ValueDef; };