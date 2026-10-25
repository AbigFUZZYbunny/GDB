// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "DynamicSpatialActor.h"
#include "DynamicDataSerializer.h"
#include "DynamicPhysicsActor.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicSocialComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicNetworkComponent.h"
#include "AIController.h"
#include "DynamicCharacter.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicEnvironmentChunkActor.h"
#include "DynamicLogicEvaluator.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
}

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void ADynamicGameState::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        WarmGameplayEffectCache();
        const FString DataDir = FPaths::ProjectContentDir() / TEXT("Data");
        TArray<FString> ShippedJsonFiles;
        IFileManager::Get().FindFiles(ShippedJsonFiles, *(DataDir / TEXT("*.json")), true, false);
        for (const FString& FileName : ShippedJsonFiles)
        {
            FString FileContent;
            if (FFileHelper::LoadFileToString(FileContent, *(DataDir / FileName)))
            {
                UDynamicDataSerializer::DeserializeMasterRegistry(FileContent, this);
            }
        }

        const FString ModDir = FPaths::ProjectSavedDir() / TEXT("DynamicModules");
        TArray<FString> ModFiles;
        IFileManager::Get().FindFiles(ModFiles, *(ModDir / TEXT("*.json")), true, false);
        for (const FString& ModFile : ModFiles)
        {
            FString ModContent;
            if (FFileHelper::LoadFileToString(ModContent, *(ModDir / ModFile)))
            {
                UDynamicDataSerializer::DeserializeMasterRegistry(ModContent, this);
            }
        }

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

    if (bIsSimulationTaskRunning)
    {
        FEnvironmentManaModificationRecord Record;
        Record.CellIndex = CellIdx;
        Record.ElementTag = ElementTag;
        Record.DeltaAmount = ActualDelta;
        PendingManaModifications.Enqueue(Record);
    }
    else if (EnvironmentWriteBuffer.IsValidIndex(CellIdx))
    {
        EnvironmentWriteBuffer[CellIdx] = Cell;
    }

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
    if (!HasAuthority() || EnvironmentReadBuffer.Num() == 0) return;

    AccumulatedUnsimulatedHours += ElapsedGameHours;
    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    if (bIsSimulationTaskRunning)
    {
        if (SimulationTaskStartTimestamp > 0.0 && (CurrentWorldTime - SimulationTaskStartTimestamp) > SimulationTaskTimeoutSeconds)
        {
            UE_LOG(LogTemp, Error, TEXT("ADynamicGameState: Async environment simulation task timed out after %.1f seconds. Forcing watchdog reset."), (CurrentWorldTime - SimulationTaskStartTimestamp));
            bIsSimulationTaskRunning = false;
            SimulationTaskStartTimestamp = 0.0;
        }
        else
        {
            return;
        }
    }

    const float HoursToSimulate = AccumulatedUnsimulatedHours;
    AccumulatedUnsimulatedHours = 0.0f;
    bIsSimulationTaskRunning = true;
    SimulationTaskStartTimestamp = CurrentWorldTime;

    const int32 TotalCells = EnvironmentReadBuffer.Num();
    const int32 CapturedGridWidth = GridWidth;
    const int32 CapturedGridHeight = GridHeight;
    const int32 CapturedChunkDim = ChunkDimension;
    const FDynamicEnvironmentSystemConfig CapturedEnvConfig = EnvironmentConfig;

    // OPTIMIZATION: Capture registries and maps by pointer to eliminate deep-copy overhead on the GameThread
    const TMap<FGameplayTag, int32>* ElementMapPtr = &TrackedElementsMap;
    const TMap<FGameplayTag, FDynamicBiomeDef>* BiomeRegistryPtr = &BiomeMasterRegistry;

    Async(EAsyncExecution::ThreadPool, [this, HoursToSimulate, TotalCells, CapturedGridWidth, CapturedGridHeight, CapturedChunkDim, CapturedEnvConfig, ElementMapPtr, BiomeRegistryPtr]()
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

            // Dereference the Biome Registry pointer safely
            if (const FDynamicBiomeDef* BiomeDef = BiomeRegistryPtr->Find(WriteCell.CurrentBiomeTag))
            {
                float MaxDeviation = 0.0f;
                for (const FTagFloatPair& Eq : BiomeDef->DefaultElementalEquilibrium)
                {
                    // Dereference the Element Map pointer safely
                    if (const int32* ElemIdxPtr = ElementMapPtr->Find(Eq.KeyTag))
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
                    if (const int32* EIdxPtr = ElementMapPtr->Find(WP.Key))
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

        // Pass C: GameThread completion using dynamic time-sliced budget (2ms cap)
        Async(EAsyncExecution::TaskGraphMainThread, [this, ModifiedIndices = MoveTemp(ModifiedCellIndices), CapturedGridWidth, CapturedChunkDim, CapturedEnvConfig, HoursToSimulate]()
        {
            if (!IsValid(this))
            {
                bIsSimulationTaskRunning = false;
                SimulationTaskStartTimestamp = 0.0;
                return;
            }

            Swap(EnvironmentReadBuffer, EnvironmentWriteBuffer);

            FEnvironmentManaModificationRecord PendingMod;
            TSet<int32> PostSwapModifiedCells;
            PostSwapModifiedCells.Reserve(ModifiedIndices.Num());
            PostSwapModifiedCells.Append(ModifiedIndices);

            const double DrainStartTime = FPlatformTime::Seconds();
            constexpr double MaxDrainBudgetSeconds = 0.002;

            while ((FPlatformTime::Seconds() - DrainStartTime < MaxDrainBudgetSeconds) && PendingManaModifications.Dequeue(PendingMod))
            {
                if (EnvironmentReadBuffer.IsValidIndex(PendingMod.CellIndex))
                {
                    const int32 ElemIdx = ResolveElementArrayIndex(PendingMod.ElementTag);
                    if (ElemIdx != INDEX_NONE && EnvironmentReadBuffer[PendingMod.CellIndex].PackedElementalDensities.IsValidIndex(ElemIdx))
                    {
                        FEnvironmentGridCellItem& Cell = EnvironmentReadBuffer[PendingMod.CellIndex];
                        const float CurrentVal = (static_cast<float>(Cell.PackedElementalDensities[ElemIdx]) / 255.0f) * CapturedEnvConfig.MaxElementDensityCap;
                        const float AdjustedVal = FMath::Clamp(CurrentVal + PendingMod.DeltaAmount, 0.0f, CapturedEnvConfig.MaxElementDensityCap);
                        Cell.PackedElementalDensities[ElemIdx] = static_cast<uint8>(FMath::RoundToInt((AdjustedVal / CapturedEnvConfig.MaxElementDensityCap) * 255.0f));
                        RecalculateCellPhysicalMetrics(Cell);
                        PostSwapModifiedCells.Add(PendingMod.CellIndex);
                    }
                }
            }

            // Execute the newly integrated Network Graph Solver update
            EvaluateNetworkGraphs(HoursToSimulate * 3600.0f);

            for (const int32 CellIdx : PostSwapModifiedCells)
            {
                if (!EnvironmentReadBuffer.IsValidIndex(CellIdx)) continue;
                if (EnvironmentWriteBuffer.IsValidIndex(CellIdx))
                {
                    EnvironmentWriteBuffer[CellIdx] = EnvironmentReadBuffer[CellIdx];
                }

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
            SimulationTaskStartTimestamp = 0.0;
        });
    });
}

UGameplayEffect* ADynamicGameState::GetOrCreateCachedEffectObject(FGameplayTag EffectTag)
{
    if (!EffectTag.IsValid()) return nullptr;

    const FDynamicEffectDef* FoundDef = MasterEffectRegistry.Find(EffectTag);
    if (!FoundDef) return nullptr;

    switch (FoundDef->DurationPolicy)
    {
    case EEffectDurationPolicy::Instant:     return GenericInstantEffect;
    case EEffectDurationPolicy::HasDuration: return GenericDurationEffect;
    case EEffectDurationPolicy::Infinite:    return GenericInfiniteEffect;
    }

    return GenericInstantEffect;
}

void ADynamicGameState::WarmGameplayEffectCacheAsync(TFunction<void()> OnComplete, float MaxBudgetPerFrameSeconds)
{
    TArray<FGameplayTag> PendingTags;
    for (const auto& Pair : MasterEffectRegistry)
    {
        PendingTags.Add(Pair.Key);
    }
    for (const auto& AbilityPair : AbilityMasterRegistry)
    {
        for (const FGameplayTag& EffTag : AbilityPair.Value.SpatialDelivery.EffectsToApply)
        {
            if (EffTag.IsValid()) PendingTags.AddUnique(EffTag);
        }
        for (const FGameplayTag& EffTag : AbilityPair.Value.InteractionConfig.EffectsToApplyToTarget)
        {
            if (EffTag.IsValid()) PendingTags.AddUnique(EffTag);
        }
    }

    TSharedRef<int32> CurrentIndex = MakeShared<int32>(0);
    TSharedRef<TArray<FGameplayTag>> TagsToProcess = MakeShared<TArray<FGameplayTag>>(MoveTemp(PendingTags));

    TWeakObjectPtr<ADynamicGameState> WeakThis(this);

    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, CurrentIndex, TagsToProcess, OnComplete, MaxBudgetPerFrameSeconds](float DeltaTime) -> bool
    {
        if (!WeakThis.IsValid())
        {
            return false;
        }

        ADynamicGameState* StrongThis = WeakThis.Get();
        const double StartTime = FPlatformTime::Seconds();

        while (*CurrentIndex < TagsToProcess->Num() && (FPlatformTime::Seconds() - StartTime < MaxBudgetPerFrameSeconds))
        {
            const FGameplayTag& Tag = (*TagsToProcess)[*CurrentIndex];
            StrongThis->GetOrCreateCachedEffectObject(Tag);
            (*CurrentIndex)++;
        }

        if (*CurrentIndex >= TagsToProcess->Num())
        {
            if (OnComplete)
            {
                OnComplete();
            }
            return false;
        }
        return true;
    }));
}

int32 ADynamicGameState::GetEnvironmentCellIndexAtLocation(const FVector& WorldLocation) const
{
    if (CellSizeWorldUnits <= 0.0f || GridWidth <= 0 || GridHeight <= 0) return INDEX_NONE;

    const float HalfWorldW = (GridWidth * CellSizeWorldUnits) * 0.5f;
    const float HalfWorldH = (GridHeight * CellSizeWorldUnits) * 0.5f;

    const float LocalX = WorldLocation.X + HalfWorldW;
    const float LocalY = WorldLocation.Y + HalfWorldH;

    const int32 CellX = FMath::FloorToInt(LocalX / CellSizeWorldUnits);
    const int32 CellY = FMath::FloorToInt(LocalY / CellSizeWorldUnits);

    if (CellX < 0 || CellY < 0 || CellX >= GridWidth || CellY >= GridHeight)
    {
        return INDEX_NONE;
    }

    return (CellY * GridWidth) + CellX;
}

void ADynamicGameState::RecalculateCellPhysicalMetrics(FEnvironmentGridCellItem& Cell)
{
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

float ADynamicGameState::ExtractReflectedNumericProperty(const void* StructMemory, const UScriptStruct* StructType, FName PropertyPath) const
{
    if (!StructMemory || !StructType || PropertyPath.IsNone()) return 0.0f;
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
    if (Rule.MetricTag.IsValid())
    {
        if (const float* FoundNum = AbilityDef.NumericParams.LocalLookupMap.Find(Rule.MetricTag)) return *FoundNum;
        if (const float* FoundDel = AbilityDef.SpatialDelivery.RuntimeMetricModifiers.LocalLookupMap.Find(Rule.MetricTag)) return *FoundDel;
        if (const float* FoundMov = AbilityDef.MovementConfig.MovementMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundMov;
        if (const float* FoundInt = AbilityDef.InteractionConfig.InteractionMetrics.LocalLookupMap.Find(Rule.MetricTag)) return *FoundInt;
    }
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

void ADynamicGameState::RegisterSpatialRoleActor(AActor* TargetActor, const FGameplayTagContainer& RoleTags)
{
    if (!HasAuthority() || !IsValid(TargetActor) || RoleTags.IsEmpty()) return;

    TWeakObjectPtr<AActor> WeakTarget(TargetActor);
    UnregisterSpatialRoleActor(TargetActor);

    TArray<FGameplayTag>& TrackedRoles = ActorToRegisteredRolesMap.FindOrAdd(WeakTarget);

    for (const FGameplayTag& BaseTag : RoleTags)
    {
        if (!BaseTag.IsValid()) continue;
        FGameplayTag CurrentTag = BaseTag;
        while (CurrentTag.IsValid())
        {
            SpatialRoleActorRegistry.FindOrAdd(CurrentTag).AddUnique(WeakTarget);
            TrackedRoles.AddUnique(CurrentTag);
            CurrentTag = CurrentTag.RequestDirectParent();
        }
    }
}

void ADynamicGameState::UnregisterSpatialRoleActor(AActor* TargetActor)
{
    if (!HasAuthority() || !TargetActor) return;

    TWeakObjectPtr<AActor> WeakTarget(TargetActor);
    TArray<FGameplayTag> RemovedRoles;
    if (ActorToRegisteredRolesMap.RemoveAndCopyValue(WeakTarget, RemovedRoles))
    {
        for (const FGameplayTag& RoleTag : RemovedRoles)
        {
            if (TArray<TWeakObjectPtr<AActor>>* RoleBucket = SpatialRoleActorRegistry.Find(RoleTag))
            {
                RoleBucket->Remove(WeakTarget);
                if (RoleBucket->IsEmpty())
                {
                    SpatialRoleActorRegistry.Remove(RoleTag);
                }
            }
        }
    }
}

void ADynamicGameState::GetActorsWithRoleTag(FGameplayTag RoleTag, TArray<AActor*>& OutActors) const
{
    OutActors.Reset();
    if (!RoleTag.IsValid()) return;

    if (const TArray<TWeakObjectPtr<AActor>>* FoundList = SpatialRoleActorRegistry.Find(RoleTag))
    {
        OutActors.Reserve(FoundList->Num());
        for (const TWeakObjectPtr<AActor>& WeakActor : *FoundList)
        {
            if (AActor* RawActor = WeakActor.Get())
            {
                if (IsValid(RawActor) && !RawActor->IsPendingKillPending())
                {
                    OutActors.Add(RawActor);
                }
            }
        }
    }
}

void ADynamicGameState::ReleasePhysicsActorToPool(ADynamicPhysicsActor* ActorToReturn)
{
    if (!HasAuthority() || !IsValid(ActorToReturn)) return;
    UnregisterSpatialRoleActor(ActorToReturn);
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
    FastPathDialogueRegistry.Append(MoveTemp(StagingData.FastPathDialogueRegistry));
    MovementStateRegistry.Append(MoveTemp(StagingData.MovementStateRegistry));
    AIBehaviorMasterRegistry.Append(MoveTemp(StagingData.AIBehaviorMasterRegistry));
    NetworkMasterRegistry.Append(MoveTemp(StagingData.NetworkMasterRegistry));
    VoxelMasterRegistry.Append(MoveTemp(StagingData.VoxelMasterRegistry));
    StructureMaterialRegistry.Append(MoveTemp(StagingData.StructureMaterialRegistry));
    StructurePieceRegistry.Append(MoveTemp(StagingData.StructurePieceRegistry));
    StructurePrefabRegistry.Append(MoveTemp(StagingData.StructurePrefabRegistry));
    RoomTypeRegistry.Append(MoveTemp(StagingData.RoomTypeRegistry));

    if (StagingData.MasterEffectRegistry.Num() > 0)
    {
        for (auto& Pair : StagingData.MasterEffectRegistry)
        {
            MasterEffectRegistry.Add(Pair.Key, MoveTemp(Pair.Value));
            GetOrCreateCachedEffectObject(Pair.Key);
        }
    }
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
    if (StagingData.bHasVoxelGenerationConfig)
    {
        VoxelGenerationConfig = StagingData.VoxelGenerationConfig;
    }

    RebuildAllAbilityScores();
    WarmGameplayEffectCache();
}

ADynamicCharacter* ADynamicGameState::AcquireCharacter(FGameplayTag TemplateTag, const FTransform& SpawnTransform, AActor* OwnerActor, APawn* InstigatorPawn)
{
    if (!HasAuthority()) return nullptr;

    auto SpawnConfiguredAIController = [this](ADynamicCharacter* TargetCharacter, const FGameplayTag& InTemplateTag)
    {
        FCharacterTemplateDef TemplateDef;
        if (GetCharacterTemplate(InTemplateTag, TemplateDef))
        {
            if (const FDynamicAIAgentDef* AgentDef = MasterAIAgentRegistry.Find(TemplateDef.DefaultAIAgentTag))
            {
                if (!AgentDef->AIControllerClass.IsNull())
                {
                    if (UClass* ControllerClass = AgentDef->AIControllerClass.LoadSynchronous())
                    {
                        FActorSpawnParameters ConParams;
                        ConParams.Owner = TargetCharacter;
                        ConParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                        if (AAIController* NewAICon = GetWorld()->SpawnActor<AAIController>(ControllerClass, TargetCharacter->GetActorTransform(), ConParams))
                        {
                            NewAICon->Possess(TargetCharacter);
                            return;
                        }
                    }
                }
            }
        }
        TargetCharacter->SpawnDefaultController();
    };

    while (InactiveCharacterPool.Num() > 0)
    {
        ADynamicCharacter* PooledActor = InactiveCharacterPool.Pop(EAllowShrinking::No);
        if (IsValid(PooledActor))
        {
            PooledActor->SetOwner(OwnerActor);
            PooledActor->SetInstigator(InstigatorPawn);
            PooledActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
            PooledActor->SetActorHiddenInGame(false);
            PooledActor->SetActorTickEnabled(true);
            PooledActor->SetActorEnableCollision(true);
            PooledActor->ResetToDefaultPoolState();

            if (UMoverComponent* Mover = PooledActor->FindComponentByClass<UMoverComponent>())
            {
                Mover->Activate(true);
            }
            if (UDynamicCustomizationComponent* Cust = PooledActor->GetCustomizationComponent())
            {
                Cust->InitializeCustomization(TemplateTag, ECharacterGender::Neutral);
            }
            SpawnConfiguredAIController(PooledActor, TemplateTag);
            return PooledActor;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = OwnerActor;
    SpawnParams.Instigator = InstigatorPawn;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    ADynamicCharacter* NewCharacter = GetWorld()->SpawnActor<ADynamicCharacter>(ADynamicCharacter::StaticClass(), SpawnTransform, SpawnParams);
    if (NewCharacter)
    {
        if (UDynamicCustomizationComponent* Cust = NewCharacter->GetCustomizationComponent())
        {
            Cust->InitializeCustomization(TemplateTag, ECharacterGender::Neutral);
        }
        SpawnConfiguredAIController(NewCharacter, TemplateTag);
    }
    return NewCharacter;
}

void ADynamicGameState::ReleaseCharacterToPool(ADynamicCharacter* ActorToReturn)
{
    if (!HasAuthority() || !IsValid(ActorToReturn)) return;
    if (AController* CharController = ActorToReturn->GetController())
    {
        CharController->Destroy();
    }
    ActorToReturn->SetActorHiddenInGame(true);
    ActorToReturn->SetActorTickEnabled(false);
    ActorToReturn->SetActorEnableCollision(false);
    ActorToReturn->SetActorLocation(FVector(0, 0, -10000.0f));
    InactiveCharacterPool.AddUnique(ActorToReturn);
}

void ADynamicGameState::WarmGameplayEffectCache()
{
    // Generates completely generic base effects once during startup. 
    // Effect details are driven exclusively by the DynamicGameplayEffectContext executing the dynamic definitions.
    GenericInstantEffect = NewObject<UGameplayEffect>(this, FName("DynamicGeneric_Instant"));
    GenericInstantEffect->DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayEffectExecutionDefinition ExecDefInst;
    ExecDefInst.CalculationClass = FindObject<UClass>(nullptr, TEXT("/Script/GDB.DynamicEffectExecution"));
    GenericInstantEffect->Executions.Add(ExecDefInst);

    GenericDurationEffect = NewObject<UGameplayEffect>(this, FName("DynamicGeneric_Duration"));
    GenericDurationEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    FGameplayEffectExecutionDefinition ExecDefDur;
    ExecDefDur.CalculationClass = ExecDefInst.CalculationClass;
    GenericDurationEffect->Executions.Add(ExecDefDur);

    GenericInfiniteEffect = NewObject<UGameplayEffect>(this, FName("DynamicGeneric_Infinite"));
    GenericInfiniteEffect->DurationPolicy = EGameplayEffectDurationType::Infinite;
    FGameplayEffectExecutionDefinition ExecDefInf;
    ExecDefInf.CalculationClass = ExecDefInst.CalculationClass;
    GenericInfiniteEffect->Executions.Add(ExecDefInf);
}

void ADynamicGameState::RegisterNetworkNode(UDynamicNetworkComponent* Node)
{
    if (!HasAuthority() || !Node) return;
    ActiveNetworkNodes.AddUnique(Node);
    bIsNetworkGraphDirty = true;
}

void ADynamicGameState::UnregisterNetworkNode(UDynamicNetworkComponent* Node)
{
    if (!HasAuthority() || !Node) return;
    ActiveNetworkNodes.Remove(Node);
    bIsNetworkGraphDirty = true;
}

void ADynamicGameState::MarkNetworkGraphDirty(UDynamicNetworkComponent* Node)
{
    bIsNetworkGraphDirty = true;
}

void ADynamicGameState::QueueNetworkBurstTransfer(UDynamicNetworkComponent* Node, FGameplayTag ResourceTag)
{
    if (!HasAuthority() || !Node) return;
    const FDynamicNetworkNodeDef* Def = Node->GetNodeDefinition();
    if (!Def) return;

    AActor* OwnerActor = Node->GetOwner();
    if (!OwnerActor) return;

    // Correctly fetch AttributeSet via UAbilitySystemComponent
    UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
    UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
    if (!AttrSet) return;

    for (const FDynamicNetworkResourceConfig& ResConfig : Def->SupportedResources)
    {
        if (ResConfig.ResourceTag == ResourceTag && ResConfig.bIsEnvironmentLinked)
        {
            const float CurrentVolume = AttrSet->GetDynamicAttributeValue(ResConfig.VolumeAttributeTag, 0);
            const float BurstAmount = FMath::Min(CurrentVolume, ResConfig.MaxBurstCapacity);
            
            if (BurstAmount > 0.0f)
            {
                ServerModifyEnvironmentalMana(OwnerActor->GetActorLocation(), ResourceTag, BurstAmount);
                AttrSet->SetDynamicAttributeValue(ResConfig.VolumeAttributeTag, CurrentVolume - BurstAmount, 0);
            }
        }
    }
}

void ADynamicGameState::RebuildNetworkGraphs()
{
    // Fast O(N+E) Breadth-First Search to label Disjoint Sets
    ComputedResourceGraphs.Empty();
    ActiveNetworkNodes.RemoveAll([](const TWeakObjectPtr<UDynamicNetworkComponent>& Node) { return !Node.IsValid(); });

    int32 CurrentGraphID = 0;
    TSet<UDynamicNetworkComponent*> Visited;

    for (const TWeakObjectPtr<UDynamicNetworkComponent>& WeakNode : ActiveNetworkNodes)
    {
        UDynamicNetworkComponent* RootNode = WeakNode.Get();
        if (!RootNode || Visited.Contains(RootNode)) continue;

        const FDynamicNetworkNodeDef* RootDef = RootNode->GetNodeDefinition();
        if (!RootDef) continue;

        for (const FDynamicNetworkResourceConfig& ResConfig : RootDef->SupportedResources)
        {
            const FGameplayTag TargetRes = ResConfig.ResourceTag;
            CurrentGraphID++;
            FResourceGraphState& Graph = ComputedResourceGraphs.Add(CurrentGraphID);

            TArray<UDynamicNetworkComponent*> Queue;
            Queue.Add(RootNode);
            Visited.Add(RootNode);

            int32 QueueIndex = 0; // Advancing index avoids O(N) array shifts
            while (QueueIndex < Queue.Num())
            {
                UDynamicNetworkComponent* Node = Queue[QueueIndex++];

                const FDynamicNetworkNodeDef* NodeDef = Node->GetNodeDefinition();
                if (NodeDef)
                {
                    for (const FDynamicNetworkResourceConfig& Config : NodeDef->SupportedResources)
                    {
                        if (Config.ResourceTag == TargetRes)
                        {
                            Node->ActiveGraphIDs.Add(TargetRes, CurrentGraphID);
                            
                            switch (Config.NodeType)
                            {
                                case ENetworkNodeType::Source: Graph.Sources.Add(Node); break;
                                case ENetworkNodeType::Consumer: Graph.Consumers.Add(Node); break;
                                case ENetworkNodeType::Storage: Graph.Storages.Add(Node); break;
                                case ENetworkNodeType::Transporter: Graph.Transporters.Add(Node); break;
                            }
                        }
                    }
                }

                // Traverse edges
                for (const FNetworkConnectionItem& Edge : Node->ReplicatedConnections.Items)
                {
                    if (UDynamicNetworkComponent* Connected = Edge.ConnectedNode.Get())
                    {
                        if (!Visited.Contains(Connected))
                        {
                            Visited.Add(Connected);
                            Queue.Add(Connected);
                        }
                    }
                }
            }
        }
    }
    bIsNetworkGraphDirty = false;
}

void ADynamicGameState::EvaluateNetworkGraphs(float DeltaSeconds)
{
    if (bIsNetworkGraphDirty)
    {
        RebuildNetworkGraphs();
    }

    // Mathematical Graph Resolution (Time-sliced batch execution)
    for (auto& Pair : ComputedResourceGraphs)
    {
        FResourceGraphState& Graph = Pair.Value;
        float TotalSupply = 0.0f;
        float TotalDemand = 0.0f;

        // 1. Accumulate Total Graph Supply & Environment Extractions
        for (const TWeakObjectPtr<UDynamicNetworkComponent>& WeakSource : Graph.Sources)
        {
            if (UDynamicNetworkComponent* Source = WeakSource.Get())
            {
                const FDynamicNetworkNodeDef* Def = Source->GetNodeDefinition();
                AActor* OwnerActor = Source->GetOwner();
                UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
                if (!Def || !AttrSet) continue;

                for (const FDynamicNetworkResourceConfig& Config : Def->SupportedResources)
                {
                    if (Config.NodeType != ENetworkNodeType::Source) continue;

                    const float AttrFlow = AttrSet->GetDynamicAttributeValue(Config.FlowRateAttributeTag, 0);
                    const float EffectiveFlow = (AttrFlow > 0.0f) ? AttrFlow : Config.MaxFlowRate;
                    const float PotentialPush = EffectiveFlow * DeltaSeconds;

                    if (Config.bIsEnvironmentLinked)
                    {
                        const float EnvAvailable = GetEnvironmentMetricAtLocation(OwnerActor->GetActorLocation(), Config.ResourceTag);
                        const float Extracted = FMath::Min(PotentialPush, EnvAvailable);
                        ServerModifyEnvironmentalMana(OwnerActor->GetActorLocation(), Config.ResourceTag, -Extracted);
                        TotalSupply += Extracted;
                    }
                    else
                    {
                        const float CurrentVol = AttrSet->GetDynamicAttributeValue(Config.VolumeAttributeTag, 0);
                        const float Pushed = FMath::Min(CurrentVol, PotentialPush);
                        AttrSet->SetDynamicAttributeValue(Config.VolumeAttributeTag, CurrentVol - Pushed, 0);
                        TotalSupply += Pushed;
                    }
                }
            }
        }

        // 2. Accumulate Graph Demand & Calculate Ratios
        for (const TWeakObjectPtr<UDynamicNetworkComponent>& WeakConsumer : Graph.Consumers)
        {
            if (UDynamicNetworkComponent* Consumer = WeakConsumer.Get())
            {
                const FDynamicNetworkNodeDef* Def = Consumer->GetNodeDefinition();
                AActor* OwnerActor = Consumer->GetOwner();
                UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
                if (!Def || !AttrSet) continue;

                for (const FDynamicNetworkResourceConfig& Config : Def->SupportedResources)
                {
                    if (Config.NodeType != ENetworkNodeType::Consumer) continue;

                    const float Capacity = AttrSet->GetDynamicAttributeValue(Config.MaxVolumeAttributeTag, 0);
                    const float CurrentVol = AttrSet->GetDynamicAttributeValue(Config.VolumeAttributeTag, 0);
                    const float AttrFlow = AttrSet->GetDynamicAttributeValue(Config.FlowRateAttributeTag, 0);
                    
                    const float SpaceLeft = FMath::Max(0.0f, Capacity - CurrentVol);
                    const float EffectiveFlow = (AttrFlow > 0.0f) ? AttrFlow : Config.MaxFlowRate;
                    const float FlowLimit = EffectiveFlow * DeltaSeconds;
                    
                    TotalDemand += FMath::Min(SpaceLeft, FlowLimit);
                }
            }
        }

        // 3. Buffer missing supply from Storage, or push excess to Storage
        float DemandDeficit = TotalDemand - TotalSupply;
        if (DemandDeficit > 0.0f)
        {
            for (const TWeakObjectPtr<UDynamicNetworkComponent>& WeakStorage : Graph.Storages)
            {
                if (UDynamicNetworkComponent* Storage = WeakStorage.Get())
                {
                    AActor* OwnerActor = Storage->GetOwner();
                    UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                    UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
                    const FDynamicNetworkNodeDef* Def = Storage->GetNodeDefinition();
                    if (!Def || !AttrSet) continue;

                    for (const FDynamicNetworkResourceConfig& Config : Def->SupportedResources)
                    {
                        if (Config.NodeType != ENetworkNodeType::Storage) continue;
                    
                        const float CurrentVol = AttrSet->GetDynamicAttributeValue(Config.VolumeAttributeTag, 0);
                        const float AttrFlow = AttrSet->GetDynamicAttributeValue(Config.FlowRateAttributeTag, 0);
                        const float EffectiveFlow = (AttrFlow > 0.0f) ? AttrFlow : Config.MaxFlowRate;
                        const float PullLimit = FMath::Min(CurrentVol, EffectiveFlow * DeltaSeconds);
                    
                        const float Pulled = FMath::Min(DemandDeficit, PullLimit);
                        AttrSet->SetDynamicAttributeValue(Config.VolumeAttributeTag, CurrentVol - Pulled, 0);
                    
                        TotalSupply += Pulled;
                        DemandDeficit -= Pulled;
                        if (DemandDeficit <= 0.0f) break;
                    }
                }
            }
        }

        // 4. Distribute finalized Supply to Consumers based on Pressure priority
        const float SatisfactionRatio = (TotalDemand > 0.0f) ? FMath::Clamp(TotalSupply / TotalDemand, 0.0f, 1.0f) : 1.0f;
        
        for (const TWeakObjectPtr<UDynamicNetworkComponent>& WeakConsumer : Graph.Consumers)
        {
            if (UDynamicNetworkComponent* Consumer = WeakConsumer.Get())
            {
                const FDynamicNetworkNodeDef* Def = Consumer->GetNodeDefinition();
                AActor* OwnerActor = Consumer->GetOwner();
                
                // CORRECTED: Fetch AttributeSet via AbilitySystemComponent
                UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                UDynamicAttributeSet* AttrSet = ASC ? const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))) : nullptr;
                
                if (!Def || !AttrSet) continue;

                for (const FDynamicNetworkResourceConfig& Config : Def->SupportedResources)
                {
                    if (Config.NodeType != ENetworkNodeType::Consumer) continue;

                    const float Capacity = AttrSet->GetDynamicAttributeValue(Config.MaxVolumeAttributeTag, 0);
                    const float CurrentVol = AttrSet->GetDynamicAttributeValue(Config.VolumeAttributeTag, 0);
                    const float AttrFlow = AttrSet->GetDynamicAttributeValue(Config.FlowRateAttributeTag, 0);
                    
                    const float SpaceLeft = FMath::Max(0.0f, Capacity - CurrentVol);
                    const float EffectiveFlow = (AttrFlow > 0.0f) ? AttrFlow : Config.MaxFlowRate;
                    const float FlowLimit = EffectiveFlow * DeltaSeconds;
                    
                    const float RequestedDemand = FMath::Min(SpaceLeft, FlowLimit);
                    const float ReceivedSupply = RequestedDemand * SatisfactionRatio;

                    if (Config.bIsEnvironmentLinked)
                    {
                        ServerModifyEnvironmentalMana(OwnerActor->GetActorLocation(), Config.ResourceTag, ReceivedSupply);
                    }
                    else
                    {
                        AttrSet->SetDynamicAttributeValue(Config.VolumeAttributeTag, CurrentVol + ReceivedSupply, 0);
                    }
                }
            }
        }
    }
}

bool ADynamicGameState::EvaluateBestDynamicBehavior(ADynamicCharacter* Character, FDynamicAIBehaviorDef& OutBestBehavior, float& OutBestScore) const
{
    OutBestScore = -MAX_flt;
    if (!Character || AIBehaviorMasterRegistry.Num() == 0) return false;

    UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
    UDynamicAttributeSet* AttrSet = Character->GetDynamicAttributeSet();
    UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>();

    FGameplayTagContainer SourceTags, TargetTags;
    if (ASC) ASC->GetOwnedGameplayTags(TargetTags);

    bool bFoundValid = false;

    for (const auto& Pair : AIBehaviorMasterRegistry)
    {
        const FDynamicAIBehaviorDef& Def = Pair.Value;

        // 1. Cooldown Check
        if (IsBehaviorOnCooldown(Character, Def.BehaviorTag))
        {
            continue;
        }

        // 2. Prerequisite Condition Tree
        if (!Def.PrerequisiteConditionTreeName.IsNone())
        {
            if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(Character, Def.PrerequisiteConditionTreeName))
            {
                if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, SourceTags, TargetTags))
                {
                    continue;
                }
            }
        }

        // 3. Dynamic Priority Evaluation (Base Priority + Weighted Attributes/Needs)
        float EvaluatedScore = Def.BasePriority;

        for (const auto& ScalarPair : Def.PriorityAttributeScalars.LocalLookupMap)
        {
            if (!ScalarPair.Key.IsValid()) continue;

            float AttributeValue = 0.0f;
            if (AttrSet)
            {
                AttributeValue = AttrSet->GetDynamicAttributeValue(ScalarPair.Key, 0);
            }

            // Check Needs if attribute is zero
            if (FMath::IsNearlyZero(AttributeValue) && SocialComp)
            {
                SocialComp->GetNeedValue(ScalarPair.Key, AttributeValue);
            }

            EvaluatedScore += (AttributeValue * ScalarPair.Value);
        }

        if (EvaluatedScore > OutBestScore)
        {
            OutBestScore = EvaluatedScore;
            OutBestBehavior = Def;
            bFoundValid = true;
        }
    }

    return bFoundValid;
}

void ADynamicGameState::RecordBehaviorExecutionCooldown(ADynamicCharacter* Character, FGameplayTag BehaviorTag, float CooldownSeconds)
{
    if (!Character || !BehaviorTag.IsValid() || CooldownSeconds <= 0.0f) return;

    UWorld* World = GetWorld();
    const double ExpiryTime = (World ? World->GetTimeSeconds() : 0.0) + CooldownSeconds;

    TWeakObjectPtr<ADynamicCharacter> WeakChar(Character);
    CharacterBehaviorCooldowns.FindOrAdd(WeakChar).Add(BehaviorTag, ExpiryTime);
}

bool ADynamicGameState::IsBehaviorOnCooldown(ADynamicCharacter* Character, FGameplayTag BehaviorTag) const
{
    if (!Character || !BehaviorTag.IsValid()) return false;

    TWeakObjectPtr<ADynamicCharacter> WeakChar(Character);
    if (const TMap<FGameplayTag, double>* CooldownMap = CharacterBehaviorCooldowns.Find(WeakChar))
    {
        if (const double* ExpiryTime = CooldownMap->Find(BehaviorTag))
        {
            const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            return (*ExpiryTime > CurrentTime);
        }
    }
    return false;
}