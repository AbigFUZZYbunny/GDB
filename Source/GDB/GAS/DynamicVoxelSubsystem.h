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
    bool ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voxels")
    ADynamicVoxelChunkActor* GetChunkAtLocation(const FVector& WorldLocation) const;

    void InitializeVoxelGenerationData();
    void GenerateChunkDataAsync(FIntPoint ChunkCoord, int32 ChunkSize, float VoxelSize, TWeakObjectPtr<ADynamicVoxelChunkActor> RequestingChunk);

private:
    UPROPERTY()
    TMap<FIntPoint, ADynamicVoxelChunkActor*> LoadedChunks;

    FVoxelMapDataCache MapDataCache;
    
    float VoxelWorldSize = 100.0f;
    float LODUpdateTimer = 0.0f;
    bool bHasInitializedMapData = false;

    void ProcessVoxelEnvironmentFeedback(const FVector& WorldLocation, FGameplayTag OldVoxelTag, FGameplayTag NewVoxelTag);
    void UpdateChunkLODs();
    bool LoadTextureDataToCache(TSoftObjectPtr<UTexture2D> TexturePtr, TArray<FColor>& OutData, int32& OutWidth, int32& OutHeight);
};