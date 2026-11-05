#include "DynamicVoxelSubsystem.h"
#include "DynamicGameState.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"

void UDynamicVoxelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicVoxelSubsystem::Deinitialize()
{
    ActiveSpawnedChunks.Empty();
    VirtualChunkRegistry.Empty();
    MapDataCache.Reset();
    Super::Deinitialize();
}

// ============================================================================
// WORLD PERSISTENCE (Cloud MMO / Dedicated Server Optimization)
// ============================================================================
void UDynamicVoxelSubsystem::SerializeWorldVoxelState(FWorldPersistenceSnapshot& OutSnapshot)
{
    // 1. Sync all active spawned chunks back into the virtual registry before saving
    for (auto& Pair : ActiveSpawnedChunks)
    {
        if (ADynamicVoxelChunkActor* Chunk = Pair.Value)
        {
            FVoxelChunkPersistentData& Data = VirtualChunkRegistry.FindOrAdd(Pair.Key);
            Data.ChunkCoord = Pair.Key;
            Data.SavedDeltas = Chunk->ReplicatedDeltas.Items;
        }
    }

    // 2. Extract all virtualized chunk deltas to the snapshot
    // In a Cloud MMO, this payload is partitioned and sent to a distributed Database (e.g. MongoDB/Redis)
    for (const auto& Pair : VirtualChunkRegistry)
    {
        if (Pair.Value.SavedDeltas.Num() > 0)
        {
            FChunkDeltaSnapshot ChunkSnap;
            ChunkSnap.ChunkCoord = Pair.Key;
            ChunkSnap.SavedDeltas = Pair.Value.SavedDeltas;
            OutSnapshot.ModifiedVoxelChunks.Add(ChunkSnap);
        }
    }
}

void UDynamicVoxelSubsystem::RestoreWorldVoxelState(const FWorldPersistenceSnapshot& InSnapshot)
{
    for (const FChunkDeltaSnapshot& ChunkSnap : InSnapshot.ModifiedVoxelChunks)
    {
        // Load directly into the math abstraction
        FVoxelChunkPersistentData& Data = VirtualChunkRegistry.FindOrAdd(ChunkSnap.ChunkCoord);
        Data.ChunkCoord = ChunkSnap.ChunkCoord;
        Data.SavedDeltas = ChunkSnap.SavedDeltas;

        // If the chunk happens to be physically spawned right now, push the data to it visually
        if (ADynamicVoxelChunkActor* Chunk = ActiveSpawnedChunks.FindRef(ChunkSnap.ChunkCoord))
        {
            Chunk->ReplicatedDeltas.Items = ChunkSnap.SavedDeltas;
            Chunk->ReplicatedDeltas.MarkArrayDirty();
            Chunk->OnRep_VoxelDeltas();
        }
    }
}

// ============================================================================
// BUILDING SYSTEM INTEGRATION
// ============================================================================
bool UDynamicVoxelSubsystem::CalculateVoxelPlacementTransform(const FHitResult& VoxelHit, FTransform& OutTransform) const
{
    if (!VoxelHit.bBlockingHit) return false;

    // To place a block, we take the impact point and push it OUTWARD along the face normal 
    // by half a voxel size to find the center of the adjacent empty cell.
    const FVector PlacementLoc = VoxelHit.ImpactPoint + (VoxelHit.ImpactNormal * (VoxelWorldSize * 0.5f));

    // Mathematical Grid Snapping
    const float X = FMath::FloorToFloat(PlacementLoc.X / VoxelWorldSize) * VoxelWorldSize + (VoxelWorldSize * 0.5f);
    const float Y = FMath::FloorToFloat(PlacementLoc.Y / VoxelWorldSize) * VoxelWorldSize + (VoxelWorldSize * 0.5f);
    const float Z = FMath::FloorToFloat(PlacementLoc.Z / VoxelWorldSize) * VoxelWorldSize + (VoxelWorldSize * 0.5f);

    OutTransform = FTransform(FRotator::ZeroRotator, FVector(X, Y, Z));
    return true;
}

bool UDynamicVoxelSubsystem::IsVoxelSpaceFree(const FVector& WorldLocation) const
{
    // A vital check for the Building System so players cannot place prefabs/walls inside terrain
    const FIntPoint ChunkCoord = GetChunkCoordFromLocation(WorldLocation);
    
    // If it's loaded as an actor, checking collision is handled by the physics engine.
    // If it's virtual, we query the mathematical dataset.
    if (const FVoxelChunkPersistentData* Data = VirtualChunkRegistry.Find(ChunkCoord))
    {
        const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
        const FVector ChunkWorldOrigin(ChunkCoord.X * ChunkWorldWidth, ChunkCoord.Y * ChunkWorldWidth, 0.0f);
        const FVector LocalPos = WorldLocation - ChunkWorldOrigin;

        const int32 LocalX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
        const int32 LocalY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
        const int32 LocalZ = FMath::Clamp(FMath::FloorToInt(LocalPos.Z / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);

        const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);

        for (const FVoxelDeltaItem& Delta : Data->SavedDeltas)
        {
            if (Delta.VoxelIndex == VoxelIndex)
            {
                return Delta.VoxelTypeID == 0; // 0 = Air (Free space)
            }
        }
    }
    
    // Fallback: If no deltas exist, space is assumed solid if below BaseElevation
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS)
    {
        return WorldLocation.Z > GS->VoxelGenerationConfig.BaseElevation;
    }
    
    return true; 
}

void UDynamicVoxelSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UWorld* World = GetWorld();
    if (World && World->GetNetMode() != NM_Client && !bHasInitializedMapData)
    {
        ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
        if (GS && (GS->MaterialMasterRegistry.Num() > 0 || GS->VoxelGenerationConfig.SurfaceHeightMap.IsValid()))
        {
            InitializeVoxelGenerationData();
            bHasInitializedMapData = true;
        }
    }

    if (!World || World->GetNetMode() == NM_Client) return;

    // Run heavy spatial operations time-sliced (once per second)
    StreamingTimer += DeltaTime;
    if (StreamingTimer >= 1.0f)
    {
        StreamingTimer = 0.0f;
        ProcessChunkStreamingAndLODs();
        ProcessTemporalGenerativeUpdates(World->GetTimeSeconds());
    }
}

FIntPoint UDynamicVoxelSubsystem::GetChunkCoordFromLocation(const FVector& WorldLocation) const
{
    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    return FIntPoint(
        FMath::FloorToInt(WorldLocation.X / ChunkWorldWidth),
        FMath::FloorToInt(WorldLocation.Y / ChunkWorldWidth)
    );
}

ADynamicVoxelChunkActor* UDynamicVoxelSubsystem::GetActiveChunkAtLocation(const FVector& WorldLocation) const
{
    return ActiveSpawnedChunks.FindRef(GetChunkCoordFromLocation(WorldLocation));
}

void UDynamicVoxelSubsystem::ProcessChunkStreamingAndLODs()
{
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<FVector> PlayerLocations;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (APawn* P = It->Get()->GetPawn())
        {
            PlayerLocations.Add(P->GetActorLocation());
        }
    }

    if (PlayerLocations.Num() == 0) return;

    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;

    // 1. Identify which coordinates SHOULD be spawned based on player radius
    TSet<FIntPoint> RequiredSpawnCoords;
    for (const FVector& PLoc : PlayerLocations)
    {
        FIntPoint CenterCoord = GetChunkCoordFromLocation(PLoc);
        int32 RadiusInChunks = FMath::CeilToInt(LowResRadius / ChunkWorldWidth);

        for (int32 Y = -RadiusInChunks; Y <= RadiusInChunks; ++Y)
        {
            for (int32 X = -RadiusInChunks; X <= RadiusInChunks; ++X)
            {
                FIntPoint EvalCoord = CenterCoord + FIntPoint(X, Y);
                FVector ChunkOrigin(EvalCoord.X * ChunkWorldWidth, EvalCoord.Y * ChunkWorldWidth, 0.0f);
                float DistSq = FVector::DistSquared(PLoc, ChunkOrigin);

                // Ensure the chunk's persistent record exists
                FVoxelChunkPersistentData& ChunkData = VirtualChunkRegistry.FindOrAdd(EvalCoord);
                ChunkData.ChunkCoord = EvalCoord;

                if (DistSq <= FMath::Square(HighResRadius))
                {
                    RequiredSpawnCoords.Add(EvalCoord);
                    ChunkData.CurrentLODState = EVoxelLODState::ActiveHighRes;
                }
                else if (DistSq <= FMath::Square(LowResRadius))
                {
                    RequiredSpawnCoords.Add(EvalCoord);
                    ChunkData.CurrentLODState = EVoxelLODState::ActiveLowRes;
                }
                else if (DistSq <= FMath::Square(VirtualNearbyRadius))
                {
                    ChunkData.CurrentLODState = EVoxelLODState::VirtualNearby;
                }
                else if (DistSq <= FMath::Square(VirtualDistantRadius))
                {
                    ChunkData.CurrentLODState = EVoxelLODState::VirtualDistant;
                }
                else
                {
                    ChunkData.CurrentLODState = EVoxelLODState::VirtualHorizon;
                }
            }
        }
    }

    // 2. Despawn chunks that are no longer required
    TArray<FIntPoint> ChunksToDespawn;
    for (auto& Pair : ActiveSpawnedChunks)
    {
        if (!RequiredSpawnCoords.Contains(Pair.Key))
        {
            ChunksToDespawn.Add(Pair.Key);
        }
    }

    for (const FIntPoint& Coord : ChunksToDespawn)
    {
        ADynamicVoxelChunkActor* OldChunk = ActiveSpawnedChunks[Coord];
        if (OldChunk)
        {
            // Sync any final player modifications back to the virtual registry before destruction
            if (FVoxelChunkPersistentData* Data = VirtualChunkRegistry.Find(Coord))
            {
                Data->SavedDeltas = OldChunk->ReplicatedDeltas.Items;
                Data->bIsSpawnedAsActor = false;
            }
            OldChunk->Destroy();
        }
        ActiveSpawnedChunks.Remove(Coord);
    }

    // 3. Spawn newly required chunks
    for (const FIntPoint& Coord : RequiredSpawnCoords)
    {
        if (!ActiveSpawnedChunks.Contains(Coord))
        {
            FVector SpawnLoc(Coord.X * ChunkWorldWidth, Coord.Y * ChunkWorldWidth, 0.0f);
            FActorSpawnParameters Params;
            Params.Owner = GetWorld()->GetGameState();
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            if (ADynamicVoxelChunkActor* NewChunk = World->SpawnActor<ADynamicVoxelChunkActor>(ADynamicVoxelChunkActor::StaticClass(), FTransform(SpawnLoc), Params))
            {
                NewChunk->InitializeChunk(Coord, ADynamicVoxelChunkActor::CHUNK_SIZE, VoxelWorldSize);
                
                // Inject the persistent deltas so it looks exactly as the player left it
                if (FVoxelChunkPersistentData* Data = VirtualChunkRegistry.Find(Coord))
                {
                    NewChunk->ReplicatedDeltas.Items = Data->SavedDeltas;
                    NewChunk->ReplicatedDeltas.MarkArrayDirty();
                    Data->bIsSpawnedAsActor = true;
                }
                
                ActiveSpawnedChunks.Add(Coord, NewChunk);
            }
        }
        else
        {
            // Update LOD on already spawned chunks dynamically
            if (FVoxelChunkPersistentData* Data = VirtualChunkRegistry.Find(Coord))
            {
                ActiveSpawnedChunks[Coord]->SetLODLevel(Data->CurrentLODState == EVoxelLODState::ActiveHighRes ? 1 : 2);
            }
        }
    }
}

void UDynamicVoxelSubsystem::ProcessTemporalGenerativeUpdates(double CurrentWorldTime)
{
    if (VirtualChunkRegistry.Num() == 0) return;
    
    // Time-slice quota to prevent server hitches
    const int32 MaxGenerativeUpdatesPerTick = 10;
    int32 ProcessedCount = 0;
    int32 CheckedCount = 0;

    // OPTIMIZATION: Extract keys to array to allow cyclical, persistent iteration
    TArray<FIntPoint> Keys;
    VirtualChunkRegistry.GenerateKeyArray(Keys);

    if (GenerativeUpdateIndex >= Keys.Num())
    {
        GenerativeUpdateIndex = 0;
    }

    while (CheckedCount < Keys.Num() && ProcessedCount < MaxGenerativeUpdatesPerTick)
    {
        FIntPoint Coord = Keys[GenerativeUpdateIndex];
        FVoxelChunkPersistentData* ChunkData = VirtualChunkRegistry.Find(Coord);
        
        if (ChunkData && !ChunkData->bIsSpawnedAsActor)
        {
            double RequiredInterval = 0.0;
            switch (ChunkData->CurrentLODState)
            {
            case EVoxelLODState::VirtualNearby:   RequiredInterval = 3600.0 * 1.0; break;
            case EVoxelLODState::VirtualDistant:  RequiredInterval = 3600.0 * 6.0; break;
            case EVoxelLODState::VirtualHorizon:  RequiredInterval = 3600.0 * 24.0; break;
            default: break;
            }

            if (RequiredInterval > 0.0 && (CurrentWorldTime - ChunkData->LastGenerativeUpdateWorldTime) >= RequiredInterval)
            {
                ChunkData->LastGenerativeUpdateWorldTime = CurrentWorldTime;
                ExecuteGenerativePassAsync(Coord, ChunkData->CurrentLODState);
                ProcessedCount++;
            }
        }

        GenerativeUpdateIndex = (GenerativeUpdateIndex + 1) % Keys.Num();
        CheckedCount++;
    }
}

void UDynamicVoxelSubsystem::ExecuteGenerativePassAsync(FIntPoint ChunkCoord, EVoxelLODState LodState)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FVoxelChunkPersistentData* DataPtr = VirtualChunkRegistry.Find(ChunkCoord);
    if (!DataPtr) return;

    TArray<uint8> ReconstructedGrid;
    ReconstructedGrid.Init(0, ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);
    
    for (const FVoxelDeltaItem& Delta : DataPtr->SavedDeltas)
    {
        if (ReconstructedGrid.IsValidIndex(Delta.VoxelIndex))
        {
            ReconstructedGrid[Delta.VoxelIndex] = Delta.VoxelTypeID;
        }
    }

    TMap<uint8, FDynamicMaterialDef> FastVoxelDefs;
    uint8 Counter = 1;
    for (const auto& Pair : GS->MaterialMasterRegistry)
    {
        FastVoxelDefs.Add(Counter, Pair.Value);
        Counter++;
    }

    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    const FVector ChunkCenter(ChunkCoord.X * ChunkWorldWidth + (ChunkWorldWidth * 0.5f), ChunkCoord.Y * ChunkWorldWidth + (ChunkWorldWidth * 0.5f), 0.0f);
    static const FGameplayTag TempTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.Temperature"), false);
    const float AmbientTemp = GS->GetEnvironmentMetricAtLocation(ChunkCenter, TempTag);

    TWeakObjectPtr<UDynamicVoxelSubsystem> WeakThis(this);

    Async(EAsyncExecution::ThreadPool, [ChunkCoord, ReconstructedGrid, FastVoxelDefs, AmbientTemp, WeakThis]() mutable {
        
        TArray<uint8> NextGrid = ReconstructedGrid;
        bool bModified = false;
        const int32 ChunkSize = ADynamicVoxelChunkActor::CHUNK_SIZE;

        auto GetIdx = [ChunkSize](int32 x, int32 y, int32 z) -> int32 {
            return x + (y * ChunkSize) + (z * ChunkSize * ChunkSize);
        };

        auto CountSolidNeighbors = [&](int32 X, int32 Y, int32 Z) -> int32 {
            int32 Solids = 0;
            if (X > 0 && ReconstructedGrid[GetIdx(X - 1, Y, Z)] != 0) Solids++;
            if (X < ChunkSize - 1 && ReconstructedGrid[GetIdx(X + 1, Y, Z)] != 0) Solids++;
            if (Y > 0 && ReconstructedGrid[GetIdx(X, Y - 1, Z)] != 0) Solids++;
            if (Y < ChunkSize - 1 && ReconstructedGrid[GetIdx(X, Y + 1, Z)] != 0) Solids++;
            if (Z > 0 && ReconstructedGrid[GetIdx(X, Y, Z - 1)] != 0) Solids++;
            if (Z < ChunkSize - 1 && ReconstructedGrid[GetIdx(X, Y, Z + 1)] != 0) Solids++;
            return Solids;
        };

        for (int32 Z = 1; Z < ChunkSize; ++Z) 
        { 
            for (int32 Y = 0; Y < ChunkSize; ++Y) 
            {
                for (int32 X = 0; X < ChunkSize; ++X) 
                {
                    const int32 Idx = GetIdx(X, Y, Z);
                    const uint8 Type = ReconstructedGrid[Idx];
                    if (Type == 0) continue; 

                    const FDynamicMaterialDef* Def = FastVoxelDefs.Find(Type);
                    if (!Def) continue;

                    // 1. Phase Transmutation (e.g. Ice Melting)
                    if (Def->TransmutedMaterialTag.IsValid() && AmbientTemp >= Def->PhaseChangeTemperature)
                    {
                        uint8 TransmutedID = 0;
                        for (const auto& DefPair : FastVoxelDefs)
                        {
                            if (DefPair.Value.MaterialTag == Def->TransmutedMaterialTag)
                            {
                                TransmutedID = DefPair.Key;
                                break;
                            }
                        }
                        NextGrid[Idx] = TransmutedID;
                        bModified = true;
                        continue; 
                    }

                    // 2. Encapsulation / Adhesion Check
                    if (Def->bIsGravityAffected)
                    {
                        const int32 ActiveNeighbors = CountSolidNeighbors(X, Y, Z);
                        
                        // If it lacks the necessary adhesion/encapsulation to remain stuck to a wall, it breaks free
                        if (ActiveNeighbors < Def->RequiredAdhesionNeighbors)
                        {
                            const int32 BelowIdx = GetIdx(X, Y, Z - 1);
                            
                            // Straight fall
                            if (ReconstructedGrid[BelowIdx] == 0)
                            {
                                NextGrid[BelowIdx] = Type;
                                NextGrid[Idx] = 0;
                                bModified = true;
                                continue;
                            }
                            
                            // Loose Material Dispersal (Sand, Water, Loose Rock rolling down hills)
                            int32 FallX = X, FallY = Y;
                            bool bCanSlide = false;
                            
                            if (X > 0 && ReconstructedGrid[GetIdx(X - 1, Y, Z)] == 0 && ReconstructedGrid[GetIdx(X - 1, Y, Z - 1)] == 0) { FallX = X - 1; bCanSlide = true; }
                            else if (X < ChunkSize - 1 && ReconstructedGrid[GetIdx(X + 1, Y, Z)] == 0 && ReconstructedGrid[GetIdx(X + 1, Y, Z - 1)] == 0) { FallX = X + 1; bCanSlide = true; }
                            else if (Y > 0 && ReconstructedGrid[GetIdx(X, Y - 1, Z)] == 0 && ReconstructedGrid[GetIdx(X, Y - 1, Z - 1)] == 0) { FallY = Y - 1; bCanSlide = true; }
                            else if (Y < ChunkSize - 1 && ReconstructedGrid[GetIdx(X, Y + 1, Z)] == 0 && ReconstructedGrid[GetIdx(X, Y + 1, Z - 1)] == 0) { FallY = Y + 1; bCanSlide = true; }

                            if (bCanSlide)
                            {
                                NextGrid[GetIdx(FallX, FallY, Z - 1)] = Type;
                                NextGrid[Idx] = 0;
                                bModified = true;
                            }
                        }
                    }
                }
            }
        }

        if (bModified)
        {
            Async(EAsyncExecution::TaskGraphMainThread, [WeakThis, ChunkCoord, NextGrid, FastVoxelDefs]() mutable {
                if (UDynamicVoxelSubsystem* StrongThis = WeakThis.Get())
                {
                    FVoxelChunkPersistentData& Data = StrongThis->VirtualChunkRegistry.FindOrAdd(ChunkCoord);
                    Data.SavedDeltas.Empty(); 

                    for (int32 i = 0; i < NextGrid.Num(); ++i)
                    {
                        if (NextGrid[i] != 0) 
                        {
                            FVoxelDeltaItem Item;
                            Item.VoxelIndex = i;
                            Item.VoxelTypeID = NextGrid[i];
                            if (const FDynamicMaterialDef* MDef = FastVoxelDefs.Find(Item.VoxelTypeID))
                            {
                                Item.CurrentHP = MDef->BaseMaxHP;
                            }
                            Data.SavedDeltas.Add(Item);
                        }
                    }

                    if (ADynamicVoxelChunkActor* ChunkActor = StrongThis->ActiveSpawnedChunks.FindRef(ChunkCoord))
                    {
                        ChunkActor->ReplicatedDeltas.Items = Data.SavedDeltas;
                        ChunkActor->ReplicatedDeltas.MarkArrayDirty();
                        ChunkActor->OnRep_VoxelDeltas();
                    }
                }
            });
        }
    });
}

bool UDynamicVoxelSubsystem::LoadTextureDataToCache(TSoftObjectPtr<UTexture2D> TexturePtr, TArray<FColor>& OutData, int32& OutWidth, int32& OutHeight)
{
    UTexture2D* Texture = TexturePtr.LoadSynchronous();
    if (!Texture) return false;

#if WITH_EDITOR
    Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    Texture->SRGB = false;
    Texture->UpdateResource();
#endif

    if (FTexturePlatformData* PlatformData = Texture->GetPlatformData())
    {
        if (PlatformData->Mips.Num() > 0)
        {
            FTexture2DMipMap& Mip = PlatformData->Mips[0];
            void* Data = Mip.BulkData.Lock(LOCK_READ_ONLY);
            OutWidth = Mip.SizeX;
            OutHeight = Mip.SizeY;
            const int32 TotalPixels = OutWidth * OutHeight;
            OutData.SetNumUninitialized(TotalPixels);
            FMemory::Memcpy(OutData.GetData(), Data, TotalPixels * sizeof(FColor));
            Mip.BulkData.Unlock();
            return true;
        }
    }
    return false;
}

void UDynamicVoxelSubsystem::InitializeVoxelGenerationData()
{
    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return;

    const FDynamicVoxelGenerationConfig& Config = GS->VoxelGenerationConfig;

    MapDataCache = MakeShared<FVoxelMapDataCache>();
    int32 W = 0, H = 0;
    
    if (LoadTextureDataToCache(Config.SurfaceHeightMap, MapDataCache->SurfaceHeightData, W, H))
    {
        MapDataCache->Width = W;
        MapDataCache->Height = H;
        
        LoadTextureDataToCache(Config.BiomeMap, MapDataCache->BiomeData, W, H);
        LoadTextureDataToCache(Config.CaveDensityMap, MapDataCache->CaveData, W, H);
        LoadTextureDataToCache(Config.EnergyLevelMap, MapDataCache->EnergyData, W, H);
    }
}

void UDynamicVoxelSubsystem::GenerateChunkDataAsync(FIntPoint ChunkCoord, int32 ChunkSize, float VoxelSize, TWeakObjectPtr<ADynamicVoxelChunkActor> RequestingChunk)
{
    if (!MapDataCache.IsValid() || !MapDataCache->IsValid() || !RequestingChunk.IsValid()) return;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return;

    // OPTIMIZATION: Thread-safe O(1) ref-count copy prevents massive memory stalls
    TSharedPtr<FVoxelMapDataCache> LocalCache = MapDataCache;
    FDynamicVoxelGenerationConfig LocalConfig = GS->VoxelGenerationConfig;

    Async(EAsyncExecution::ThreadPool, [ChunkCoord, ChunkSize, VoxelSize, LocalCache, LocalConfig, RequestingChunk]() {
        
        TArray<uint8> GeneratedVoxelData;
        GeneratedVoxelData.Init(0, ChunkSize * ChunkSize * ChunkSize);
        
        const float WorldXOffset = ChunkCoord.X * ChunkSize * VoxelSize;
        const float WorldYOffset = ChunkCoord.Y * ChunkSize * VoxelSize;

        for (int32 X = 0; X < ChunkSize; ++X)
        {
            for (int32 Y = 0; Y < ChunkSize; ++Y)
            {
                const float WorldX = WorldXOffset + (X * VoxelSize);
                const float WorldY = WorldYOffset + (Y * VoxelSize);

                const float U = FMath::Clamp((WorldX + (LocalConfig.MapWorldBounds * 0.5f)) / LocalConfig.MapWorldBounds, 0.0f, 1.0f);
                const float V = FMath::Clamp((WorldY + (LocalConfig.MapWorldBounds * 0.5f)) / LocalConfig.MapWorldBounds, 0.0f, 1.0f);
                
                const int32 PixelX = FMath::Clamp(FMath::RoundToInt(U * (LocalCache->Width - 1)), 0, LocalCache->Width - 1);
                const int32 PixelY = FMath::Clamp(FMath::RoundToInt(V * (LocalCache->Height - 1)), 0, LocalCache->Height - 1);
                const int32 PixelIdx = PixelY * LocalCache->Width + PixelX;

                const FColor HeightPixel = LocalCache->SurfaceHeightData[PixelIdx];
                const float HeightNormalized = HeightPixel.R / 255.0f;
                const float TargetWorldHeight = LocalConfig.BaseElevation + (HeightNormalized * LocalConfig.WorldHeightScale);
                const int32 HeightZ = FMath::Clamp(FMath::FloorToInt(TargetWorldHeight / VoxelSize), 0, ChunkSize - 1);

                uint8 SurfaceVoxelID = 1; 
                uint8 SubsurfaceVoxelID = 2; 
                
                if (LocalCache->BiomeData.IsValidIndex(PixelIdx))
                {
                    const FColor BiomeColor = LocalCache->BiomeData[PixelIdx];
                    for (int i = 0; i < LocalConfig.BiomeColorMappings.Num(); ++i)
                    {
                        if (BiomeColor == LocalConfig.BiomeColorMappings[i].MapColorKey)
                        {
                            SurfaceVoxelID = 1; 
                            SubsurfaceVoxelID = 2;
                            break;
                        }
                    }
                }

                float CaveDensity = 0.0f;
                if (LocalCache->CaveData.IsValidIndex(PixelIdx))
                {
                    CaveDensity = LocalCache->CaveData[PixelIdx].R / 255.0f;
                }

                for (int32 Z = 0; Z <= HeightZ; ++Z)
                {
                    const float DepthFactor = FMath::Clamp((HeightZ - Z) / 20.0f, 0.0f, 1.0f);
                    const bool bIsCave = (CaveDensity > 0.5f) && (DepthFactor > 0.2f) && (DepthFactor < 0.8f);

                    if (bIsCave) continue;

                    int32 Idx = X + (Y * ChunkSize) + (Z * ChunkSize * ChunkSize);
                    if (Z == HeightZ) GeneratedVoxelData[Idx] = SurfaceVoxelID;
                    else GeneratedVoxelData[Idx] = SubsurfaceVoxelID;
                }
            }
        }

        Async(EAsyncExecution::TaskGraphMainThread, [RequestingChunk, GeneratedVoxelData = MoveTemp(GeneratedVoxelData)]() mutable {
            if (ADynamicVoxelChunkActor* ChunkActor = RequestingChunk.Get())
            {
                ChunkActor->ReceiveGeneratedData(MoveTemp(GeneratedVoxelData));
            }
        });
    });
}

bool UDynamicVoxelSubsystem::ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator, FGameplayTag& OutMinedVoxelTag)
{
    OutMinedVoxelTag = FGameplayTag::EmptyTag;
    
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return false;

    const FIntPoint ChunkCoord = GetChunkCoordFromLocation(WorldLocation);
    FVoxelChunkPersistentData& PersistentData = VirtualChunkRegistry.FindOrAdd(ChunkCoord);
    PersistentData.ChunkCoord = ChunkCoord;

    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    const FVector ChunkWorldOrigin(ChunkCoord.X * ChunkWorldWidth, ChunkCoord.Y * ChunkWorldWidth, 0.0f);
    const FVector LocalPos = WorldLocation - ChunkWorldOrigin;

    const int32 LocalX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalZ = FMath::Clamp(FMath::FloorToInt(LocalPos.Z / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);

    const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);
    
    uint8 NewTypeID = 0; 
    uint8 MaxHP = 0;

    if (NewVoxelTypeTag.IsValid())
    {
        uint8 Counter = 1;
        for (const auto& MatPair : GS->MaterialMasterRegistry)
        {
            if (MatPair.Key == NewVoxelTypeTag)
            {
                NewTypeID = Counter;
                MaxHP = static_cast<uint8>(FMath::Clamp(MatPair.Value.BaseMaxHP, 1, 255));
                break;
            }
            Counter++;
        }
    }

    if (ADynamicVoxelChunkActor* TargetChunk = ActiveSpawnedChunks.FindRef(ChunkCoord))
    {
        const uint8 OldTypeID = TargetChunk->CachedVoxelData.IsValidIndex(VoxelIndex) ? TargetChunk->CachedVoxelData[VoxelIndex] : 0;
        
        uint8 MatCounter = 1;
        for (const auto& MatPair : GS->MaterialMasterRegistry)
        {
            if (MatCounter == OldTypeID)
            {
                OutMinedVoxelTag = MatPair.Value.DefaultYieldArchetype;
                break;
            }
            MatCounter++;
        }

        TargetChunk->UpdateVoxelData(VoxelIndex, NewTypeID, MaxHP);
    }
    else
    {
        FVoxelDeltaItem NewDelta;
        NewDelta.VoxelIndex = VoxelIndex;
        NewDelta.VoxelTypeID = NewTypeID;
        NewDelta.CurrentHP = MaxHP;

        const int32 FoundIdx = PersistentData.SavedDeltas.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
            return Item.VoxelIndex == VoxelIndex;
        });

        if (FoundIdx != INDEX_NONE) PersistentData.SavedDeltas[FoundIdx] = NewDelta;
        else PersistentData.SavedDeltas.Add(NewDelta);
    }

    ProcessVoxelEnvironmentFeedback(WorldLocation, OutMinedVoxelTag, NewVoxelTypeTag);
    return true;
}

void UDynamicVoxelSubsystem::ProcessVoxelEnvironmentFeedback(const FVector& WorldLocation, FGameplayTag OldVoxelTag, FGameplayTag NewVoxelTag)
{
    // Hooks seamlessly into your environment elemental grids
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const int32 GridCellIndex = GS->GetEnvironmentCellIndexAtLocation(WorldLocation);
    if (!GS->GetEnvironmentReadBuffer().IsValidIndex(GridCellIndex)) return;
}

bool UDynamicVoxelSubsystem::ServerApplyMechanicalStressAtLocation(
    const FVector& WorldLocation, 
    const FGameplayTagContainer& ImpactOrDamageTags, 
    float RawForceMagnitude, 
    AActor* Instigator, 
    FGameplayTag& OutYieldArchetype, 
    bool& bOutDestroyed)
{
    OutYieldArchetype = FGameplayTag::EmptyTag;
    bOutDestroyed = false;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS || RawForceMagnitude <= 0.0f) return false;

    const FIntPoint ChunkCoord = GetChunkCoordFromLocation(WorldLocation);
    FVoxelChunkPersistentData& PersistentData = VirtualChunkRegistry.FindOrAdd(ChunkCoord);
    PersistentData.ChunkCoord = ChunkCoord;

    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    const FVector ChunkWorldOrigin(ChunkCoord.X * ChunkWorldWidth, ChunkCoord.Y * ChunkWorldWidth, 0.0f);
    const FVector LocalPos = WorldLocation - ChunkWorldOrigin;

    const int32 LocalX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalZ = FMath::Clamp(FMath::FloorToInt(LocalPos.Z / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);

    const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);

    uint8 CurrentTypeID = 0;
    int32 CurrentHP = 100;

    ADynamicVoxelChunkActor* TargetChunk = ActiveSpawnedChunks.FindRef(ChunkCoord);
    if (TargetChunk)
    {
        CurrentTypeID = TargetChunk->CachedVoxelData.IsValidIndex(VoxelIndex) ? TargetChunk->CachedVoxelData[VoxelIndex] : 0;
        
        const int32 FoundIdx = TargetChunk->ReplicatedDeltas.Items.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
            return Item.VoxelIndex == VoxelIndex;
        });
        if (FoundIdx != INDEX_NONE)
        {
            CurrentHP = TargetChunk->ReplicatedDeltas.Items[FoundIdx].CurrentHP;
        }
    }
    else
    {
        const int32 FoundIdx = PersistentData.SavedDeltas.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
            return Item.VoxelIndex == VoxelIndex;
        });
        if (FoundIdx != INDEX_NONE)
        {
            CurrentTypeID = PersistentData.SavedDeltas[FoundIdx].VoxelTypeID;
            CurrentHP = PersistentData.SavedDeltas[FoundIdx].CurrentHP;
        }
    }

    if (CurrentTypeID == 0) return false; // Air cannot be stressed

    // Resolve Material Definition from fast mapped index
    FGameplayTag MaterialTag;
    uint8 Counter = 1;
    const FDynamicMaterialDef* MatDef = nullptr;

    for (const auto& MatPair : GS->MaterialMasterRegistry)
    {
        if (Counter == CurrentTypeID)
        {
            MaterialTag = MatPair.Key;
            MatDef = &MatPair.Value;
            break;
        }
        Counter++;
    }

    if (!MatDef) return false;

    // Evaluate Material Science Mechanical Failure
    FGameplayTag DominantYield;
    const float CalculatedDamage = MatDef->CalculateMechanicalDamage(ImpactOrDamageTags, RawForceMagnitude, DominantYield);
    if (CalculatedDamage <= 0.0f) return false; // Force did not exceed the material's strength thresholds

    CurrentHP -= FMath::CeilToInt(CalculatedDamage);

    if (CurrentHP <= 0)
    {
        bOutDestroyed = true;
        OutYieldArchetype = DominantYield.IsValid() ? DominantYield : MatDef->DefaultYieldArchetype;

        // Voxel is replaced with Air (ID 0)
        if (TargetChunk)
        {
            TargetChunk->UpdateVoxelData(VoxelIndex, 0, 0);
        }
        else
        {
            FVoxelDeltaItem AirDelta;
            AirDelta.VoxelIndex = VoxelIndex;
            AirDelta.VoxelTypeID = 0;
            AirDelta.CurrentHP = 0;

            const int32 FoundIdx = PersistentData.SavedDeltas.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
                return Item.VoxelIndex == VoxelIndex;
            });
            if (FoundIdx != INDEX_NONE) PersistentData.SavedDeltas[FoundIdx] = AirDelta;
            else PersistentData.SavedDeltas.Add(AirDelta);
        }

        ProcessVoxelEnvironmentFeedback(WorldLocation, MaterialTag, FGameplayTag::EmptyTag);
    }
    else
    {
        // Partial fracture / health reduction
        bOutDestroyed = false;
        if (TargetChunk)
        {
            TargetChunk->UpdateVoxelData(VoxelIndex, CurrentTypeID, static_cast<uint8>(FMath::Clamp(CurrentHP, 1, 255)));
        }
        else
        {
            const int32 FoundIdx = PersistentData.SavedDeltas.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
                return Item.VoxelIndex == VoxelIndex;
            });
            if (FoundIdx != INDEX_NONE)
            {
                PersistentData.SavedDeltas[FoundIdx].CurrentHP = static_cast<uint8>(FMath::Clamp(CurrentHP, 1, 255));
            }
        }
    }

    return true;
}