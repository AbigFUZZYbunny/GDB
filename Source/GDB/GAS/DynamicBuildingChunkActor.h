// DynamicBuildingChunkActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DynamicBuildingChunkActor.generated.h"

UCLASS()
class GDB_API ADynamicBuildingChunkActor : public AActor
{
    GENERATED_BODY()

public:
    ADynamicBuildingChunkActor();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(ReplicatedUsing = OnRep_ChunkCoord, BlueprintReadOnly, Category = "Building")
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    UFUNCTION()
    void OnRep_ChunkCoord();

    UPROPERTY(ReplicatedUsing = OnRep_StructureDeltas)
    FFastStructureInstanceArray ReplicatedStructures;

    UFUNCTION()
    void OnRep_StructureDeltas();

    void HandleSingleStructureReplicated(const FStructureInstanceItem& Item);
    void InitializeChunk(FIntPoint InCoord, float InChunkSize);
    
    void RecalculateStructuralIntegrityAsync();
    void UpdateInstancedMeshes();

    // Core Physical Building Methods
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building|Construction")
    bool ServerProvideMaterialToGhost(const FGuid& InstanceID, AActor* WorkerActor);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building|Construction")
    void ServerApplyConstructionLabor(const FGuid& InstanceID, AActor* WorkerActor, float LaborAmount);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building|Damage")
    void ServerDamageBuildingInstance(const FGuid& InstanceID, float DamageAmount, const FVector& HitLocation, const FVector& HitNormal, AActor* DamageInstigator);

    static constexpr float CHUNK_SIZE_UNITS = 5000.0f;
    TMap<FGuid, TArray<FGuid>> AdjacencyGraph;
    TMap<UHierarchicalInstancedStaticMeshComponent*, TMap<int32, FGuid>> InstanceHitMap;

private:
    float ChunkWorldSize = 5000.0f;
    bool bGraphUpdateQueued = false;
    bool bIsCalculatingIntegrity = false;

    UPROPERTY(Transient)
    TMap<uint64, UHierarchicalInstancedStaticMeshComponent*> InstancedMeshPool;

    UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(FGameplayTag PieceTag, FGameplayTag MaterialTag);
};