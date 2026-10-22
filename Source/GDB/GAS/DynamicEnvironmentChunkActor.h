// DynamicEnvironmentChunkActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "DynamicGASTypes.h"
#include "DynamicEnvironmentChunkActor.generated.h"

class ADynamicGameState;

/**
 * Spatialized actor representing an 8x8 slice of the environmental grid.
 * Replicates coordinates on initial spawn only, and grid cells via NetCullDistance proximity.
 */
UCLASS()
class GDB_API ADynamicEnvironmentChunkActor : public AInfo
{
    GENERATED_BODY()

public:
    ADynamicEnvironmentChunkActor();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(ReplicatedUsing = OnRep_ChunkCoord, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    UFUNCTION()
    void OnRep_ChunkCoord();

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    int32 ChunkDimX = 8;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Environment")
    int32 ChunkDimY = 8;

    UPROPERTY(ReplicatedUsing = OnRep_ChunkCells)
    FFastEnvironmentGridArray ChunkCells;

    UFUNCTION()
    void OnRep_ChunkCells();

    void HandleSingleCellReplicated(const FEnvironmentGridCellItem& Item);

    // Initial setup called by ADynamicGameState on server authority
    void InitializeChunk(FIntPoint InCoord, int32 InDimX, int32 InDimY, float CellSizeWorldUnits, const FVector& WorldOrigin);

    // Server push to update a cell in this chunk and mark it dirty for replication
    void UpdateCellData(int32 GlobalCellIndex, const FEnvironmentGridCellItem& NewCellData);

    const FEnvironmentGridCellItem* FindCellItemByGlobalIndex(int32 GlobalCellIndex) const;

private:
    TMap<int32, int32> GlobalToLocalCellIndexMap;
};