// DynamicBuildingChunkActor.cpp
#include "DynamicBuildingChunkActor.h"
#include "DynamicGameState.h"
#include "DynamicVoxelSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Async/Async.h"

ADynamicBuildingChunkActor::ADynamicBuildingChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = false;
    NetPriority = 1.0f;
    SetNetUpdateFrequency(2.0f);
    
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    ReplicatedStructures.OwningChunk = this;
}

void ADynamicBuildingChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ADynamicBuildingChunkActor, ChunkCoord, COND_InitialOnly);
    DOREPLIFETIME(ADynamicBuildingChunkActor, ReplicatedStructures);
}

void ADynamicBuildingChunkActor::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedStructures.OwningChunk = this;
}

void ADynamicBuildingChunkActor::InitializeChunk(FIntPoint InCoord, float InChunkSize)
{
    ChunkCoord = InCoord;
    ChunkWorldSize = InChunkSize;
    
    SetActorLocation(FVector(ChunkCoord.X * ChunkWorldSize, ChunkCoord.Y * ChunkWorldSize, 0.0f));
    SetNetCullDistanceSquared(FMath::Square(ChunkWorldSize * 2.0f));
}

void ADynamicBuildingChunkActor::OnRep_ChunkCoord()
{
    UpdateInstancedMeshes();
}

void ADynamicBuildingChunkActor::OnRep_StructureDeltas()
{
    UpdateInstancedMeshes();
}

void ADynamicBuildingChunkActor::HandleSingleStructureReplicated(const FStructureInstanceItem& Item)
{
    UpdateInstancedMeshes();
}

UHierarchicalInstancedStaticMeshComponent* ADynamicBuildingChunkActor::GetOrCreateHISM(FGameplayTag PieceTag, FGameplayTag MaterialTag)
{
    // Composite ID to separate wood vs stone walls, etc.
    const uint64 CompKey = (static_cast<uint64>(PieceTag.GetTagName().GetComparisonIndex().ToUnstableInt()) << 32) | 
                           static_cast<uint64>(MaterialTag.GetTagName().GetComparisonIndex().ToUnstableInt());

    if (UHierarchicalInstancedStaticMeshComponent** Found = InstancedMeshPool.Find(CompKey))
    {
        return *Found;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    const FStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(PieceTag);
    if (!PieceDef) return nullptr;

    UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    
    if (UStaticMesh* Mesh = PieceDef->MeshAsset.LoadSynchronous())
    {
        NewHISM->SetStaticMesh(Mesh);
    }
    
    // In production, resolve Dynamic Material Instance based on MaterialTag here

    InstancedMeshPool.Add(CompKey, NewHISM);
    return NewHISM;
}

void ADynamicBuildingChunkActor::UpdateInstancedMeshes()
{
    for (auto& Pair : InstancedMeshPool)
    {
        if (Pair.Value) Pair.Value->ClearInstances();
    }

    for (const FStructureInstanceItem& Item : ReplicatedStructures.Items)
    {
        if (Item.CurrentHP <= 0.0f) continue;

        if (UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Item.PieceTag, Item.MaterialTag))
        {
            // Calculate tint/sag based on SupportValue/HP for visual feedback
            HISM->AddInstanceWorldSpace(Item.WorldTransform);
        }
    }
}

void ADynamicBuildingChunkActor::RecalculateStructuralIntegrityAsync()
{
    if (!HasAuthority()) return;
    if (bIsCalculatingIntegrity)
    {
        bGraphUpdateQueued = true;
        return;
    }

    bIsCalculatingIntegrity = true;
    bGraphUpdateQueued = false;

    TArray<FStructureInstanceItem> StateCapture = ReplicatedStructures.Items;
    TWeakObjectPtr<ADynamicBuildingChunkActor> WeakThis(this);
    UWorld* World = GetWorld();
    UDynamicVoxelSubsystem* VoxelSub = World ? World->GetSubsystem<UDynamicVoxelSubsystem>() : nullptr;
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    if (!VoxelSub || !GS)
    {
        bIsCalculatingIntegrity = false;
        return;
    }

    // Capture configs for thread safety
    TMap<FGameplayTag, FStructurePieceDef> Pieces = GS->StructurePieceRegistry;
    TMap<FGameplayTag, FStructureMaterialDef> Materials = GS->StructureMaterialRegistry;

    // Check grounding synchronously before threading
    TArray<bool> GroundedStates;
    GroundedStates.SetNum(StateCapture.Num());
    for (int32 i = 0; i < StateCapture.Num(); ++i)
    {
        GroundedStates[i] = !VoxelSub->IsVoxelSpaceFree(StateCapture[i].WorldTransform.GetLocation());
    }

    Async(EAsyncExecution::ThreadPool, [StateCapture, GroundedStates, Pieces, Materials, WeakThis]() mutable {
        
        TMap<FGuid, TArray<FGuid>> LocalGraph;
        TMap<FGuid, float> ResolvedSupport;
        TQueue<FGuid> EvaluationQueue;

        // 1. Build Adjacency Graph via Spatial Math (O(N^2) localized to chunk)
        for (int32 i = 0; i < StateCapture.Num(); ++i)
        {
            FStructureInstanceItem& Node = StateCapture[i];
            const FStructurePieceDef* Def = Pieces.Find(Node.PieceTag);
            const float Reach = Def ? Def->OverlapConnectionRadius : 150.0f;
            const FVector LocA = Node.WorldTransform.GetLocation();

            Node.bIsGrounded = GroundedStates[i];

            if (Node.bIsGrounded)
            {
                const FStructureMaterialDef* Mat = Materials.Find(Node.MaterialTag);
                Node.SupportValue = Mat ? Mat->SupportStrength : 100.0f;
                ResolvedSupport.Add(Node.InstanceID, Node.SupportValue);
                EvaluationQueue.Enqueue(Node.InstanceID);
            }

            for (int32 j = i + 1; j < StateCapture.Num(); ++j)
            {
                if (FVector::DistSquared(LocA, StateCapture[j].WorldTransform.GetLocation()) <= FMath::Square(Reach))
                {
                    LocalGraph.FindOrAdd(Node.InstanceID).Add(StateCapture[j].InstanceID);
                    LocalGraph.FindOrAdd(StateCapture[j].InstanceID).Add(Node.InstanceID);
                }
            }
        }

        // 2. BFS Support Propagation
        FGuid CurrentNode;
        while (EvaluationQueue.Dequeue(CurrentNode))
        {
            float CurrentSupport = ResolvedSupport[CurrentNode];

            if (TArray<FGuid>* Neighbors = LocalGraph.Find(CurrentNode))
            {
                for (const FGuid& NeighborID : *Neighbors)
                {
                    FStructureInstanceItem* NeighborItem = StateCapture.FindByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == NeighborID; });
                    if (!NeighborItem) continue;

                    const FStructurePieceDef* NDef = Pieces.Find(NeighborItem->PieceTag);
                    const FStructureMaterialDef* NMat = Materials.Find(NeighborItem->MaterialTag);
                    
                    const float Mass = NDef ? NDef->BaseMass : 10.0f;
                    const float Density = NMat ? NMat->StructuralDensity : 1.0f;
                    const float PropagatedSupport = CurrentSupport - (Mass * Density);

                    if (PropagatedSupport > 0.0f)
                    {
                        if (!ResolvedSupport.Contains(NeighborID) || ResolvedSupport[NeighborID] < PropagatedSupport)
                        {
                            ResolvedSupport.Add(NeighborID, PropagatedSupport);
                            NeighborItem->SupportValue = PropagatedSupport;
                            EvaluationQueue.Enqueue(NeighborID);
                        }
                    }
                }
            }
        }

        // 3. Mark Unsupported for breakage
        for (FStructureInstanceItem& Item : StateCapture)
        {
            if (!ResolvedSupport.Contains(Item.InstanceID))
            {
                Item.SupportValue = -1.0f; // Snaps
            }
        }

        Async(EAsyncExecution::TaskGraphMainThread, [WeakThis, StateCapture, LocalGraph]() mutable {
            if (ADynamicBuildingChunkActor* StrongThis = WeakThis.Get())
            {
                StrongThis->AdjacencyGraph = LocalGraph;
                
                bool bModified = false;
                for (int32 i = StrongThis->ReplicatedStructures.Items.Num() - 1; i >= 0; --i)
                {
                    FStructureInstanceItem& RealItem = StrongThis->ReplicatedStructures.Items[i];
                    FStructureInstanceItem* CalcItem = StateCapture.FindByPredicate([&](const FStructureInstanceItem& C) { return C.InstanceID == RealItem.InstanceID; });

                    if (CalcItem)
                    {
                        RealItem.SupportValue = CalcItem->SupportValue;
                        RealItem.bIsGrounded = CalcItem->bIsGrounded;
                        
                        // Physics Breakage
                        if (RealItem.SupportValue < 0.0f)
                        {
                            RealItem.CurrentHP = 0.0f; 
                            bModified = true;
                        }
                        StrongThis->ReplicatedStructures.MarkItemDirty(RealItem);
                    }
                }

                if (bModified)
                {
                    // Cleanup broken pieces
                    StrongThis->ReplicatedStructures.Items.RemoveAll([](const FStructureInstanceItem& I) { return I.CurrentHP <= 0.0f; });
                    StrongThis->ReplicatedStructures.MarkArrayDirty();
                    StrongThis->UpdateInstancedMeshes();
                }

                StrongThis->bIsCalculatingIntegrity = false;
                if (StrongThis->bGraphUpdateQueued) StrongThis->RecalculateStructuralIntegrityAsync();
            }
        });
    });
}

void FStructureInstanceItem::PreReplicatedRemove(const FFastStructureInstanceArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleStructureReplicated(*this);
}
void FStructureInstanceItem::PostReplicatedAdd(const FFastStructureInstanceArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleStructureReplicated(*this);
}
void FStructureInstanceItem::PostReplicatedChange(const FFastStructureInstanceArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleStructureReplicated(*this);
}