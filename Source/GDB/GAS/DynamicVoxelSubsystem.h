// DynamicVoxelSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicVoxelChunkActor.h"
#include "DynamicVoxelSubsystem.generated.h"

struct FVoxelMapDataCache
{
    TArray<FColor> SurfaceHeightData;
    TArray<FColor> BiomeData;
    TArray<FColor> CaveData;
    TArray<FColor> EnergyData;
    
    int32 Width = 0;
    int32 Height = 0;

    bool IsValid() const { return Width > 0 && Height > 0; }
};

UCLASS()
class GDB_API UDynamicVoxelSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { return TStatId(); }

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Voxels")
    bool ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator, FGameplayTag& OutMinedVoxelTag);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voxels")
    ADynamicVoxelChunkActor* GetActiveChunkAtLocation(const FVector& WorldLocation) const;

    void InitializeVoxelGenerationData();
    void GenerateChunkDataAsync(FIntPoint ChunkCoord, int32 ChunkSize, float VoxelSize, TWeakObjectPtr<ADynamicVoxelChunkActor> RequestingChunk);

    // Flushes all loaded chunk modifications to JSON for persistence
    void SerializeWorldVoxelState(struct FWorldPersistenceSnapshot& OutSnapshot);
    void RestoreWorldVoxelState(const struct FWorldPersistenceSnapshot& InSnapshot);
    
    // Calculates the exact grid-snapped transform for placing a new block against a mined surface
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voxels|Building")
    bool CalculateVoxelPlacementTransform(const FHitResult& VoxelHit, FTransform& OutTransform) const;

    // Determines if a specific world bounds overlaps with any solid virtual/loaded voxels
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voxels|Building")
    bool IsVoxelSpaceFree(const FVector& WorldLocation) const;

protected:
    UPROPERTY(EditAnywhere, Category = "Dynamic GAS|Voxels|Streaming")
    float HighResRadius = 10000.0f; // 100m
    UPROPERTY(EditAnywhere, Category = "Dynamic GAS|Voxels|Streaming")
    float LowResRadius = 30000.0f;  // 300m
    UPROPERTY(EditAnywhere, Category = "Dynamic GAS|Voxels|Streaming")
    float VirtualNearbyRadius = 60000.0f; // 600m
    UPROPERTY(EditAnywhere, Category = "Dynamic GAS|Voxels|Streaming")
    float VirtualDistantRadius = 120000.0f; // 1.2km

private:
    // Virtualized memory map of only chunks that have been modified or loaded
    UPROPERTY()
    TMap<FIntPoint, FVoxelChunkPersistentData> VirtualChunkRegistry;

    // Physical actors currently spawned in the world
    UPROPERTY()
    TMap<FIntPoint, ADynamicVoxelChunkActor*> ActiveSpawnedChunks;

    FVoxelMapDataCache MapDataCache;
    
    float VoxelWorldSize = 100.0f;
    float StreamingTimer = 0.0f;
    bool bHasInitializedMapData = false;

    // Evaluates streaming thresholds and spawns/despawns actors seamlessly
    void ProcessChunkStreamingAndLODs();

    // Runs time-sliced generative updates on background threads for virtualized chunks
    void ProcessTemporalGenerativeUpdates(double CurrentWorldTime);
    void ExecuteGenerativePassAsync(FIntPoint ChunkCoord, EVoxelLODState LodState);

    void ProcessVoxelEnvironmentFeedback(const FVector& WorldLocation, FGameplayTag OldVoxelTag, FGameplayTag NewVoxelTag);
    bool LoadTextureDataToCache(TSoftObjectPtr<UTexture2D> TexturePtr, TArray<FColor>& OutData, int32& OutWidth, int32& OutHeight);
    
    FIntPoint GetChunkCoordFromLocation(const FVector& WorldLocation) const;
};