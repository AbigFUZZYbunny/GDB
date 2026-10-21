// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "DynamicPhysicsActor.h"
#include "DynamicEffectFactory.h"
#include "DynamicEnvironmentChunkActor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
    // Environment grid is simulated on server and replicated through spatialized ADynamicEnvironmentChunkActor instances.
}

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // CRITICAL: ReplicatedEnvironmentGrid removed to eliminate global 10,000-cell array choke.
}

void ADynamicGameState::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        SpawnServerEnvironmentChunks();
    }
}

void ADynamicGameState::RegisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk)
{
    if (Chunk)
    {
        ActiveChunkMap.Add(Chunk->ChunkCoord, Chunk);
    }
}

void ADynamicGameState::UnregisterEnvironmentChunk(ADynamicEnvironmentChunkActor* Chunk)
{
    if (Chunk)
    {
        ActiveChunkMap.Remove(Chunk->ChunkCoord);
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
        ServerEnvironmentGrid.SetNum(TotalCells);
        const int32 NumElements = EnvironmentConfig.TrackedElementalTags.Num();
        const uint8 DefaultPackedVal = static_cast<uint8>(FMath::Clamp(
            FMath::RoundToInt((EnvironmentConfig.DefaultCellDensity / FMath::Max(1.0f, EnvironmentConfig.MaxElementDensityCap)) * 255.0f),
            0, 255
        ));

        for (int32 i = 0; i < TotalCells; ++i)
        {
            FEnvironmentGridCellItem& Cell = ServerEnvironmentGrid[i];
            Cell.CellIndex = i;
            Cell.PackedElementalDensities.Init(DefaultPackedVal, NumElements);
            RecalculateCellPhysicalMetrics(Cell);
        }

        SpawnServerEnvironmentChunks();
    }
}

void ADynamicGameState::SpawnServerEnvironmentChunks()
{
    if (!HasAuthority() || ServerEnvironmentGrid.Num() == 0 || ChunkDimension <= 0) return;

    const int32 ChunksX = FMath::DivideAndRoundUp(GridWidth, ChunkDimension);
    const int32 ChunksY = FMath::DivideAndRoundUp(GridHeight, ChunkDimension);
    const float HalfWorldW = (GridWidth * CellSizeWorldUnits) * 0.5f;
    const float HalfWorldH = (GridHeight * CellSizeWorldUnits) * 0.5f;

    for (int32 CY = 0; CY < ChunksY; ++CY)
    {
        for (int32 CX = 0; CX < ChunksX; ++CX)
        {
            const FIntPoint Coord(CX, CY);
            if (ActiveChunkMap.Contains(Coord)) continue;

            const FVector ChunkWorldOrigin(
                (-HalfWorldW) + (CX * ChunkDimension * CellSizeWorldUnits),
                (-HalfWorldH) + (CY * ChunkDimension * CellSizeWorldUnits),
                0.0f
            );

            FActorSpawnParameters SpawnParams;
            SpawnParams.Owner = this;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            ADynamicEnvironmentChunkActor* NewChunk = GetWorld()->SpawnActor<ADynamicEnvironmentChunkActor>(
                ADynamicEnvironmentChunkActor::StaticClass(),
                FTransform(ChunkWorldOrigin),
                SpawnParams
            );

            if (NewChunk)
            {
                NewChunk->InitializeChunk(Coord, ChunkDimension, ChunkDimension, CellSizeWorldUnits, ChunkWorldOrigin);
                RegisterEnvironmentChunk(NewChunk);

                // Populate initial chunk slice from authoritative memory
                for (int32 LocalY = 0; LocalY < ChunkDimension; ++LocalY)
                {
                    for (int32 LocalX = 0; LocalX < ChunkDimension; ++LocalX)
                    {
                        const int32 GlobalX = (CX * ChunkDimension) + LocalX;
                        const int32 GlobalY = (CY * ChunkDimension) + LocalY;
                        if (GlobalX < GridWidth && GlobalY < GridHeight)
                        {
                            const int32 GlobalIndex = (GlobalY * GridWidth) + GlobalX;
                            NewChunk->UpdateCellData(GlobalIndex, ServerEnvironmentGrid[GlobalIndex]);
                        }
                    }
                }
            }
        }
    }
}

float ADynamicGameState::GetEnvironmentMetricAtLocation(const FVector& WorldLocation, FGameplayTag MetricOrElementTag) const
{
    if (CellSizeWorldUnits <= 0.0f || !MetricOrElementTag.IsValid()) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);

    // 1. Authoritative Server / Standalone Lookup
    if (HasAuthority())
    {
        if (!ServerEnvironmentGrid.IsValidIndex(CellIdx)) return 0.0f;
        const FEnvironmentGridCellItem& Cell = ServerEnvironmentGrid[CellIdx];

        const int32 ElemIdx = ResolveElementArrayIndex(MetricOrElementTag);
        if (ElemIdx != INDEX_NONE && Cell.PackedElementalDensities.IsValidIndex(ElemIdx))
        {
            return (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
        }
        return Cell.DerivedPhysicalMetrics.LocalLookupMap.FindRef(MetricOrElementTag);
    }

    // 2. Client Proximity Chunk Lookup
    const int32 CellX = CellIdx % GridWidth;
    const int32 CellY = CellIdx / GridWidth;
    const FIntPoint ChunkCoord(CellX / ChunkDimension, CellY / ChunkDimension);

    if (const TObjectPtr<ADynamicEnvironmentChunkActor>* FoundChunk = ActiveChunkMap.Find(ChunkCoord))
    {
        if (FoundChunk->Get())
        {
            if (const FEnvironmentGridCellItem* Cell = (*FoundChunk)->FindCellItemByGlobalIndex(CellIdx))
            {
                const int32 ElemIdx = ResolveElementArrayIndex(MetricOrElementTag);
                if (ElemIdx != INDEX_NONE && Cell->PackedElementalDensities.IsValidIndex(ElemIdx))
                {
                    return (static_cast<float>(Cell->PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
                }
                return Cell->DerivedPhysicalMetrics.LocalLookupMap.FindRef(MetricOrElementTag);
            }
        }
    }

    // 3. Fall back to default equilibrium if chunk is outside network replication range
    return 0.0f;
}

float ADynamicGameState::ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount)
{
    if (!HasAuthority() || ServerEnvironmentGrid.Num() == 0) return 0.0f;
    const int32 ElemIdx = ResolveElementArrayIndex(ElementTag);
    if (ElemIdx == INDEX_NONE) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    if (!ServerEnvironmentGrid.IsValidIndex(CellIdx)) return 0.0f;

    FEnvironmentGridCellItem& Cell = ServerEnvironmentGrid[CellIdx];
    if (!Cell.PackedElementalDensities.IsValidIndex(ElemIdx)) return 0.0f;

    const float OldValue = (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
    const float NewValue = FMath::Clamp(OldValue + DeltaAmount, 0.0f, EnvironmentConfig.MaxElementDensityCap);
    Cell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewValue / EnvironmentConfig.MaxElementDensityCap) * 255.0f));
    const float ActualDelta = NewValue - OldValue;

    RecalculateCellPhysicalMetrics(Cell);

    // Sync authoritative modification to the relevant spatial chunk
    const int32 CellX = CellIdx % GridWidth;
    const int32 CellY = CellIdx / GridWidth;
    const FIntPoint ChunkCoord(CellX / ChunkDimension, CellY / ChunkDimension);
    if (TObjectPtr<ADynamicEnvironmentChunkActor>* ChunkPtr = ActiveChunkMap.Find(ChunkCoord))
    {
        if (ChunkPtr->Get())
        {
            (*ChunkPtr)->UpdateCellData(CellIdx, Cell);
        }
    }

    return ActualDelta;
}

void ADynamicGameState::ServerTickEnvironmentSimulation(float ElapsedGameHours)
{
    if (!HasAuthority() || ServerEnvironmentGrid.Num() == 0)
    {
        return;
    }

    AccumulatedUnsimulatedHours += ElapsedGameHours;

    // If a worker thread is still computing the previous step, accumulate elapsed hours and return
    if (bIsSimulationTaskRunning)
    {
        return;
    }

    const float HoursToSimulate = AccumulatedUnsimulatedHours;
    AccumulatedUnsimulatedHours = 0.0f;
    bIsSimulationTaskRunning = true;

    // 1. Capture lightweight thread-safe snapshot on the Game Thread
    const int32 TotalCells = ServerEnvironmentGrid.Num();
    const int32 CapturedGridWidth = GridWidth;
    const int32 CapturedGridHeight = GridHeight;
    const int32 CapturedChunkDim = ChunkDimension;
    const FDynamicEnvironmentSystemConfig CapturedEnvConfig = EnvironmentConfig;
    const TMap<FGameplayTag, int32> CapturedElementMap = TrackedElementsMap;
    const TMap<FGameplayTag, FDynamicBiomeDef> CapturedBiomeRegistry = BiomeMasterRegistry;

    TArray<FEnvironmentCellWorkerInput> Inputs;
    Inputs.SetNum(TotalCells);
    for (int32 i = 0; i < TotalCells; ++i)
    {
        const FEnvironmentGridCellItem& Cell = ServerEnvironmentGrid[i];
        Inputs[i].CellIndex = Cell.CellIndex;
        Inputs[i].CurrentBiomeTag = Cell.CurrentBiomeTag;
        Inputs[i].TargetShiftBiomeTag = Cell.TargetShiftBiomeTag;
        Inputs[i].BiomeBlendAlpha = Cell.BiomeBlendAlpha;
        Inputs[i].SustainedOutOfRangeHours = Cell.SustainedOutOfRangeHours;
        Inputs[i].WindDirection = FVector2D(Cell.WindDirection.X, Cell.WindDirection.Y);
        Inputs[i].PackedDensities = Cell.PackedElementalDensities;
    }

    // 2. Dispatch the heavy O(N * Elements) advection work to the thread pool
    Async(EAsyncExecution::ThreadPool, [this, Inputs = MoveTemp(Inputs), HoursToSimulate, TotalCells, CapturedGridWidth, CapturedGridHeight, CapturedChunkDim, CapturedEnvConfig, CapturedElementMap, CapturedBiomeRegistry]()
    {
        const int32 NumElements = CapturedEnvConfig.TrackedElementalTags.Num();
        TArray<TArray<float>> AdvectionDeltas;
        AdvectionDeltas.SetNum(NumElements);
        for (int32 e = 0; e < NumElements; ++e)
        {
            AdvectionDeltas[e].Init(0.0f, TotalCells);
        }

        // Pass A: Advection transfer calculations
        for (int32 i = 0; i < TotalCells; ++i)
        {
            const FEnvironmentCellWorkerInput& Cell = Inputs[i];
            const int32 X = i % CapturedGridWidth;
            const int32 Y = i / CapturedGridWidth;
            const int32 StepX = FMath::RoundToInt(Cell.WindDirection.X);
            const int32 StepY = FMath::RoundToInt(Cell.WindDirection.Y);
            const int32 TargetX = FMath::Clamp(X + StepX, 0, CapturedGridWidth - 1);
            const int32 TargetY = FMath::Clamp(Y + StepY, 0, CapturedGridHeight - 1);
            const int32 TargetIdx = (TargetY * CapturedGridWidth) + TargetX;

            if (TargetIdx != i)
            {
                const float TransferRatio = FMath::Clamp(0.15f * HoursToSimulate, 0.0f, 0.35f);
                for (int32 e = 0; e < NumElements; ++e)
                {
                    if (Cell.PackedDensities.IsValidIndex(e))
                    {
                        const float Density = (static_cast<float>(Cell.PackedDensities[e]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                        const float DensityMoved = Density * TransferRatio;
                        AdvectionDeltas[e][i] -= DensityMoved;
                        AdvectionDeltas[e][TargetIdx] += DensityMoved;
                    }
                }
            }
        }

        // Pass B: Homeostasis, Biome Shift, and Physical Formula evaluations
        TArray<FEnvironmentCellWorkerOutput> Outputs;
        Outputs.SetNum(TotalCells);

        for (int32 i = 0; i < TotalCells; ++i)
        {
            const FEnvironmentCellWorkerInput& InCell = Inputs[i];
            FEnvironmentCellWorkerOutput& OutCell = Outputs[i];
            OutCell.CellIndex = InCell.CellIndex;
            OutCell.CurrentBiomeTag = InCell.CurrentBiomeTag;
            OutCell.TargetShiftBiomeTag = InCell.TargetShiftBiomeTag;
            OutCell.BiomeBlendAlpha = InCell.BiomeBlendAlpha;
            OutCell.SustainedOutOfRangeHours = InCell.SustainedOutOfRangeHours;
            OutCell.PackedDensities = InCell.PackedDensities;
            OutCell.bModified = false;

            for (int32 e = 0; e < NumElements; ++e)
            {
                if (OutCell.PackedDensities.IsValidIndex(e) && !FMath::IsNearlyZero(AdvectionDeltas[e][i]))
                {
                    const float CurDensity = (static_cast<float>(OutCell.PackedDensities[e]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                    const float NewDensity = FMath::Clamp(CurDensity + AdvectionDeltas[e][i], 0.0f, CapturedEnvConfig.MaxElementDensityCap);
                    OutCell.PackedDensities[e] = static_cast<uint8>(FMath::RoundToInt((NewDensity / CapturedEnvConfig.MaxElementDensityCap) * 255.0f));
                    OutCell.bModified = true;
                }
            }

            if (const FDynamicBiomeDef* BiomeDef = CapturedBiomeRegistry.Find(OutCell.CurrentBiomeTag))
            {
                float MaxDeviation = 0.0f;
                for (const FTagFloatPair& Eq : BiomeDef->DefaultElementalEquilibrium)
                {
                    if (const int32* ElemIdxPtr = CapturedElementMap.Find(Eq.KeyTag))
                    {
                        const int32 ElemIdx = *ElemIdxPtr;
                        if (OutCell.PackedDensities.IsValidIndex(ElemIdx))
                        {
                            const float CurVal = (static_cast<float>(OutCell.PackedDensities[ElemIdx]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                            MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(CurVal - Eq.Value));
                            const float NewVal = FMath::FInterpConstantTo(CurVal, Eq.Value, HoursToSimulate, BiomeDef->HomeostasisPullRate);
                            if (!FMath::IsNearlyEqual(CurVal, NewVal, 0.5f))
                            {
                                OutCell.PackedDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewVal / CapturedEnvConfig.MaxElementDensityCap) * 255.0f));
                                OutCell.bModified = true;
                            }
                        }
                    }
                }

                if (MaxDeviation > 35.0f)
                {
                    OutCell.SustainedOutOfRangeHours += HoursToSimulate;
                    for (const FBiomeShiftRule& ShiftRule : BiomeDef->BiomeShiftRules)
                    {
                        if (OutCell.SustainedOutOfRangeHours >= ShiftRule.RequiredSustainedGameHours)
                        {
                            OutCell.TargetShiftBiomeTag = ShiftRule.TargetBiomeTag;
                            OutCell.BiomeBlendAlpha = FMath::Clamp(OutCell.BiomeBlendAlpha + (HoursToSimulate / ShiftRule.RequiredSustainedGameHours), 0.0f, 1.0f);
                            if (OutCell.BiomeBlendAlpha >= 1.0f)
                            {
                                OutCell.CurrentBiomeTag = ShiftRule.TargetBiomeTag;
                                OutCell.BiomeBlendAlpha = 0.0f;
                                OutCell.SustainedOutOfRangeHours = 0.0f;
                            }
                            OutCell.bModified = true;
                            break;
                        }
                    }
                }
                else
                {
                    OutCell.SustainedOutOfRangeHours = FMath::Max(0.0f, OutCell.SustainedOutOfRangeHours - HoursToSimulate);
                }
            }

            // Metric formula evaluation
            for (const FDynamicEnvironmentPhysicalMetricFormula& Formula : CapturedEnvConfig.MetricFormulas)
            {
                float MetricAccum = Formula.BaseConstant;
                for (const auto& WP : Formula.ElementWeights.LocalLookupMap)
                {
                    if (const int32* EIdxPtr = CapturedElementMap.Find(WP.Key))
                    {
                        const int32 EIdx = *EIdxPtr;
                        if (OutCell.PackedDensities.IsValidIndex(EIdx))
                        {
                            const float D = (static_cast<float>(OutCell.PackedDensities[EIdx]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                            MetricAccum += D * WP.Value;
                        }
                    }
                }
                OutCell.DerivedMetrics.UpdateEntry(Formula.MetricTag, FMath::Clamp(MetricAccum, Formula.MinClamp, Formula.MaxClamp));
            }
        }

        // 3. Marshaling results back to the Game Thread to dirty chunk components
        Async(EAsyncExecution::TaskGraphMainThread, [this, Outputs = MoveTemp(Outputs), CapturedGridWidth, CapturedChunkDim]()
        {
            if (!IsValid(this) || ServerEnvironmentGrid.Num() != Outputs.Num())
            {
                bIsSimulationTaskRunning = false;
                return;
            }

            for (const FEnvironmentCellWorkerOutput& OutCell : Outputs)
            {
                if (!OutCell.bModified)
                {
                    continue;
                }

                FEnvironmentGridCellItem& MasterCell = ServerEnvironmentGrid[OutCell.CellIndex];
                MasterCell.CurrentBiomeTag = OutCell.CurrentBiomeTag;
                MasterCell.TargetShiftBiomeTag = OutCell.TargetShiftBiomeTag;
                MasterCell.BiomeBlendAlpha = OutCell.BiomeBlendAlpha;
                MasterCell.SustainedOutOfRangeHours = OutCell.SustainedOutOfRangeHours;
                MasterCell.PackedElementalDensities = OutCell.PackedDensities;
                MasterCell.DerivedPhysicalMetrics = OutCell.DerivedMetrics;

                const int32 CellX = OutCell.CellIndex % CapturedGridWidth;
                const int32 CellY = OutCell.CellIndex / CapturedGridWidth;
                const FIntPoint ChunkCoord(CellX / CapturedChunkDim, CellY / CapturedChunkDim);

                if (TObjectPtr<ADynamicEnvironmentChunkActor>* FoundChunk = ActiveChunkMap.Find(ChunkCoord))
                {
                    if (FoundChunk->Get())
                    {
                        (*FoundChunk)->UpdateCellData(OutCell.CellIndex, MasterCell);
                    }
                }
            }

            bIsSimulationTaskRunning = false;
        });
    });
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
    if (GridWidth <= 0 || GridHeight <= 0 || CellSizeWorldUnits <= 0.0f) return 0;
    const float HalfWorldW = (GridWidth * CellSizeWorldUnits) * 0.5f;
    const float HalfWorldH = (GridHeight * CellSizeWorldUnits) * 0.5f;
    const int32 X = FMath::Clamp(FMath::FloorToInt((WorldLocation.X + HalfWorldW) / CellSizeWorldUnits), 0, GridWidth - 1);
    const int32 Y = FMath::Clamp(FMath::FloorToInt((WorldLocation.Y + HalfWorldH) / CellSizeWorldUnits), 0, GridHeight - 1);
    return FMath::Clamp((Y * GridWidth) + X, 0, (GridWidth * GridHeight) - 1);
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