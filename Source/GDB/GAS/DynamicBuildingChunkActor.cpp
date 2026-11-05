// DynamicBuildingChunkActor.cpp
#include "DynamicBuildingChunkActor.h"
#include "DynamicGameState.h"
#include "DynamicVoxelSubsystem.h"
#include "DynamicPhysicsActor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/AssetManager.h"
#include "Async/Async.h"
#include "Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Runtime/Engine/Public/Net/UnrealNetwork.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicCharacter.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicBuildingSubsystem.h"
#include "Engine/AssetManager.h"
#include "NavigationSystem.h"
#include "Engine/StreamableManager.h"

ADynamicBuildingChunkActor::ADynamicBuildingChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = false;
    NetPriority = 1.0f;
    SetNetUpdateFrequency(2.0f);
    
    NavModifierComponent = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifierComponent"));
    NavModifierComponent->SetIsReplicated(false);
    
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
    NewHISM->NumCustomDataFloats = 2;

    if (!PieceDef->MeshAsset.IsNull())
    {
        FSoftObjectPath AssetPath = PieceDef->MeshAsset.ToSoftObjectPath();
        UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateLambda([NewHISM, AssetPath]() {
            if (IsValid(NewHISM))
            {
                if (UStaticMesh* LoadedMesh = Cast<UStaticMesh>(AssetPath.ResolveObject()))
                {
                    NewHISM->SetStaticMesh(LoadedMesh);
                }
            }
        }));
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

    struct FBulkInstancePayload
    {
        TArray<FTransform> Transforms;
        TArray<FGuid> InstanceIDs;
        TArray<float> StressNormals;
        TArray<float> HologramMasks;
    };

    TMap<UHierarchicalInstancedStaticMeshComponent*, FBulkInstancePayload> BulkAddMap;

    // 1. Sort structure data into respective component buckets without triggering updates
    for (const FStructureInstanceItem& Item : ReplicatedStructures.Items)
    {
        if (Item.CurrentHP <= 0.0f) continue;

        if (UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Item.PieceTag, Item.MaterialTag))
        {
            FBulkInstancePayload& Payload = BulkAddMap.FindOrAdd(HISM);
            Payload.Transforms.Add(Item.WorldTransform);
            Payload.InstanceIDs.Add(Item.InstanceID);

            float MaxSupport = 100.0f;
            if (const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(Item.MaterialTag))
            {
                MaxSupport = MatDef->SupportStrength;
            }
            
            Payload.StressNormals.Add(FMath::Clamp(Item.SupportValue / MaxSupport, 0.0f, 1.0f));
            Payload.HologramMasks.Add(Item.bIsGhost ? 0.0f : 1.0f);
        }
    }

    // 2. Execute bulk updates precisely once per mesh component
    for (auto& Pair : BulkAddMap)
    {
        UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Key;
        const FBulkInstancePayload& Payload = Pair.Value;

        // bReturnLocalBounds = false prevents calculating bounding boxes until the batch completes
        TArray<int32> AssignedIndices = HISM->AddInstances(Payload.Transforms, false, true);
        TMap<int32, FGuid>& LocalHitMap = InstanceHitMap.FindOrAdd(HISM);

        for (int32 i = 0; i < AssignedIndices.Num() && i < Payload.InstanceIDs.Num(); ++i)
        {
            const int32 InstIdx = AssignedIndices[i];
            LocalHitMap.Add(InstIdx, Payload.InstanceIDs[i]);
            HISM->SetCustomDataValue(InstIdx, 0, Payload.StressNormals[i], false); // bMarkRenderStateDirty = false
            HISM->SetCustomDataValue(InstIdx, 1, Payload.HologramMasks[i], false);
        }
        
        // Single final bounds update for rendering & navmesh
        HISM->BuildTreeIfOutdated(true, false);
        HISM->MarkRenderStateDirty();
    }
    
    // Trigger runtime NavMesh rebuild for this specific chunk bounds
    if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        NavSys->UpdateComponentInNavOctree(*RootComponent);
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
        // Guard against shifting beyond 64-bit integer width
        if (i >= 64) break;

        if ((Item.ProvidedMaterialsMask & (1ULL << i)) != 0) continue;

        const FGameplayTag ReqTag = PieceDef->RequiredMaterials[i].MaterialArchetypeTag;
        int32 NeededQty = PieceDef->RequiredMaterials[i].RequiredQuantity;

        // 1. Consume from active left hand slot and cleanly recycle the actor into the pool
        static const FGameplayTag LeftHandTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Left"), false);
        if (EquipComp->GetEquippedItemInSlot(LeftHandTag) == ReqTag)
        {
            if (ADynamicPhysicsActor* UnequippedActor = EquipComp->ServerUnequipSlot(LeftHandTag, WorkerActor->GetActorTransform()))
            {
                UnequippedActor->DeactivateAndReturnToPool();
                NeededQty -= 1;
            }
        }

        // 2. Consume remaining quantity from container inventory, strictly verifying the return value
        if (NeededQty > 0)
        {
            if (InvComp->GetTotalCountOfArchetype(ReqTag) >= NeededQty)
            {
                if (InvComp->ServerRemoveStoredActorByTag(ReqTag, NeededQty))
                {
                    NeededQty = 0;
                }
            }
            else if (bAllowProximityPull)
            {
                // Master builders can search nearby room containers
                if (UDynamicBuildingSubsystem* BuildSub = GetWorld()->GetSubsystem<UDynamicBuildingSubsystem>())
                {
                    static const FGameplayTag StorageRoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Type.Storage"), false);
                    FGuid WorkerGuid;
                    if (UDynamicSocialComponent* Social = WorkerActor->FindComponentByClass<UDynamicSocialComponent>())
                    {
                        WorkerGuid = Social->CharacterPersistentGuid;
                    }

                    if (UDynamicContainerComponent* NearbyContainer = BuildSub->FindOptimalAIContainerInRoomType(WorkerActor->GetActorLocation(), StorageRoomTag, ReqTag, WorkerGuid))
                    {
                        if (NearbyContainer->GetTotalCountOfArchetype(ReqTag) >= NeededQty)
                        {
                            if (NearbyContainer->ServerRemoveStoredActorByTag(ReqTag, NeededQty))
                            {
                                NeededQty = 0;
                            }
                        }
                    }
                }
            }
        }

        // If the entire required quantity for this material step was satisfied, mark the bitmask
        if (NeededQty <= 0)
        {
            Item.ProvidedMaterialsMask |= (1ULL << i);
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

void ADynamicBuildingChunkActor::ServerDamageBuildingInstance(
    const FGuid& InstanceID, 
    float DamageAmount, 
    const FVector& HitLocation, 
    const FVector& HitNormal, 
    AActor* DamageInstigator, 
    FGameplayTag DamageCauseTag)
{
    if (!HasAuthority() || DamageAmount <= 0.0f) return;

    int32 FoundIdx = ReplicatedStructures.Items.IndexOfByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == InstanceID; });
    if (FoundIdx == INDEX_NONE) return;

    FStructureInstanceItem& Item = ReplicatedStructures.Items[FoundIdx];
    Item.CurrentHP -= DamageAmount;

    if (Item.CurrentHP <= 0.0f)
    {
        ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
        
        if (GS && !Item.bIsGhost)
        {
            if (const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(Item.MaterialTag))
            {
                // Determine the correct drop based on mechanical stress/thermal destruction cause
                FGameplayTag YieldTag = MatDef->DefaultYieldArchetype;

                static const FGameplayTag FireTag     = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Fire"), false);
                static const FGameplayTag RotTag      = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Rot"), false);
                static const FGameplayTag CrushingTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Physical.Crushing"), false);
                static const FGameplayTag SlashingTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Physical.Slashing"), false);
                static const FGameplayTag PiercingTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Physical.Piercing"), false);
                static const FGameplayTag ShatterTag  = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Shattered"), false);

                if (DamageCauseTag.MatchesTag(FireTag) && MatDef->BurnedYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->BurnedYieldArchetype;
                }
                else if (DamageCauseTag.MatchesTag(RotTag) && MatDef->RottedYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->RottedYieldArchetype;
                }
                else if (DamageCauseTag.MatchesTag(CrushingTag) && MatDef->CrushedYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->CrushedYieldArchetype;
                }
                else if (DamageCauseTag.MatchesTag(SlashingTag) && MatDef->CleavedYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->CleavedYieldArchetype;
                }
                else if (DamageCauseTag.MatchesTag(PiercingTag) && MatDef->PiercedYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->PiercedYieldArchetype;
                }
                else if (DamageCauseTag.MatchesTag(ShatterTag) && MatDef->ShatteredYieldArchetype.IsValid())
                {
                    YieldTag = MatDef->ShatteredYieldArchetype;
                }

                if (YieldTag.IsValid())
                {
                    FTransform DropTransform(FRotator::ZeroRotator, Item.WorldTransform.GetLocation());
                    if (ADynamicPhysicsActor* DroppedLoot = GS->AcquirePhysicsActor(YieldTag, DropTransform, this, Cast<APawn>(DamageInstigator)))
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

    TMap<FGameplayTag, FDynamicStructurePieceDef> Pieces = GS->StructurePieceRegistry;
    TMap<FGameplayTag, FDynamicMaterialDef> Materials = GS->MaterialMasterRegistry;

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

        // OPTIMIZATION: Zero-Allocation 1D Spatial Hash Grid (DOD)
        // Eliminates thousands of TArray heap allocations inside the ThreadPool task
        const float GridSize = 250.0f; 
        const int32 HashBuckets = 8191; // Prime number for hash modulo
        
        TArray<int32> Head; 
        Head.Init(INDEX_NONE, HashBuckets);
        
        TArray<int32> Next; 
        Next.Init(INDEX_NONE, StateCapture.Num());
        
        for (int32 i = 0; i < StateCapture.Num(); ++i)
        {
            FStructureInstanceItem& Node = StateCapture[i];
            Node.bIsGrounded = GroundedStates[i];

            if (Node.bIsGrounded)
            {
                const FDynamicMaterialDef* Mat = Materials.Find(Node.MaterialTag);
                Node.SupportValue = Mat ? Mat->SupportStrength : 100.0f;
                ResolvedSupport.Add(Node.InstanceID, Node.SupportValue);
                EvaluationQueue.Enqueue(Node.InstanceID);
            }

            const FVector Loc = Node.WorldTransform.GetLocation();
            const FIntVector Cell(FMath::FloorToInt(Loc.X / GridSize), FMath::FloorToInt(Loc.Y / GridSize), FMath::FloorToInt(Loc.Z / GridSize));
            
            // Hash coordinate to 1D bucket
            const uint32 HashIndex = GetTypeHash(Cell) % HashBuckets;
            Next[i] = Head[HashIndex];
            Head[HashIndex] = i;
        }

        // 1. Build Adjacency Graph via Spatial Hash (O(N) resolution)
        for (int32 i = 0; i < StateCapture.Num(); ++i)
        {
            const FStructureInstanceItem& NodeA = StateCapture[i];
            const FDynamicStructurePieceDef* Def = Pieces.Find(NodeA.PieceTag);
            const float Reach = Def ? Def->OverlapConnectionRadius : 150.0f;
            const float ReachSq = FMath::Square(Reach);
            const FVector LocA = NodeA.WorldTransform.GetLocation();

            const FIntVector CenterCell(FMath::FloorToInt(LocA.X / GridSize), FMath::FloorToInt(LocA.Y / GridSize), FMath::FloorToInt(LocA.Z / GridSize));

            // Check adjacent 3x3x3 grid cells using the flattened linked list
            for (int32 Z = -1; Z <= 1; ++Z) {
                for (int32 Y = -1; Y <= 1; ++Y) {
                    for (int32 X = -1; X <= 1; ++X) {
                        const FIntVector NeighborCell = CenterCell + FIntVector(X, Y, Z);
                        const uint32 NeighborHash = GetTypeHash(NeighborCell) % HashBuckets;
                        
                        int32 j = Head[NeighborHash];
                        while (j != INDEX_NONE)
                        {
                            if (i < j) // Prevent duplicate bidirectional checks and self-checks
                            {
                                if (FVector::DistSquared(LocA, StateCapture[j].WorldTransform.GetLocation()) <= ReachSq)
                                {
                                    LocalGraph.FindOrAdd(NodeA.InstanceID).Add(StateCapture[j].InstanceID);
                                    LocalGraph.FindOrAdd(StateCapture[j].InstanceID).Add(NodeA.InstanceID);
                                }
                            }
                            j = Next[j];
                        }
                    }
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
                    const FDynamicMaterialDef* NMat = Materials.Find(NeighborItem->MaterialTag);
                    
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
                    StrongThis->ReplicatedStructures.Items.RemoveAll([](const FStructureInstanceItem& I) { return I.CurrentHP <= 0.0f; });
                    StrongThis->ReplicatedStructures.MarkArrayDirty();
                    StrongThis->UpdateInstancedMeshes();

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