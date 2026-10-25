// DynamicBuildingChunkActor.cpp
#include "DynamicBuildingChunkActor.h"
#include "DynamicGameState.h"
#include "DynamicVoxelSubsystem.h"
#include "DynamicPhysicsActor.h"
#include "Net/UnrealNetwork.h"
#include "Async/Async.h"
#include "Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Runtime/Engine/Public/Net/UnrealNetwork.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicCharacter.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicBuildingSubsystem.h"

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

void ADynamicBuildingChunkActor::OnRep_ChunkCoord() { UpdateInstancedMeshes(); }
void ADynamicBuildingChunkActor::OnRep_StructureDeltas() { UpdateInstancedMeshes(); }
void ADynamicBuildingChunkActor::HandleSingleStructureReplicated(const FStructureInstanceItem& Item) { UpdateInstancedMeshes(); }

UHierarchicalInstancedStaticMeshComponent* ADynamicBuildingChunkActor::GetOrCreateHISM(FGameplayTag PieceTag, FGameplayTag MaterialTag)
{
    const uint64 CompKey = (static_cast<uint64>(PieceTag.GetTagName().GetComparisonIndex().ToUnstableInt()) << 32) | 
                           static_cast<uint64>(MaterialTag.GetTagName().GetComparisonIndex().ToUnstableInt());

    if (UHierarchicalInstancedStaticMeshComponent** Found = InstancedMeshPool.Find(CompKey)) return *Found;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(PieceTag);
    if (!PieceDef) return nullptr;

    UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    
    // CustomData[0] = Stress/Integrity Color, CustomData[1] = Construction State (0.0 = Ghost Blueprint)
    NewHISM->NumCustomDataFloats = 2;
    
    if (UStaticMesh* Mesh = PieceDef->MeshAsset.LoadSynchronous())
    {
        NewHISM->SetStaticMesh(Mesh);
    }
    InstancedMeshPool.Add(CompKey, NewHISM);
    return NewHISM;
}

void ADynamicBuildingChunkActor::UpdateInstancedMeshes()
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    for (auto& Pair : InstancedMeshPool)
    {
        if (Pair.Value) Pair.Value->ClearInstances();
    }
    
    InstanceHitMap.Empty();

    for (const FStructureInstanceItem& Item : ReplicatedStructures.Items)
    {
        if (Item.CurrentHP <= 0.0f) continue;

        if (UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Item.PieceTag, Item.MaterialTag))
        {
            const int32 InstIdx = HISM->AddInstance(Item.WorldTransform);
            InstanceHitMap.FindOrAdd(HISM).Add(InstIdx, Item.InstanceID);

            float MaxSupport = 100.0f;
            if (const FDynamicStructureMaterialDef* MatDef = GS->StructureMaterialRegistry.Find(Item.MaterialTag))
            {
                MaxSupport = MatDef->SupportStrength;
            }
            
            const float StressNormalized = FMath::Clamp(Item.SupportValue / MaxSupport, 0.0f, 1.0f);
            
            HISM->SetCustomDataValue(InstIdx, 0, StressNormalized);
            HISM->SetCustomDataValue(InstIdx, 1, Item.bIsGhost ? 0.0f : 1.0f); // Feeds hologram material shader
        }
    }
}

bool ADynamicBuildingChunkActor::ServerProvideMaterialToGhost(const FGuid& InstanceID, AActor* WorkerActor)
{
    if (!HasAuthority() || !WorkerActor) return false;

    int32 FoundIdx = ReplicatedStructures.Items.IndexOfByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == InstanceID; });
    if (FoundIdx == INDEX_NONE) return false;

    FStructureInstanceItem& Item = ReplicatedStructures.Items[FoundIdx];
    if (!Item.bIsGhost) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(Item.PieceTag);
    if (!PieceDef) return false;

    UDynamicEquipmentComponent* EquipComp = WorkerActor->FindComponentByClass<UDynamicEquipmentComponent>();
    UDynamicContainerComponent* InvComp = WorkerActor->FindComponentByClass<UDynamicContainerComponent>();
    if (!EquipComp || !InvComp) return false;

    // Advanced builders pull from vicinity automatically
    // FIX: UDynamicAttributeSet is a UAttributeSet (UObject), not a UActorComponent. Query via ASC or ADynamicCharacter.
    bool bAllowProximityPull = false;
    const UDynamicAttributeSet* AttrSet = nullptr;
    if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(WorkerActor))
    {
        AttrSet = DynChar->GetDynamicAttributeSet();
    }
    else if (UAbilitySystemComponent* ASC = WorkerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        AttrSet = Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    }

    if (AttrSet)
    {
        static const FGameplayTag BuildSkillTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Skill.Building"), false);
        bAllowProximityPull = (AttrSet->GetDynamicAttributeValue(BuildSkillTag, 0) > 80.0f);
    }

    bool bProvidedAny = false;

    for (int32 i = 0; i < PieceDef->RequiredMaterials.Num(); ++i)
    {
        if ((Item.ProvidedMaterialsMask & (1 << i)) != 0) continue;

        const FGameplayTag ReqTag = PieceDef->RequiredMaterials[i].MaterialArchetypeTag;
        int32 NeededQty = PieceDef->RequiredMaterials[i].RequiredQuantity;

        static const FGameplayTag LeftHandTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Left"), false);
        if (EquipComp->GetEquippedItemInSlot(LeftHandTag) == ReqTag)
        {
            EquipComp->ServerUnequipSlot(LeftHandTag, FTransform::Identity); 
            NeededQty -= 1;
        }

        if (NeededQty > 0 && bAllowProximityPull)
        {
            InvComp->ServerRemoveStoredActorByTag(ReqTag, NeededQty);
            NeededQty = 0;
        }

        if (NeededQty <= 0)
        {
            Item.ProvidedMaterialsMask |= (1 << i);
            bProvidedAny = true;
        }
    }

    if (bProvidedAny)
    {
        ReplicatedStructures.MarkItemDirty(Item);
    }
    return bProvidedAny;
}

void ADynamicBuildingChunkActor::ServerApplyConstructionLabor(const FGuid& InstanceID, AActor* WorkerActor, float LaborAmount)
{
    if (!HasAuthority() || !WorkerActor || LaborAmount <= 0.0f) return;

    int32 FoundIdx = ReplicatedStructures.Items.IndexOfByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == InstanceID; });
    if (FoundIdx == INDEX_NONE) return;

    FStructureInstanceItem& Item = ReplicatedStructures.Items[FoundIdx];
    if (!Item.bIsGhost) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(Item.PieceTag);
    if (!PieceDef) return;

    UDynamicEquipmentComponent* EquipComp = WorkerActor->FindComponentByClass<UDynamicEquipmentComponent>();
    if (PieceDef->RequiredToolTag.IsValid() && EquipComp)
    {
        static const FGameplayTag RightHandTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
        if (EquipComp->GetEquippedItemInSlot(RightHandTag) != PieceDef->RequiredToolTag) return;
    }

    const uint32 RequiredMask = (1 << PieceDef->RequiredMaterials.Num()) - 1;
    if ((Item.ProvidedMaterialsMask & RequiredMask) != RequiredMask) return;

    // FIX: Query UDynamicAttributeSet through Character or ASC
    float SkillMultiplier = 1.0f;
    const UDynamicAttributeSet* AttrSet = nullptr;
    if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(WorkerActor))
    {
        AttrSet = DynChar->GetDynamicAttributeSet();
    }
    else if (UAbilitySystemComponent* ASC = WorkerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        AttrSet = Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    }

    if (AttrSet)
    {
        static const FGameplayTag BuildSkillTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Skill.Building"), false);
        SkillMultiplier += (AttrSet->GetDynamicAttributeValue(BuildSkillTag, 0) * 0.02f);
    }

    Item.ConstructionProgress += (LaborAmount * SkillMultiplier);

    if (Item.ConstructionProgress >= PieceDef->BaseLaborCost)
    {
        Item.bIsGhost = false;
        Item.ConstructionProgress = 1.0f;
        RecalculateStructuralIntegrityAsync();
    }
    
    ReplicatedStructures.MarkItemDirty(Item);
    UpdateInstancedMeshes();
}

void ADynamicBuildingChunkActor::ServerDamageBuildingInstance(const FGuid& InstanceID, float DamageAmount, const FVector& HitLocation, const FVector& HitNormal, AActor* DamageInstigator)
{
    if (!HasAuthority() || DamageAmount <= 0.0f) return;

    int32 FoundIdx = ReplicatedStructures.Items.IndexOfByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == InstanceID; });
    if (FoundIdx == INDEX_NONE) return;

    FStructureInstanceItem& Item = ReplicatedStructures.Items[FoundIdx];
    Item.CurrentHP -= DamageAmount;

    if (Item.CurrentHP <= 0.0f)
    {
        ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
        
        if (GS && !Item.bIsGhost) // Don't drop loot for destroying blueprints
        {
            if (const FDynamicStructureMaterialDef* MatDef = GS->StructureMaterialRegistry.Find(Item.MaterialTag))
            {
                if (MatDef->DestroyedYieldArchetype.IsValid())
                {
                    FTransform DropTransform(FRotator::ZeroRotator, Item.WorldTransform.GetLocation());
                    if (ADynamicPhysicsActor* DroppedLoot = GS->AcquirePhysicsActor(MatDef->DestroyedYieldArchetype, DropTransform, this, Cast<APawn>(DamageInstigator)))
                    {
                        if (UPrimitiveComponent* Prim = DroppedLoot->GetActivePhysicsPrimitive())
                        {
                            Prim->SetSimulatePhysics(true);
                        }
                    }
                }
            }
        }

        ReplicatedStructures.Items.RemoveAt(FoundIdx);
        ReplicatedStructures.MarkArrayDirty();
        UpdateInstancedMeshes();
        RecalculateStructuralIntegrityAsync(); 
    }
    else
    {
        ReplicatedStructures.MarkItemDirty(Item);
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
    TMap<FGameplayTag, FDynamicStructurePieceDef> Pieces = GS->StructurePieceRegistry;
    TMap<FGameplayTag, FDynamicStructureMaterialDef> Materials = GS->StructureMaterialRegistry;

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
            const FDynamicStructurePieceDef* Def = Pieces.Find(Node.PieceTag);
            const float Reach = Def ? Def->OverlapConnectionRadius : 150.0f;
            const FVector LocA = Node.WorldTransform.GetLocation();

            Node.bIsGrounded = GroundedStates[i];

            if (Node.bIsGrounded)
            {
                const FDynamicStructureMaterialDef* Mat = Materials.Find(Node.MaterialTag);
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

                    const FDynamicStructurePieceDef* NDef = Pieces.Find(NeighborItem->PieceTag);
                    const FDynamicStructureMaterialDef* NMat = Materials.Find(NeighborItem->MaterialTag);
                    
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

                    // FIX: Retrieve UWorld safely from StrongThis inside the lambda scope
                    if (UWorld* LocalWorld = StrongThis->GetWorld())
                    {
                        if (UDynamicBuildingSubsystem* BuildSub = LocalWorld->GetSubsystem<UDynamicBuildingSubsystem>())
                        {
                            BuildSub->RequestAsynchronousRoomEvaluation();
                        }
                    }
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