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
    LoadedChunks.Empty();
    MapDataCache = FVoxelMapDataCache();
    Super::Deinitialize();
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

    LODUpdateTimer += DeltaTime;
    if (LODUpdateTimer >= 1.0f)
    {
        LODUpdateTimer = 0.0f;
        UpdateChunkLODs();
    }
}

void UDynamicVoxelSubsystem::UpdateChunkLODs()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer) return;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC || !PC->GetPawn()) return;

    const FVector PlayerLoc = PC->GetPawn()->GetActorLocation();

    for (const auto& Pair : LoadedChunks)
    {
        if (ADynamicVoxelChunkActor* Chunk = Pair.Value)
        {
            const float DistSq = FVector::DistSquared(PlayerLoc, Chunk->GetActorLocation());
            int32 DesiredLOD = 1;

            if (DistSq > FMath::Square(15000.0f)) DesiredLOD = 4;
            else if (DistSq > FMath::Square(7500.0f)) DesiredLOD = 2;

            Chunk->SetLODLevel(DesiredLOD);
        }
    }
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

ADynamicVoxelChunkActor* UDynamicVoxelSubsystem::GetChunkAtLocation(const FVector& WorldLocation) const
{
    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    FIntPoint Coord(
        FMath::FloorToInt(WorldLocation.X / ChunkWorldWidth),
        FMath::FloorToInt(WorldLocation.Y / ChunkWorldWidth)
    );
    return LoadedChunks.FindRef(Coord);
}

bool UDynamicVoxelSubsystem::ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator)
{
    UWorld* World = GetWorld();
    if (!World) return false;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    ADynamicVoxelChunkActor* TargetChunk = GetChunkAtLocation(WorldLocation);
    if (!TargetChunk || !TargetChunk->HasAuthority()) return false;

    const FVector LocalPos = TargetChunk->GetTransform().InverseTransformPosition(WorldLocation);
    const int32 LocalX = FMath::FloorToInt(LocalPos.X / VoxelWorldSize);
    const int32 LocalY = FMath::FloorToInt(LocalPos.Y / VoxelWorldSize);
    const int32 LocalZ = FMath::FloorToInt(LocalPos.Z / VoxelWorldSize);

    if (LocalX < 0 || LocalY < 0 || LocalZ < 0 || LocalX >= ADynamicVoxelChunkActor::CHUNK_SIZE || LocalY >= ADynamicVoxelChunkActor::CHUNK_SIZE || LocalZ >= ADynamicVoxelChunkActor::CHUNK_SIZE)
    {
        return false;
    }

    const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);
    
    uint8 OldTypeID = TargetChunk->CachedVoxelData[VoxelIndex];
    uint8 NewTypeID = 0; 
    FGameplayTag OldVoxelTag; 

    if (const FDynamicVoxelTypeDef* VoxelDef = GS->VoxelMasterRegistry.Find(NewVoxelTypeTag))
    {
        NewTypeID = 1; 
        TargetChunk->UpdateVoxelData(VoxelIndex, NewTypeID, VoxelDef->MaxHP);
    }
    else
    {
        TargetChunk->UpdateVoxelData(VoxelIndex, 0, 0); 
    }

    ProcessVoxelEnvironmentFeedback(WorldLocation, OldVoxelTag, NewVoxelTypeTag);
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