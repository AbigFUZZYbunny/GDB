// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "DynamicPhysicsActor.h"
#include "Engine/World.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
}

void ADynamicGameState::SetAbilityScoreConfig(const FAbilityScoreConfigData& InScoreConfig)
{
    AbilityScoreConfig = InScoreConfig;
    AbilityScoreConfig.ExecutionTypeWeights.SyncMapFromChangesArray();
    RebuildAllAbilityScores();
}

void ADynamicGameState::SetActiveDifficultyTag(FGameplayTag NewDifficultyTag)
{
    AbilityScoreConfig.ActiveDifficultyTag = NewDifficultyTag;
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

// ... (EvaluateAbilityScore and ExtractAbilityPropertyForScoring remain unchanged computationally) ...

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
    GlobalLimbs = ConfigData.Limbs;
    RebuildLimbMapsFromTransport();
}

uint8 ADynamicGameState::GetLimbIdForBone(const FName& BoneName) const
{
    if (const uint8* FoundId = BoneToLimbMap.Find(BoneName)) return *FoundId;
    return 1; // Fallback to root limb
}

const FPhysicsLimbConfig* ADynamicGameState::GetLimbData(uint8 LimbId) const
{
    return LimbLookupMap.Find(LimbId);
}

const FPhysicsJointNodeConfig* ADynamicGameState::GetBoneJointData(const FName& BoneName) const
{
    return BoneToJointMap.Find(BoneName);
}

void ADynamicGameState::RebuildLimbMapsFromTransport()
{
    LimbLookupMap.Empty(GlobalLimbs.Num());
    BoneToLimbMap.Empty();
    BoneToJointMap.Empty();
    for (FPhysicsLimbConfig& Limb : GlobalLimbs)
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

bool ADynamicGameState::GetCharacterTemplate(FGameplayTag TemplateTag, FCharacterTemplateDef& OutTemplate) const
{
    if (const FCharacterTemplateDef* Found = CharacterTemplatesRegistry.Find(TemplateTag))
    {
        OutTemplate = *Found;
        return true;
    }
    return false;
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