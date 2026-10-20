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

int32 ADynamicGameState::ResolveElementArrayIndex(FGameplayTag ElementTag) const
{
    const FString TagStr = ElementTag.ToString();
    if (TagStr.EndsWith(TEXT("Fire")))  return 0;
    if (TagStr.EndsWith(TEXT("Water"))) return 1;
    if (TagStr.EndsWith(TEXT("Earth"))) return 2;
    if (TagStr.EndsWith(TEXT("Air")))   return 3;
    if (TagStr.EndsWith(TEXT("Light"))) return 4;
    if (TagStr.EndsWith(TEXT("Dark")))  return 5;
    return -1;
}

int32 ADynamicGameState::GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const
{
    if (ReplicatedEnvironmentGrid.Items.Num() == 0 || CellSizeWorldUnits <= 0.0f) return 0;

    const float HalfWorldW = (GridWidth * CellSizeWorldUnits) * 0.5f;
    const float HalfWorldH = (GridHeight * CellSizeWorldUnits) * 0.5f;

    const int32 X = FMath::Clamp(FMath::FloorToInt((WorldLocation.X + HalfWorldW) / CellSizeWorldUnits), 0, GridWidth - 1);
    const int32 Y = FMath::Clamp(FMath::FloorToInt((WorldLocation.Y + HalfWorldH) / CellSizeWorldUnits), 0, GridHeight - 1);

    return FMath::Clamp((Y * GridWidth) + X, 0, ReplicatedEnvironmentGrid.Items.Num() - 1);
}

float ADynamicGameState::GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const
{
    if (ReplicatedEnvironmentGrid.Items.Num() == 0) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    const FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[CellIdx];

    const int32 ElemIdx = ResolveElementArrayIndex(MetricOrElementTag);
    if (ElemIdx != -1)
    {
        return Cell.ElementalDensities[ElemIdx];
    }

    const FString MetricName = MetricOrElementTag.ToString();
    if (MetricName.EndsWith(TEXT("Temperature")))    return Cell.TemperatureCelsius;
    if (MetricName.EndsWith(TEXT("Wetness")))        return Cell.Wetness;
    if (MetricName.EndsWith(TEXT("WindSpeed")))      return Cell.WindSpeed;
    if (MetricName.EndsWith(TEXT("GroundMoisture"))) return Cell.GroundMoisture;
    if (MetricName.EndsWith(TEXT("Sunlight")))       return Cell.ElementalDensities[4]; // Light element

    return 0.0f;
}

float ADynamicGameState::ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount)
{
    if (!HasAuthority() || ReplicatedEnvironmentGrid.Items.Num() == 0) return 0.0f;

    const int32 ElemIdx = ResolveElementArrayIndex(ElementTag);
    if (ElemIdx == -1) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[CellIdx];

    const float OldValue = Cell.ElementalDensities[ElemIdx];
    Cell.ElementalDensities[ElemIdx] = FMath::Clamp(OldValue + DeltaAmount, 0.0f, 200.0f);
    const float ActualDelta = Cell.ElementalDensities[ElemIdx] - OldValue;

    RecalculateCellPhysicalMetrics(Cell, 1.0f);
    ReplicatedEnvironmentGrid.MarkItemDirty(Cell);

    return ActualDelta;
}

void ADynamicGameState::RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell, float SunLightFactor)
{
    if (Cell.ElementalDensities.Num() < 6)
    {
        Cell.ElementalDensities.Init(50.0f, 6);
    }

    const float Fire  = Cell.ElementalDensities[0];
    const float Water = Cell.ElementalDensities[1];
    const float Earth = Cell.ElementalDensities[2];
    const float Air   = Cell.ElementalDensities[3];
    const float Light = Cell.ElementalDensities[4] * SunLightFactor;
    const float Dark  = Cell.ElementalDensities[5];

    Cell.TemperatureCelsius = -15.0f + (Fire * 0.65f) + (Light * 0.25f) - (Water * 0.20f) - (Air * 0.15f) - (Dark * 0.20f);
    Cell.WindSpeed = (Air * 0.8f) + FMath::Abs(Fire - Water) * 0.25f;
    Cell.Wetness = FMath::Clamp((Water * 0.85f) + (Cell.GroundMoisture * 0.25f) - (Fire * 0.35f), 0.0f, 100.0f);
}

void ADynamicGameState::ServerTickEnvironmentSimulation(float ElapsedGameHours)
{
    if (!HasAuthority() || ReplicatedEnvironmentGrid.Items.Num() == 0) return;

    const int32 TotalCells = ReplicatedEnvironmentGrid.Items.Num();

    // Buffer to accumulate advected Air & Water moving across cells (so a storm sweeps across a city)
    TArray<float> AdvectedWaterDelta;
    TArray<float> AdvectedAirDelta;
    AdvectedWaterDelta.Init(0.0f, TotalCells);
    AdvectedAirDelta.Init(0.0f, TotalCells);

    // 1. Advection Pass: Wind pushes atmospheric Water and Air into neighboring downwind cells
    for (int32 i = 0; i < TotalCells; ++i)
    {
        const FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[i];
        if (Cell.WindSpeed <= 5.0f || Cell.ElementalDensities[1] <= 10.0f) continue;

        const int32 X = i % GridWidth;
        const int32 Y = i / GridWidth;

        const int32 StepX = FMath::RoundToInt(Cell.WindDirection.X);
        const int32 StepY = FMath::RoundToInt(Cell.WindDirection.Y);

        const int32 TargetX = FMath::Clamp(X + StepX, 0, GridWidth - 1);
        const int32 TargetY = FMath::Clamp(Y + StepY, 0, GridHeight - 1);
        const int32 TargetIdx = (TargetY * GridWidth) + TargetX;

        if (TargetIdx != i)
        {
            // Transfer a fraction of atmospheric Water & Air downwind
            const float TransferRatio = FMath::Clamp((Cell.WindSpeed / 100.0f) * 0.25f * ElapsedGameHours, 0.0f, 0.45f);
            const float WaterMoved = Cell.ElementalDensities[1] * TransferRatio;
            const float AirMoved   = Cell.ElementalDensities[3] * (TransferRatio * 0.5f);

            AdvectedWaterDelta[i]         -= WaterMoved;
            AdvectedWaterDelta[TargetIdx] += WaterMoved;
            AdvectedAirDelta[i]           -= AirMoved;
            AdvectedAirDelta[TargetIdx]   += AirMoved;
        }
    }

    // 2. Cell Update Pass: Precipitation, Biome Homeostasis, and Biome Shifting
    for (int32 i = 0; i < TotalCells; ++i)
    {
        FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[i];
        bool bCellModified = false;

        if (!FMath::IsNearlyZero(AdvectedWaterDelta[i]) || !FMath::IsNearlyZero(AdvectedAirDelta[i]))
        {
            Cell.ElementalDensities[1] = FMath::Clamp(Cell.ElementalDensities[1] + AdvectedWaterDelta[i], 0.0f, 200.0f);
            Cell.ElementalDensities[3] = FMath::Clamp(Cell.ElementalDensities[3] + AdvectedAirDelta[i], 0.0f, 200.0f);
            bCellModified = true;
        }

        // Precipitation: When Air & Water are high, rain/snow falls, transferring Water from Air to Ground/Earth
        if (Cell.ElementalDensities[1] > 65.0f && Cell.ElementalDensities[3] > 50.0f)
        {
            const float RainRate = 8.0f * ElapsedGameHours;
            Cell.ElementalDensities[1] = FMath::Max(0.0f, Cell.ElementalDensities[1] - RainRate); // Deplete atmospheric water
            Cell.GroundMoisture = FMath::Clamp(Cell.GroundMoisture + RainRate, 0.0f, 150.0f);
            Cell.ElementalDensities[2] = FMath::Clamp(Cell.ElementalDensities[2] + (RainRate * 0.2f), 0.0f, 200.0f); // Nourish Earth
            bCellModified = true;
        }

        // Biome Homeostasis & Terraforming Shift Check
        if (const FDynamicBiomeDef* BiomeDef = BiomeMasterRegistry.Find(Cell.CurrentBiomeTag))
        {
            float MaxDeviation = 0.0f;
            for (const FTagFloatPair& Eq : BiomeDef->DefaultElementalEquilibrium)
            {
                const int32 ElemIdx = ResolveElementArrayIndex(Eq.KeyTag);
                if (ElemIdx != -1)
                {
                    const float CurrentVal = Cell.ElementalDensities[ElemIdx];
                    MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(CurrentVal - Eq.Value));

                    // Pull toward Biome Equilibrium
                    const float NewVal = FMath::FInterpConstantTo(CurrentVal, Eq.Value, ElapsedGameHours, BiomeDef->HomeostasisPullRate);
                    if (!FMath::IsNearlyEqual(CurrentVal, NewVal, 0.1f))
                    {
                        Cell.ElementalDensities[ElemIdx] = NewVal;
                        bCellModified = true;
                    }
                }
            }

            // If elements are forced far out of equilibrium for an extended period, shift the biome
            if (MaxDeviation > 35.0f)
            {
                Cell.SustainedOutOfRangeHours += ElapsedGameHours;
                for (const FBiomeShiftRule& ShiftRule : BiomeDef->BiomeShiftRules)
                {
                    if (Cell.SustainedOutOfRangeHours >= ShiftRule.RequiredSustainedGameHours)
                    {
                        Cell.TargetShiftBiomeTag = ShiftRule.TargetBiomeTag;
                        Cell.BiomeBlendAlpha = FMath::Clamp(Cell.BiomeBlendAlpha + (ElapsedGameHours / ShiftRule.RequiredSustainedGameHours), 0.0f, 1.0f);
                        if (Cell.BiomeBlendAlpha >= 1.0f)
                        {
                            Cell.CurrentBiomeTag = ShiftRule.TargetBiomeTag;
                            Cell.BiomeBlendAlpha = 0.0f;
                            Cell.SustainedOutOfRangeHours = 0.0f;
                        }
                        bCellModified = true;
                        break;
                    }
                }
            }
            else
            {
                Cell.SustainedOutOfRangeHours = FMath::Max(0.0f, Cell.SustainedOutOfRangeHours - ElapsedGameHours);
            }
        }

        if (bCellModified)
        {
            RecalculateCellPhysicalMetrics(Cell, 1.0f);
            ReplicatedEnvironmentGrid.MarkItemDirty(Cell);
        }
    }
}

// --- Fast Array Callbacks for FEnvironmentGridCellItem ---
void FEnvironmentGridCellItem::PreReplicatedRemove(const FFastEnvironmentGridArray& InArrayOwner)
{
}

void FEnvironmentGridCellItem::PostReplicatedAdd(const FFastEnvironmentGridArray& InArrayOwner)
{
}

void FEnvironmentGridCellItem::PostReplicatedChange(const FFastEnvironmentGridArray& InArrayOwner)
{
}

// --- ADynamicGameState Replication Setup ---
void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ADynamicGameState, ReplicatedEnvironmentGrid);
}