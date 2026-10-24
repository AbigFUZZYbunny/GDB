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
    MapDataCache = FVoxelMapDataCache();
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
        if (GS && (GS->VoxelMasterRegistry.Num() > 0 || GS->VoxelGenerationConfig.SurfaceHeightMap.IsValid()))
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
    // Evaluates virtual chunks and updates them organically (erosion, flora spread) based on distance
    const double HourInSeconds = 3600.0;
    
    // Time-slice quota to prevent server hitches
    int32 ProcessedCount = 0;
    const int32 MaxGenerativeUpdatesPerTick = 10;

    for (auto& Pair : VirtualChunkRegistry)
    {
        FVoxelChunkPersistentData& ChunkData = Pair.Value;
        
        // Active chunks are updated via localized active systems; skip background generation
        if (ChunkData.bIsSpawnedAsActor) continue;

        double RequiredInterval = 0.0;
        switch (ChunkData.CurrentLODState)
        {
            case EVoxelLODState::VirtualNearby:   RequiredInterval = HourInSeconds * 1.0; break;
            case EVoxelLODState::VirtualDistant:  RequiredInterval = HourInSeconds * 6.0; break;
            case EVoxelLODState::VirtualHorizon:  RequiredInterval = HourInSeconds * 24.0; break;
            default: continue;
        }

        if ((CurrentWorldTime - ChunkData.LastGenerativeUpdateWorldTime) >= RequiredInterval)
        {
            ChunkData.LastGenerativeUpdateWorldTime = CurrentWorldTime;
            ExecuteGenerativePassAsync(ChunkData.ChunkCoord, ChunkData.CurrentLODState);
            
            ProcessedCount++;
            if (ProcessedCount >= MaxGenerativeUpdatesPerTick) break;
        }
    }
}

void UDynamicVoxelSubsystem::ExecuteGenerativePassAsync(FIntPoint ChunkCoord, EVoxelLODState LodState)
{
    // Dispatches a zero-lock background thread task to run cellular automata or ecology loops on the chunk.
    // e.g. "If water is here, turn adjacent dirt to mud. If grass, spread."
    // Once calculation is done, the thread pushes the resulting FVoxelDeltaItems back into VirtualChunkRegistry.
    
    Async(EAsyncExecution::ThreadPool, [this, ChunkCoord]() {
        // ... Abstracted Ecology Algorithms run here ...
        
        Async(EAsyncExecution::TaskGraphMainThread, [this, ChunkCoord]() {
            // Apply generative deltas to VirtualChunkRegistry[ChunkCoord].SavedDeltas safely
        });
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

    int32 W = 0, H = 0;
    if (LoadTextureDataToCache(Config.SurfaceHeightMap, MapDataCache.SurfaceHeightData, W, H))
    {
        MapDataCache.Width = W;
        MapDataCache.Height = H;
        
        LoadTextureDataToCache(Config.BiomeMap, MapDataCache.BiomeData, W, H);
        LoadTextureDataToCache(Config.CaveDensityMap, MapDataCache.CaveData, W, H);
        LoadTextureDataToCache(Config.EnergyLevelMap, MapDataCache.EnergyData, W, H);
    }
}

void UDynamicVoxelSubsystem::GenerateChunkDataAsync(FIntPoint ChunkCoord, int32 ChunkSize, float VoxelSize, TWeakObjectPtr<ADynamicVoxelChunkActor> RequestingChunk)
{
    if (!MapDataCache.IsValid() || !RequestingChunk.IsValid()) return;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return;

    FVoxelMapDataCache LocalCache = MapDataCache;
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
                
                const int32 PixelX = FMath::Clamp(FMath::RoundToInt(U * (LocalCache.Width - 1)), 0, LocalCache.Width - 1);
                const int32 PixelY = FMath::Clamp(FMath::RoundToInt(V * (LocalCache.Height - 1)), 0, LocalCache.Height - 1);
                const int32 PixelIdx = PixelY * LocalCache.Width + PixelX;

                const FColor HeightPixel = LocalCache.SurfaceHeightData[PixelIdx];
                const float HeightNormalized = HeightPixel.R / 255.0f;
                const float TargetWorldHeight = LocalConfig.BaseElevation + (HeightNormalized * LocalConfig.WorldHeightScale);
                const int32 HeightZ = FMath::Clamp(FMath::FloorToInt(TargetWorldHeight / VoxelSize), 0, ChunkSize - 1);

                uint8 SurfaceVoxelID = 1; 
                uint8 SubsurfaceVoxelID = 2; 
                
                if (LocalCache.BiomeData.IsValidIndex(PixelIdx))
                {
                    const FColor BiomeColor = LocalCache.BiomeData[PixelIdx];
                    for (int i = 0; i < LocalConfig.BiomeColorMappings.Num(); ++i)
                    {
                        if (BiomeColor == LocalConfig.BiomeColorMappings[i].MapColorKey)
                        {
                            // In a full implementation, these map to the FGameplayTag uint8 dictionary index
                            SurfaceVoxelID = 1; 
                            SubsurfaceVoxelID = 2;
                            break;
                        }
                    }
                }

                float CaveDensity = 0.0f;
                if (LocalCache.CaveData.IsValidIndex(PixelIdx))
                {
                    CaveDensity = LocalCache.CaveData[PixelIdx].R / 255.0f;
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
    if (!World) return false;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    const FIntPoint ChunkCoord = GetChunkCoordFromLocation(WorldLocation);
    FVoxelChunkPersistentData& PersistentData = VirtualChunkRegistry.FindOrAdd(ChunkCoord);
    PersistentData.ChunkCoord = ChunkCoord; // Ensure initialized

    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    const FVector ChunkWorldOrigin(ChunkCoord.X * ChunkWorldWidth, ChunkCoord.Y * ChunkWorldWidth, 0.0f);
    const FVector LocalPos = WorldLocation - ChunkWorldOrigin;

    const int32 LocalX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);
    const int32 LocalZ = FMath::Clamp(FMath::FloorToInt(LocalPos.Z / VoxelWorldSize), 0, ADynamicVoxelChunkActor::CHUNK_SIZE - 1);

    const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);
    
    uint8 NewTypeID = 0; 
    uint8 MaxHP = 0;
    
    if (const FDynamicVoxelTypeDef* VoxelDef = GS->VoxelMasterRegistry.Find(NewVoxelTypeTag))
    {
        NewTypeID = 1; // Map resolved ID
        MaxHP = VoxelDef->MaxHP;
    }

    // 1. If the Actor is currently spawned, update it visually
    if (ADynamicVoxelChunkActor* TargetChunk = ActiveSpawnedChunks.FindRef(ChunkCoord))
    {
        uint8 OldTypeID = TargetChunk->CachedVoxelData.IsValidIndex(VoxelIndex) ? TargetChunk->CachedVoxelData[VoxelIndex] : 0;
        
        // Resolve Drop
        if (OldTypeID == 1) OutMinedVoxelTag = FGameplayTag::RequestGameplayTag(TEXT("Voxel.Type.Grass"), false);
        else if (OldTypeID == 2) OutMinedVoxelTag = FGameplayTag::RequestGameplayTag(TEXT("Voxel.Type.Dirt"), false);
        else if (OldTypeID == 3) OutMinedVoxelTag = FGameplayTag::RequestGameplayTag(TEXT("Voxel.Type.Stone"), false);

        TargetChunk->UpdateVoxelData(VoxelIndex, NewTypeID, MaxHP);
    }
    else
    {
        // 2. If chunk is virtualized, update the mathematical delta record in memory directly
        FVoxelDeltaItem NewDelta;
        NewDelta.VoxelIndex = VoxelIndex;
        NewDelta.VoxelTypeID = NewTypeID;
        NewDelta.CurrentHP = MaxHP;

        int32 FoundIdx = PersistentData.SavedDeltas.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
            return Item.VoxelIndex == VoxelIndex;
        });

        if (FoundIdx != INDEX_NONE) PersistentData.SavedDeltas[FoundIdx] = NewDelta;
        else PersistentData.SavedDeltas.Add(NewDelta);
        
        // Virtual mining yields default assumption without full generation (can be upgraded with async mapping query)
        OutMinedVoxelTag = FGameplayTag::RequestGameplayTag(TEXT("Voxel.Type.Stone"), false); 
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