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
        EnvironmentReadBuffer.SetNum(TotalCells);
        EnvironmentWriteBuffer.SetNum(TotalCells);

        const int32 NumElements = EnvironmentConfig.TrackedElementalTags.Num();
        const uint8 DefaultPackedVal = static_cast<uint8>(FMath::Clamp(
            FMath::RoundToInt((EnvironmentConfig.DefaultCellDensity / FMath::Max(1.0f, EnvironmentConfig.MaxElementDensityCap)) * 255.0f),
            0, 255
        ));

        for (int32 i = 0; i < TotalCells; ++i)
        {
            FEnvironmentGridCellItem& Cell = EnvironmentReadBuffer[i];
            Cell.CellIndex = i;
            Cell.PackedElementalDensities.Init(DefaultPackedVal, NumElements);
            RecalculateCellPhysicalMetrics(Cell);
        }

        EnvironmentWriteBuffer = EnvironmentReadBuffer;

        if (NumElements > 0 && TotalCells > 0)
        {
            AdvectionDeltaScratchpad.SetNumZeroed(NumElements * TotalCells);
        }

        SpawnServerEnvironmentChunks();
    }
}

void ADynamicGameState::SpawnServerEnvironmentChunks()
{
    if (!HasAuthority() || EnvironmentReadBuffer.Num() == 0 || ChunkDimension <= 0) return;

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

                for (int32 LocalY = 0; LocalY < ChunkDimension; ++LocalY)
                {
                    for (int32 LocalX = 0; LocalX < ChunkDimension; ++LocalX)
                    {
                        const int32 GlobalX = (CX * ChunkDimension) + LocalX;
                        const int32 GlobalY = (CY * ChunkDimension) + LocalY;
                        if (GlobalX < GridWidth && GlobalY < GridHeight)
                        {
                            const int32 GlobalIndex = (GlobalY * GridWidth) + GlobalX;
                            NewChunk->UpdateCellData(GlobalIndex, EnvironmentReadBuffer[GlobalIndex]);
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
        if (!EnvironmentReadBuffer.IsValidIndex(CellIdx)) return 0.0f;
        const FEnvironmentGridCellItem& Cell = EnvironmentReadBuffer[CellIdx];

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

    return 0.0f;
}

float ADynamicGameState::ServerModifyEnvironmentalMana(const FVector& WorldLocation, FGameplayTag ElementTag, float DeltaAmount)
{
    if (!HasAuthority() || EnvironmentReadBuffer.Num() == 0) return 0.0f;
    const int32 ElemIdx = ResolveElementArrayIndex(ElementTag);
    if (ElemIdx == INDEX_NONE) return 0.0f;

    const int32 CellIdx = GetEnvironmentCellIndexAtLocation(WorldLocation);
    if (!EnvironmentReadBuffer.IsValidIndex(CellIdx)) return 0.0f;

    FEnvironmentGridCellItem& Cell = EnvironmentReadBuffer[CellIdx];
    if (!Cell.PackedElementalDensities.IsValidIndex(ElemIdx)) return 0.0f;

    const float OldValue = (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * EnvironmentConfig.MaxElementDensityCap;
    const float NewValue = FMath::Clamp(OldValue + DeltaAmount, 0.0f, EnvironmentConfig.MaxElementDensityCap);
    Cell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewValue / EnvironmentConfig.MaxElementDensityCap) * 255.0f));
    const float ActualDelta = NewValue - OldValue;

    RecalculateCellPhysicalMetrics(Cell);

    // Keep write buffer aligned if simulation task is not running
    if (!bIsSimulationTaskRunning && EnvironmentWriteBuffer.IsValidIndex(CellIdx))
    {
        EnvironmentWriteBuffer[CellIdx] = Cell;
    }

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
    if (!HasAuthority() || EnvironmentReadBuffer.Num() == 0)
    {
        return;
    }

    AccumulatedUnsimulatedHours += ElapsedGameHours;

    if (bIsSimulationTaskRunning)
    {
        return;
    }

    const float HoursToSimulate = AccumulatedUnsimulatedHours;
    AccumulatedUnsimulatedHours = 0.0f;
    bIsSimulationTaskRunning = true;

    const int32 TotalCells = EnvironmentReadBuffer.Num();
    const int32 CapturedGridWidth = GridWidth;
    const int32 CapturedGridHeight = GridHeight;
    const int32 CapturedChunkDim = ChunkDimension;
    const FDynamicEnvironmentSystemConfig CapturedEnvConfig = EnvironmentConfig;
    const TMap<FGameplayTag, int32> CapturedElementMap = TrackedElementsMap;
    const TMap<FGameplayTag, FDynamicBiomeDef> CapturedBiomeRegistry = BiomeMasterRegistry;

    // Worker thread reads from EnvironmentReadBuffer (read-only) and writes directly to EnvironmentWriteBuffer.
    // Zero allocations on the Game Thread.
    Async(EAsyncExecution::ThreadPool, [this, HoursToSimulate, TotalCells, CapturedGridWidth, CapturedGridHeight, CapturedChunkDim, CapturedEnvConfig, CapturedElementMap, CapturedBiomeRegistry]()
    {
        const int32 NumElements = CapturedEnvConfig.TrackedElementalTags.Num();
        const int32 ScratchpadSize = NumElements * TotalCells;

        if (AdvectionDeltaScratchpad.Num() != ScratchpadSize)
        {
            AdvectionDeltaScratchpad.SetNumZeroed(ScratchpadSize);
        }
        else
        {
            FMemory::Memzero(AdvectionDeltaScratchpad.GetData(), ScratchpadSize * sizeof(float));
        }

        const FEnvironmentGridCellItem* ReadCells = EnvironmentReadBuffer.GetData();
        FEnvironmentGridCellItem* WriteCells = EnvironmentWriteBuffer.GetData();
        float* Deltas = AdvectionDeltaScratchpad.GetData();

        const bool bWrapGrid = CapturedEnvConfig.bWrapGridBoundaries;
        const float BorderDissipation = FMath::Clamp(CapturedEnvConfig.BorderDissipationRate, 0.0f, 1.0f);
        const float TransferRatio = FMath::Clamp(0.15f * HoursToSimulate, 0.0f, 0.35f);

        // Pass A: Advection transfer calculation with toroidal wrapping or boundary dissipation
        for (int32 i = 0; i < TotalCells; ++i)
        {
            const FEnvironmentGridCellItem& Cell = ReadCells[i];
            const int32 X = i % CapturedGridWidth;
            const int32 Y = i / CapturedGridWidth;
            const int32 StepX = FMath::RoundToInt(Cell.WindDirection.X);
            const int32 StepY = FMath::RoundToInt(Cell.WindDirection.Y);

            int32 TargetIdx = INDEX_NONE;
            bool bBleedOffMap = false;

            if (bWrapGrid)
            {
                const int32 WrappedX = ((X + StepX) % CapturedGridWidth + CapturedGridWidth) % CapturedGridWidth;
                const int32 WrappedY = ((Y + StepY) % CapturedGridHeight + CapturedGridHeight) % CapturedGridHeight;
                TargetIdx = (WrappedY * CapturedGridWidth) + WrappedX;
            }
            else
            {
                const int32 NextX = X + StepX;
                const int32 NextY = Y + StepY;
                if (NextX < 0 || NextX >= CapturedGridWidth || NextY < 0 || NextY >= CapturedGridHeight)
                {
                    bBleedOffMap = true;
                }
                else
                {
                    TargetIdx = (NextY * CapturedGridWidth) + NextX;
                }
            }

            for (int32 e = 0; e < NumElements; ++e)
            {
                if (Cell.PackedElementalDensities.IsValidIndex(e))
                {
                    const float Density = (static_cast<float>(Cell.PackedElementalDensities[e]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                    const float DensityMoved = Density * TransferRatio;

                    if (bBleedOffMap)
                    {
                        Deltas[e * TotalCells + i] -= DensityMoved * BorderDissipation;
                    }
                    else if (TargetIdx != INDEX_NONE && TargetIdx != i)
                    {
                        Deltas[e * TotalCells + i] -= DensityMoved;
                        Deltas[e * TotalCells + TargetIdx] += DensityMoved;
                    }
                }
            }
        }

        // Pass B: Homeostasis, Biome Shift, and Physical Formula evaluations
        TArray<int32> ModifiedCellIndices;
        ModifiedCellIndices.Reserve(TotalCells / 4);

        for (int32 i = 0; i < TotalCells; ++i)
        {
            const FEnvironmentGridCellItem& ReadCell = ReadCells[i];
            FEnvironmentGridCellItem& WriteCell = WriteCells[i];

            WriteCell.CellIndex = ReadCell.CellIndex;
            WriteCell.CurrentBiomeTag = ReadCell.CurrentBiomeTag;
            WriteCell.TargetShiftBiomeTag = ReadCell.TargetShiftBiomeTag;
            WriteCell.BiomeBlendAlpha = ReadCell.BiomeBlendAlpha;
            WriteCell.SustainedOutOfRangeHours = ReadCell.SustainedOutOfRangeHours;
            WriteCell.WindDirection = ReadCell.WindDirection;
            WriteCell.PackedElementalDensities = ReadCell.PackedElementalDensities;

            bool bCellModified = false;

            for (int32 e = 0; e < NumElements; ++e)
            {
                const float Delta = Deltas[e * TotalCells + i];
                if (WriteCell.PackedElementalDensities.IsValidIndex(e) && !FMath::IsNearlyZero(Delta))
                {
                    const float CurDensity = (static_cast<float>(WriteCell.PackedElementalDensities[e]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                    const float NewDensity = FMath::Clamp(CurDensity + Delta, 0.0f, CapturedEnvConfig.MaxElementDensityCap);
                    WriteCell.PackedElementalDensities[e] = static_cast<uint8>(FMath::RoundToInt((NewDensity / CapturedEnvConfig.MaxElementDensityCap) * 255.0f));
                    bCellModified = true;
                }
            }

            if (const FDynamicBiomeDef* BiomeDef = CapturedBiomeRegistry.Find(WriteCell.CurrentBiomeTag))
            {
                float MaxDeviation = 0.0f;
                for (const FTagFloatPair& Eq : BiomeDef->DefaultElementalEquilibrium)
                {
                    if (const int32* ElemIdxPtr = CapturedElementMap.Find(Eq.KeyTag))
                    {
                        const int32 ElemIdx = *ElemIdxPtr;
                        if (WriteCell.PackedElementalDensities.IsValidIndex(ElemIdx))
                        {
                            const float CurVal = (static_cast<float>(WriteCell.PackedElementalDensities[ElemIdx]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                            MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(CurVal - Eq.Value));
                            const float NewVal = FMath::FInterpConstantTo(CurVal, Eq.Value, HoursToSimulate, BiomeDef->HomeostasisPullRate);
                            if (!FMath::IsNearlyEqual(CurVal, NewVal, 0.5f))
                            {
                                WriteCell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((NewVal / CapturedEnvConfig.MaxElementDensityCap) * 255.0f));
                                bCellModified = true;
                            }
                        }
                    }
                }

                if (MaxDeviation > 35.0f)
                {
                    WriteCell.SustainedOutOfRangeHours += HoursToSimulate;
                    for (const FBiomeShiftRule& ShiftRule : BiomeDef->BiomeShiftRules)
                    {
                        if (WriteCell.SustainedOutOfRangeHours >= ShiftRule.RequiredSustainedGameHours)
                        {
                            WriteCell.TargetShiftBiomeTag = ShiftRule.TargetBiomeTag;
                            WriteCell.BiomeBlendAlpha = FMath::Clamp(WriteCell.BiomeBlendAlpha + (HoursToSimulate / ShiftRule.RequiredSustainedGameHours), 0.0f, 1.0f);
                            if (WriteCell.BiomeBlendAlpha >= 1.0f)
                            {
                                WriteCell.CurrentBiomeTag = ShiftRule.TargetBiomeTag;
                                WriteCell.BiomeBlendAlpha = 0.0f;
                                WriteCell.SustainedOutOfRangeHours = 0.0f;
                            }
                            bCellModified = true;
                            break;
                        }
                    }
                }
                else
                {
                    WriteCell.SustainedOutOfRangeHours = FMath::Max(0.0f, WriteCell.SustainedOutOfRangeHours - HoursToSimulate);
                }
            }

            for (const FDynamicEnvironmentPhysicalMetricFormula& Formula : CapturedEnvConfig.MetricFormulas)
            {
                float MetricAccum = Formula.BaseConstant;
                for (const auto& WP : Formula.ElementWeights.LocalLookupMap)
                {
                    if (const int32* EIdxPtr = CapturedElementMap.Find(WP.Key))
                    {
                        const int32 EIdx = *EIdxPtr;
                        if (WriteCell.PackedElementalDensities.IsValidIndex(EIdx))
                        {
                            const float D = (static_cast<float>(WriteCell.PackedElementalDensities[EIdx]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                            MetricAccum += D * WP.Value;
                        }
                    }
                }
                WriteCell.DerivedPhysicalMetrics.UpdateEntry(Formula.MetricTag, FMath::Clamp(MetricAccum, Formula.MinClamp, Formula.MaxClamp));
            }

            if (bCellModified)
            {
                ModifiedCellIndices.Add(i);
            }
        }

        // Pass C: GameThread completion - O(1) buffer swap
        Async(EAsyncExecution::TaskGraphMainThread, [this, ModifiedIndices = MoveTemp(ModifiedCellIndices), CapturedGridWidth, CapturedChunkDim]()
        {
            if (!IsValid(this))
            {
                bIsSimulationTaskRunning = false;
                return;
            }

            Swap(EnvironmentReadBuffer, EnvironmentWriteBuffer);

            for (const int32 CellIdx : ModifiedIndices)
            {
                const int32 CellX = CellIdx % CapturedGridWidth;
                const int32 CellY = CellIdx / CapturedGridWidth;
                const FIntPoint ChunkCoord(CellX / CapturedChunkDim, CellY / CapturedChunkDim);

                if (TObjectPtr<ADynamicEnvironmentChunkActor>* FoundChunk = ActiveChunkMap.Find(ChunkCoord))
                {
                    if (FoundChunk->Get())
                    {
                        (*FoundChunk)->UpdateCellData(CellIdx, EnvironmentReadBuffer[CellIdx]);
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

            // SCRUB STALE DATA: Sanitize actor before returning to active world
            PooledActor->ResetToDefaultPoolState();
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

            // SCRUB STALE DATA: Sanitize actor before re-hydrating new archetype
            PooledActor->ResetToDefaultPoolState();
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

void ADynamicGameState::MergeStagingRegistry(FMasterRegistryStagingData&& StagingData)
{
    check(IsInGameThread());

    // 1. Move/Append all parsed TMaps in O(1) pointer operations
    AttributeMasterRegistry.Append(MoveTemp(StagingData.AttributeMasterRegistry));
    DeliveryArchetypes.Append(MoveTemp(StagingData.DeliveryArchetypes));
    AbilityMasterRegistry.Append(MoveTemp(StagingData.AbilityMasterRegistry));
    SharedConditionTrees.Append(MoveTemp(StagingData.SharedConditionTrees));
    MasterMilestoneRegistry.Append(MoveTemp(StagingData.MasterMilestoneRegistry));
    MasterThresholdRegistry.Append(MoveTemp(StagingData.MasterThresholdRegistry));
    MasterPhysicsActorRegistry.Append(MoveTemp(StagingData.MasterPhysicsActorRegistry));
    CharacterTemplatesRegistry.Append(MoveTemp(StagingData.CharacterTemplatesRegistry));
    SkeletonMasterRegistry.Append(MoveTemp(StagingData.SkeletonMasterRegistry));
    RelationshipTypeRegistry.Append(MoveTemp(StagingData.RelationshipTypeRegistry));
    ProfessionMasterRegistry.Append(MoveTemp(StagingData.ProfessionMasterRegistry));
    MasterAIAgentRegistry.Append(MoveTemp(StagingData.MasterAIAgentRegistry));
    ConversionRegistry.Append(MoveTemp(StagingData.ConversionRegistry));
    EvolutionMasterRegistry.Append(MoveTemp(StagingData.EvolutionMasterRegistry));
    GeneticsMasterRegistry.Append(MoveTemp(StagingData.GeneticsMasterRegistry));
    BiomeMasterRegistry.Append(MoveTemp(StagingData.BiomeMasterRegistry));
    WeatherMasterRegistry.Append(MoveTemp(StagingData.WeatherMasterRegistry));
    EvolutionProfilesRegistry.Append(MoveTemp(StagingData.EvolutionProfilesRegistry));
    QuestMasterRegistry.Append(MoveTemp(StagingData.QuestMasterRegistry));
    QuestGeneratorRules.Append(MoveTemp(StagingData.QuestGeneratorRules));
    LanguageMasterRegistry.Append(MoveTemp(StagingData.LanguageMasterRegistry));
    NameGenerationRulesRegistry.Append(MoveTemp(StagingData.NameGenerationRulesRegistry));
    RecipeMasterRegistry.Append(MoveTemp(StagingData.RecipeMasterRegistry));

    // 2. Append Gameplay Effects and pre-warm CDO cache for newly registered effects
    if (StagingData.MasterEffectRegistry.Num() > 0)
    {
        for (auto& Pair : StagingData.MasterEffectRegistry)
        {
            MasterEffectRegistry.Add(Pair.Key, MoveTemp(Pair.Value));
            GetOrCreateCachedEffectObject(Pair.Key);
        }
    }

    // 3. Update single configuration blocks if provided
    if (StagingData.bHasCalendarConfig)
    {
        CalendarConfig = StagingData.CalendarConfig;
    }
    if (StagingData.bHasPhysicsConfig)
    {
        SetPhysicsConfig(StagingData.PhysicsConfig);
    }
    else if (StagingData.bHasCalcSettings)
    {
        CalculationSettings = StagingData.CalcSettings;
    }
    if (StagingData.bHasAbilityScoreConfig)
    {
        SetAbilityScoreConfig(StagingData.AbilityScoreConfig);
    }
    if (StagingData.bHasEnvironmentConfig)
    {
        SetupDynamicEnvironment(StagingData.EnvironmentConfig);
    }

    // 4. Rebuild ability progression scores
    RebuildAllAbilityScores();
}