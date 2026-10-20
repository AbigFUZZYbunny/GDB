// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "DynamicPhysicsActor.h"
#include "DynamicEffectFactory.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
    ReplicatedEnvironmentGrid.OwningGameState = this;
}

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicGameState, ReplicatedEnvironmentGrid);
}

void FEnvironmentGridCellItem::PostReplicatedAdd(const FFastEnvironmentGridArray& InArrayOwner)
{
    if (InArrayOwner.OwningGameState)
    {
        InArrayOwner.OwningGameState->RecalculateCellPhysicalMetrics(*this);
    }
}

void FEnvironmentGridCellItem::PostReplicatedChange(const FFastEnvironmentGridArray& InArrayOwner)
{
    if (InArrayOwner.OwningGameState)
    {
        InArrayOwner.OwningGameState->RecalculateCellPhysicalMetrics(*this);
    }
}

void ADynamicGameState::SetupDynamicEnvironment(const FDynamicEnvironmentSystemConfig& InConfig)
{
    EnvironmentConfig = InConfig;
    TrackedElementsMap.Empty(EnvironmentConfig.TrackedElementalTags.Num());

    for (int32 i = 0; i < EnvironmentConfig.TrackedElementalTags.Num(); ++i)
    {
        const FGameplayTag& Tag = EnvironmentConfig.TrackedElementalTags[i];
        if (Tag.IsValid())
        {
            TrackedElementsMap.Add(Tag, i);
        }
    }

    for (FDynamicEnvironmentPhysicalMetricFormula& Formula : EnvironmentConfig.MetricFormulas)
    {
        Formula.ElementWeights.SyncMapFromChangesArray();
    }

    if (HasAuthority())
    {
        const int32 TotalCells = GridWidth * GridHeight;
        ReplicatedEnvironmentGrid.Items.SetNum(TotalCells);

        const int32 NumElements = EnvironmentConfig.TrackedElementalTags.Num();
        const uint8 DefaultPackedVal = static_cast<uint8>(FMath::Clamp(
            FMath::RoundToInt((EnvironmentConfig.DefaultCellDensity / FMath::Max(1.0f, EnvironmentConfig.MaxElementDensityCap)) * 255.0f),
            0, 255
        ));

        for (int32 i = 0; i < TotalCells; ++i)
        {
            FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[i];
            Cell.CellIndex = i;
            Cell.PackedElementalDensities.Init(DefaultPackedVal, NumElements);
            RecalculateCellPhysicalMetrics(Cell);
        }
        ReplicatedEnvironmentGrid.MarkArrayDirty();
    }
}

UGameplayEffect* ADynamicGameState::GetOrCreateCachedEffectObject(FGameplayTag EffectTag)
{
    if (TObjectPtr<UGameplayEffect>* Found = DynamicEffectCDOCache.Find(EffectTag))
    {
        return Found->Get();
    }

    if (const FDynamicEffectDef* FoundDef = MasterEffectRegistry.Find(EffectTag))
    {
        UGameplayEffect* NewEffect = UDynamicEffectFactory::ConstructGameplayEffectObject(this, *FoundDef, EffectTag);
        DynamicEffectCDOCache.Add(EffectTag, NewEffect);
        return NewEffect;
    }

    return nullptr;
}

void ADynamicGameState::WarmGameplayEffectCache()
{
    DynamicEffectCDOCache.Empty(MasterEffectRegistry.Num());
    for (const auto& Pair : MasterEffectRegistry)
    {
        GetOrCreateCachedEffectObject(Pair.Key);
    }
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
    if (ReplicatedEnvironmentGrid.Items.Num() == 0 || !MetricOrElementTag.IsValid()) return 0.0f;
    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    const FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[CellIdx];

    // Check if querying an elemental density directly
    const int32 ElemIdx = ResolveElementArrayIndex(MetricOrElementTag);
    if (ElemIdx != INDEX_NONE && Cell.PackedElementalDensities.IsValidIndex(ElemIdx))
    {
        return (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
    }

    // Check derived physical metrics calculated by JSON formulas
    return Cell.DerivedPhysicalMetrics.LocalLookupMap.FindRef(MetricOrElementTag);
}

float ADynamicGameState::ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount)
{
    if (!HasAuthority() || ReplicatedEnvironmentGrid.Items.Num() == 0) return 0.0f;

    const int32 ElemIdx = ResolveElementArrayIndex(ElementTag);
    if (ElemIdx == INDEX_NONE) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[CellIdx];

    if (!Cell.PackedElementalDensities.IsValidIndex(ElemIdx)) return 0.0f;

    const float OldValue = (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
    const float NewValue = FMath::Clamp(OldValue + DeltaAmount, 0.0f, EnvironmentConfig.MaxElementDensityCap);

    Cell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewValue / EnvironmentConfig.MaxElementDensityCap) * 255.0f));
    const float ActualDelta = NewValue - OldValue;

    RecalculateCellPhysicalMetrics(Cell);
    ReplicatedEnvironmentGrid.MarkItemDirty(Cell);
    return ActualDelta;
}

void ADynamicGameState::RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell)
{
    // Evaluates dynamic metric formulas from external JSON
    for (const FDynamicEnvironmentPhysicalMetricFormula& Formula : EnvironmentConfig.MetricFormulas)
    {
        float EvaluatedValue = Formula.BaseConstant;

        for (const auto& WeightPair : Formula.ElementWeights.LocalLookupMap)
        {
            const int32 Idx = ResolveElementArrayIndex(WeightPair.Key);
            if (Idx != INDEX_NONE && Cell.PackedElementalDensities.IsValidIndex(Idx))
            {
                const float Density = (static_cast<float>(Cell.PackedElementalDensities[Idx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
                EvaluatedValue += Density * WeightPair.Value;
            }
        }

        const float Clamped = FMath::Clamp(EvaluatedValue, Formula.MinClamp, Formula.MaxClamp);
        Cell.DerivedPhysicalMetrics.UpdateEntry(Formula.MetricTag, Clamped);
    }
}

void ADynamicGameState::ServerTickEnvironmentSimulation(float ElapsedGameHours)
{
    if (!HasAuthority() || ReplicatedEnvironmentGrid.Items.Num() == 0) return;
    const int32 TotalCells = ReplicatedEnvironmentGrid.Items.Num();
    const int32 NumElements = EnvironmentConfig.TrackedElementalTags.Num();

    TArray<TArray<float>> AdvectionDeltas;
    AdvectionDeltas.SetNum(NumElements);
    for (int32 e = 0; e < NumElements; ++e)
    {
        AdvectionDeltas[e].Init(0.0f, TotalCells);
    }

    // 1. Data-Driven Dynamic Advection Pass
    for (int32 i = 0; i < TotalCells; ++i)
    {
        const FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[i];
        const int32 X = i % GridWidth;
        const int32 Y = i / GridWidth;
        const int32 StepX = FMath::RoundToInt(Cell.WindDirection.X);
        const int32 StepY = FMath::RoundToInt(Cell.WindDirection.Y);
        const int32 TargetX = FMath::Clamp(X + StepX, 0, GridWidth - 1);
        const int32 TargetY = FMath::Clamp(Y + StepY, 0, GridHeight - 1);
        const int32 TargetIdx = (TargetY * GridWidth) + TargetX;

        if (TargetIdx != i)
        {
            const float TransferRatio = FMath::Clamp(0.15f * ElapsedGameHours, 0.0f, 0.35f);
            for (int32 e = 0; e < NumElements; ++e)
            {
                if (Cell.PackedElementalDensities.IsValidIndex(e))
                {
                    const float Density = (static_cast<float>(Cell.PackedElementalDensities[e]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
                    const float DensityMoved = Density * TransferRatio;
                    AdvectionDeltas[e][i]         -= DensityMoved;
                    AdvectionDeltas[e][TargetIdx] += DensityMoved;
                }
            }
        }
    }

    // 2. Cell Update Pass (Homeostasis & Biome Shifting)
    for (int32 i = 0; i < TotalCells; ++i)
    {
        FEnvironmentGridCellItem& Cell = ReplicatedEnvironmentGrid.Items[i];
        bool bCellModified = false;

        for (int32 e = 0; e < NumElements; ++e)
        {
            if (Cell.PackedElementalDensities.IsValidIndex(e) && !FMath::IsNearlyZero(AdvectionDeltas[e][i]))
            {
                const float CurrentDensity = (static_cast<float>(Cell.PackedElementalDensities[e]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
                const float NewDensity = FMath::Clamp(CurrentDensity + AdvectionDeltas[e][i], 0.0f, EnvironmentConfig.MaxElementDensityCap);
                Cell.PackedElementalDensities[e] = static_cast<uint8>(FMath::RoundToInt((NewDensity / EnvironmentConfig.MaxElementDensityCap) * 255.0f));
                bCellModified = true;
            }
        }

        // Homeostasis pull towards Biome Equilibrium
        if (const FDynamicBiomeDef* BiomeDef = BiomeMasterRegistry.Find(Cell.CurrentBiomeTag))
        {
            float MaxDeviation = 0.0f;
            for (const FTagFloatPair& Eq : BiomeDef->DefaultElementalEquilibrium)
            {
                const int32 ElemIdx = ResolveElementArrayIndex(Eq.KeyTag);
                if (ElemIdx != INDEX_NONE && Cell.PackedElementalDensities.IsValidIndex(ElemIdx))
                {
                    const float CurrentVal = (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
                    MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(CurrentVal - Eq.Value));

                    const float NewVal = FMath::FInterpConstantTo(CurrentVal, Eq.Value, ElapsedGameHours, BiomeDef->HomeostasisPullRate);
                    if (!FMath::IsNearlyEqual(CurrentVal, NewVal, 0.5f))
                    {
                        Cell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewVal / EnvironmentConfig.MaxElementDensityCap) * 255.0f));
                        bCellModified = true;
                    }
                }
            }

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
            RecalculateCellPhysicalMetrics(Cell);
            ReplicatedEnvironmentGrid.MarkItemDirty(Cell);
        }
    }
}

// Universal Property Reflection Helper
float ADynamicGameState::ExtractReflectedNumericProperty(const void* StructMemory, const UScriptStruct* StructType, FName PropertyPath)
{
    if (!StructMemory || !StructType || PropertyPath.IsNone()) return 0.0f;

    // Splits sub-paths (e.g., "LifeCycleRequirements.BaseResourceCost" or "SpatialDelivery.SpeedOrExpansionRate")
    FString LeftStr, RightStr;
    const FString FullPath = PropertyPath.ToString();

    if (FullPath.Split(TEXT("."), &LeftStr, &RightStr))
    {
        const FName OuterPropName(*LeftStr);
        if (FProperty* Prop = StructType->FindPropertyByName(OuterPropName))
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                const void* SubStructAddress = StructProp->ContainerPtrToValuePtr<void>(StructMemory);
                return ExtractReflectedNumericProperty(SubStructAddress, StructProp->Struct, *RightStr);
            }
        }
    }
    else
    {
        if (FProperty* Prop = StructType->FindPropertyByName(PropertyPath))
        {
            if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Prop))
            {
                const void* ValuePtr = NumericProp->ContainerPtrToValuePtr<void>(StructMemory);
                if (NumericProp->IsFloatingPoint())
                {
                    return static_cast<float>(NumericProp->GetFloatingPointPropertyValue(ValuePtr));
                }
                else if (NumericProp->IsInteger())
                {
                    return static_cast<float>(NumericProp->GetSignedIntPropertyValue(ValuePtr));
                }
            }
        }
    }

    return 0.0f;
}

float ADynamicGameState::ExtractAbilityPropertyForScoring(const FDynamicAbilityDef& AbilityDef, const FAbilityPropertyScoreRule& Rule) const
{
    // 1. Dynamic MetricTag Query: searches any dynamic map wrapper
    if (Rule.MetricTag.IsValid())
    {
        if (const float* FoundNum = AbilityDef.NumericParams.LocalLookupMap.Find(Rule.MetricTag)) return *FoundNum;
        if (const float* FoundDel = AbilityDef.SpatialDelivery.RuntimeMetricModifiers.LocalLookupMap.Find(Rule.MetricTag)) return *FoundDel;
        if (const float* FoundMov = AbilityDef.MovementConfig.MovementMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundMov;
        if (const float* FoundInt = AbilityDef.InteractionConfig.InteractionMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundInt;
    }

    // 2. Dynamic Property Reflection: extracts any native struct variable without hardcoded strings
    if (!Rule.PropertyName.IsNone())
    {
        return ExtractReflectedNumericProperty(&AbilityDef, FDynamicAbilityDef::StaticStruct(), Rule.PropertyName);
    }

    return 0.0f;
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
    GlobalLimbs = ConfigData.Limbs;
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