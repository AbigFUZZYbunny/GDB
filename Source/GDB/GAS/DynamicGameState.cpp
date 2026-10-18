// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "DynamicPhysicsActor.h"
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
    DOREPLIFETIME(ADynamicGameState, MasterPhysicsActorRegistryArr);
    DOREPLIFETIME(ADynamicGameState, ReplicatedLimbs);
    DOREPLIFETIME(ADynamicGameState, AbilityScoreConfig);
}

void ADynamicGameState::RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !AbilityTag.IsValid()) return;
    AbilityMasterRegistry.Add(AbilityTag, AbilityDef);

    FTagAbilityDefPair NewAbPair;
    NewAbPair.KeyTag = AbilityTag;
    NewAbPair.ValueDef = AbilityDef;
    AbilityMasterRegistryArr.Add(NewAbPair);

    for (const auto& CondPair : AssociatedConditions)
    {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair;
        NewCondPair.KeyName = CondPair.Key;
        NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
    RebuildAllAbilityScores();
}

void ADynamicGameState::RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !EffectTag.IsValid()) return;
    MasterEffectRegistry.Add(EffectTag, EffectDef);

    FTagEffectPair NewEffectPair;
    NewEffectPair.KeyTag = EffectTag;
    NewEffectPair.ValueDef = EffectDef;
    MasterEffectRegistryArr.Add(NewEffectPair);

    for (const auto& CondPair : AssociatedConditions)
    {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair;
        NewCondPair.KeyName = CondPair.Key;
        NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

void ADynamicGameState::RegisterNewDynamicPhysicsActorDef(FGameplayTag ArchetypeTag, FDynamicPhysicsActorDef ActorDef)
{
    if (!HasAuthority() || !ArchetypeTag.IsValid()) return;
    MasterPhysicsActorRegistry.Add(ArchetypeTag, ActorDef);

    if (ActorDef.CraftingRecipes.Num() > 0)
    {
        FDynamicRecipeList List;
        List.Recipes = ActorDef.CraftingRecipes;
        RecipeMasterRegistry.Add(ArchetypeTag, List);
    }

    FTagPhysicsActorDefPair NewPair;
    NewPair.KeyTag = ArchetypeTag;
    NewPair.ValueDef = ActorDef;
    MasterPhysicsActorRegistryArr.Add(NewPair);
}

void ADynamicGameState::SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig)
{
    AbilityScoreConfig = InScoreConfig;
    AbilityScoreConfig.ExecutionTypeWeights.SyncMapFromChangesArray();
    RebuildAllAbilityScores();
}

void ADynamicGameState::SetActiveDifficultyTag(FGameplayTag NewDifficultyTag)
{
    if (!HasAuthority() || !NewDifficultyTag.IsValid()) return;
    AbilityScoreConfig.ActiveDifficultyTag = NewDifficultyTag;
    RebuildAllAbilityScores();
}

void ADynamicGameState::OnRep_AbilityScoreConfig()
{
    AbilityScoreConfig.ExecutionTypeWeights.SyncMapFromChangesArray();
    RebuildAllAbilityScores();
}

void ADynamicGameState::RebuildAllAbilityScores()
{
    ComputedAbilityScoresCache.Empty(AbilityMasterRegistry.Num());
    for (const auto& Pair : AbilityMasterRegistry)
    {
        ComputedAbilityScoresCache.Add(Pair.Key, EvaluateAbilityScore(Pair.Key, Pair.Value));
    }
}

bool ADynamicGameState::GetComputedAbilityScore(FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const
{
    if (const FComputedAbilityScoreInfo* Found = ComputedAbilityScoresCache.Find(AbilityTag))
    {
        OutScoreInfo = *Found;
        return true;
    }
    if (const FDynamicAbilityDef* Def = AbilityMasterRegistry.Find(AbilityTag))
    {
        OutScoreInfo = EvaluateAbilityScore(AbilityTag, *Def);
        return true;
    }
    return false;
}

float ADynamicGameState::ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const
{
    if (Rule.MetricTag.IsValid())
    {
        if (const float* FoundNum = AbilityDef.NumericParams.LocalLookupMap.Find(Rule.MetricTag)) return *FoundNum;
        if (const float* FoundDel = AbilityDef.SpatialDelivery.RuntimeMetricModifiers.LocalLookupMap.Find(Rule.MetricTag)) return *FoundDel;
        if (const float* FoundMov = AbilityDef.MovementConfig.MovementMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundMov;
        if (const float* FoundInt = AbilityDef.InteractionConfig.InteractionMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundInt;
    }
    if (Rule.PropertyName == TEXT("BaseCooldown"))       return AbilityDef.LifeCycleRequirements.BaseCooldownDuration;
    if (Rule.PropertyName == TEXT("BaseCost"))           return AbilityDef.LifeCycleRequirements.BaseResourceCost;
    if (Rule.PropertyName == TEXT("MaxChargeTime"))      return AbilityDef.ChargeProfile.MaxChargeTime;
    if (Rule.PropertyName == TEXT("DirectMagnitude"))    return FMath::Abs(AbilityDef.SpatialDelivery.DirectAttributeMagnitude);
    if (Rule.PropertyName == TEXT("MaxRadius"))          return AbilityDef.SpatialDelivery.MaxRadius;
    if (Rule.PropertyName == TEXT("Duration"))           return AbilityDef.SpatialDelivery.Duration;
    if (Rule.PropertyName == TEXT("Speed"))              return AbilityDef.SpatialDelivery.SpeedOrExpansionRate;
    if (Rule.PropertyName == TEXT("SpawnCount"))         return static_cast<float>(AbilityDef.SpatialDelivery.SpawnCount);
    if (Rule.PropertyName == TEXT("KineticImpulse"))     return AbilityDef.SpatialDelivery.KineticImpulseOnHit;
    if (Rule.PropertyName == TEXT("TickInterval"))       return AbilityDef.SpatialDelivery.TickInterval;
    if (Rule.PropertyName == TEXT("MaxBounces"))         return static_cast<float>(AbilityDef.ChainConfig.MaxBounces);
    if (Rule.PropertyName == TEXT("BounceSearchRadius")) return AbilityDef.ChainConfig.BounceSearchRadius;
    return 0.0f;
}

FComputedAbilityScoreInfo ADynamicGameState::EvaluateAbilityScore(FGameplayTag AbilityTag, const FDynamicAbilityDef& AbilityDef) const
{
    FComputedAbilityScoreInfo Result;
    Result.AbilityTag = AbilityTag;
    float RawScore = 10.0f;

    for (const FAbilityPropertyScoreRule& Rule : AbilityScoreConfig.PropertyRules)
    {
        const float RawVal = ExtractAbilityPropertyForScoring(AbilityDef, Rule);
        if (Rule.ScalingMode == EAbilityScoreScalingMode::DirectProportional)
        {
            if (RawVal > 0.0f)
            {
                const float Normalized = RawVal / FMath::Max(0.001f, Rule.ReferenceBaseline);
                RawScore += Rule.WeightScalar * FMath::Pow(Normalized, Rule.Exponent);
            }
        }
        else
        {
            const float SafeVal = FMath::Max(Rule.MinClampedValue, RawVal);
            const float InverseRatio = FMath::Max(0.001f, Rule.ReferenceBaseline) / SafeVal;
            RawScore += Rule.WeightScalar * FMath::Pow(InverseRatio, Rule.Exponent);
        }
    }
    Result.RawAbilityScore = FMath::Max(1.0f, RawScore);

    FAbilityScoreDifficultyProfile ActiveDiff;
    for (const FAbilityScoreDifficultyProfile& Diff : AbilityScoreConfig.DifficultyProfiles)
    {
        if (Diff.DifficultyTag == AbilityScoreConfig.ActiveDifficultyTag)
        {
            ActiveDiff = Diff;
            break;
        }
    }
    Result.DifficultyAdjustedScore = Result.RawAbilityScore * ActiveDiff.GlobalScoreMultiplier;
    const float ScoreRange = FMath::Max(1.0f, ActiveDiff.MaxScoreForLevel10 - ActiveDiff.MinScoreForLevel1);
    const float LevelAlpha = FMath::Clamp((Result.DifficultyAdjustedScore - ActiveDiff.MinScoreForLevel1) / ScoreRange, 0.0f, 1.0f);
    Result.ComputedSpellLevel = FMath::Clamp(1 + FMath::RoundToInt(LevelAlpha * 9.0f), 1, 10);
    Result.LearnPointCost = FMath::Max(1.0f, static_cast<float>(FMath::RoundToInt(Result.DifficultyAdjustedScore * ActiveDiff.LearnPointCostPerScoreUnit)));
    Result.MinimumMasteryRequired = FMath::Max(0.0f, Result.DifficultyAdjustedScore * ActiveDiff.MinMasteryRequiredPerScoreUnit);
    return Result;
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

ADynamicPhysicsActor* ADynamicGameState::AcquirePhysicsActor(FGameplayTag ArchetypeTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn)
{
    if (!HasAuthority() || !ArchetypeTag.IsValid()) return nullptr;

    while (InactivePhysicsActorPool.Num() > 0)
    {
        ADynamicPhysicsActor* PooledActor = InactivePhysicsActorPool.Pop(EAllowShrinking::No);
        if (IsValid(PooledActor))
        {
            PooledActor->SetOwner(OwnerActor);
            PooledActor->SetInstigator(InstigatorPawn);
            PooledActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
            PooledActor->InitializeFromArchetype(ArchetypeTag);
            return PooledActor;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = OwnerActor;
    SpawnParams.Instigator = InstigatorPawn;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    ADynamicPhysicsActor* NewActor = GetWorld()->SpawnActor<ADynamicPhysicsActor>(ADynamicPhysicsActor::StaticClass(), SpawnTransform, SpawnParams);
    if (NewActor)
    {
        NewActor->InitializeFromArchetype(ArchetypeTag);
    }
    return NewActor;
}

void ADynamicGameState::ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn)
{
    if (!HasAuthority() || !IsValid(ActorToReturn)) return;
    InactivePhysicsActorPool.AddUnique(ActorToReturn);
}

void ADynamicGameState::SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData)
{
    CalculationSettings = ConfigData.CalculationSettings;
    ReplicatedLimbs = ConfigData.Limbs;
    RebuildLimbMapsFromTransport();
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

const FPhysicsJointNodeConfig* ADynamicGameState::GetBoneJointData(const FName& BoneName) const
{
    return BoneToJointMap.Find(BoneName);
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

void ADynamicGameState::OnRep_MasterPhysicsActorRegistryArr()
{
    MasterPhysicsActorRegistry.Empty(MasterPhysicsActorRegistryArr.Num());
    RecipeMasterRegistry.Empty();

    for (FTagPhysicsActorDefPair& Element : MasterPhysicsActorRegistryArr)
    {
        Element.ValueDef.Localization.SyncMapFromChangesArray();
        Element.ValueDef.DefaultGlobalAttributes.SyncMapFromChangesArray();

        for (FPhysicsLimbConfig& Limb : Element.ValueDef.CustomLimbs)
        {
            Limb.Localization.SyncMapFromChangesArray();
            Limb.DefaultLimbAttributes.SyncMapFromChangesArray();
            Limb.AngularMotorAttributeScalars.SyncMapFromChangesArray();
            Limb.LinearMotorAttributeScalars.SyncMapFromChangesArray();
        }

        MasterPhysicsActorRegistry.Add(Element.KeyTag, Element.ValueDef);

        if (Element.ValueDef.CraftingRecipes.Num() > 0)
        {
            FDynamicRecipeList List;
            List.Recipes = Element.ValueDef.CraftingRecipes;
            RecipeMasterRegistry.Add(Element.KeyTag, List);
        }
    }
}

void ADynamicGameState::OnRep_AbilityMasterRegistryArr()
{
    AbilityMasterRegistry.Empty();
    for (auto& Element : AbilityMasterRegistryArr)
    {
        Element.ValueDef.NumericParams.SyncMapFromChangesArray();
        Element.ValueDef.SpatialDelivery.RuntimeMetricModifiers.SyncMapFromChangesArray();
        Element.ValueDef.InteractionConfig.InteractionMetrics.SyncMapFromChangesArray();
        Element.ValueDef.MovementConfig.MovementMetrics.SyncMapFromChangesArray();
        Element.ValueDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds.SyncMapFromChangesArray();
        AbilityMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
    }
    RebuildAllAbilityScores();
}

void ADynamicGameState::OnRep_ReplicatedLimbs()
{
    RebuildLimbMapsFromTransport();
}

void ADynamicGameState::RebuildLimbMapsFromTransport()
{
    LimbLookupMap.Empty(ReplicatedLimbs.Num());
    BoneToLimbMap.Empty();
    BoneToJointMap.Empty();
    for (FPhysicsLimbConfig& Limb : ReplicatedLimbs)
    {
        Limb.Localization.SyncMapFromChangesArray();
        Limb.DefaultLimbAttributes.SyncMapFromChangesArray();
        Limb.AngularMotorAttributeScalars.SyncMapFromChangesArray();
        Limb.LinearMotorAttributeScalars.SyncMapFromChangesArray();
        for (const FPhysicsJointNodeConfig& Joint : Limb.JointChain)
        {
            if (!Joint.BoneName.IsNone())
            {
                BoneToLimbMap.Add(Joint.BoneName, Limb.LimbId);
                BoneToJointMap.Add(Joint.BoneName, Joint);
            }
        }
        LimbLookupMap.Add(Limb.LimbId, Limb);
    }
}