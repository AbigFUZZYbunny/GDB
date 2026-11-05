// DynamicBuildingSubsystem.cpp
#include "DynamicBuildingSubsystem.h"
#include "Sound/ReverbEffect.h"
#include "DynamicGameState.h"
#include "GameFramework/PlayerController.h"
#include "DynamicLogicEvaluator.h"
#include "AbilitySystemComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicContainerComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicCharacter.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

void UDynamicBuildingSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); }
void UDynamicBuildingSubsystem::Deinitialize() { ActiveBuildingChunks.Empty(); Super::Deinitialize(); }
void UDynamicBuildingSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
    {
        StreamingTimer += DeltaTime;
        if (StreamingTimer >= 2.0f)
        {
            StreamingTimer = 0.0f;
            ProcessBuildingStreaming();
        }

        TransitionTimer += DeltaTime;
        if (TransitionTimer >= 0.5f)
        {
            TransitionTimer = 0.0f;
            ProcessCharacterRoomTransitions();
        }

        // --- NEW: Thermodynamic Evaluation ---
        ThermoDecayTimer += DeltaTime;
        if (ThermoDecayTimer >= 5.0f) // Evaluate every 5 seconds to conserve CPU
        {
            ProcessThermodynamicDecay(ThermoDecayTimer);
            ThermoDecayTimer = 0.0f;
        }
    }
}

void UDynamicBuildingSubsystem::ProcessCharacterRoomTransitions()
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS || ActiveProperties.Num() == 0) return;

    const FGameplayTag EnterActionTag = GS->LawAndOrderConfig.EnterPropertyActionTag.IsValid()
        ? GS->LawAndOrderConfig.EnterPropertyActionTag
        : FGameplayTag::RequestGameplayTag(TEXT("Action.Movement.EnterProperty"), false);

    const FGameplayTag ExitActionTag = GS->LawAndOrderConfig.ExitPropertyActionTag;

    // Iterate over registered active characters and resolve owners in O(1) via ActiveCharacterByGuidMap
    for (const auto& CharPair : GS->GetActiveCharacterByGuidMap())
    {
        ADynamicCharacter* Char = CharPair.Value.Get();
        if (!IsValid(Char) || Char->IsPendingKillPending()) continue;

        UDynamicSocialComponent* Social = Char->FindComponentByClass<UDynamicSocialComponent>();
        if (!Social) continue;

        FGuid NewPropertyID;
        FGuid NewOwnerGuid;

        const FVector CharLoc = Char->GetActorLocation();
        for (const FDynamicPropertyInstance& Prop : ActiveProperties)
        {
            if (Prop.PropertyBounds.IsInsideOrOn(CharLoc))
            {
                NewPropertyID = Prop.PropertyID;
                NewOwnerGuid = Prop.OwnerGuid;
                break;
            }
        }

        if (Social->CurrentPropertyID != NewPropertyID)
        {
            const FGuid OldPropertyID = Social->CurrentPropertyID;
            Social->CurrentPropertyID = NewPropertyID;

            if (NewPropertyID.IsValid() && NewOwnerGuid.IsValid() && NewOwnerGuid != Social->CharacterPersistentGuid)
            {
                // O(1) Owner lookup via GameState GUID registry
                AActor* OwnerActor = GS->FindCharacterByGuid(NewOwnerGuid);

                if (EnterActionTag.IsValid())
                {
                    Social->ServerEvaluateAndBroadcastAction(
                        EnterActionTag,
                        Char,       // Perpetrator (Entrant)
                        OwnerActor, // Victim (Property Owner)
                        FGuid(),
                        TArray<FGuid>()
                    );
                }
            }
            else if (OldPropertyID.IsValid() && ExitActionTag.IsValid())
            {
                Social->ServerEvaluateAndBroadcastAction(
                    ExitActionTag,
                    Char,
                    nullptr,
                    FGuid(),
                    TArray<FGuid>()
                );
            }
        }
    }
}

ADynamicBuildingChunkActor* UDynamicBuildingSubsystem::GetOrCreateChunkAtLocation(const FVector& WorldLocation)
{
    FIntPoint Coord(
        FMath::FloorToInt(WorldLocation.X / ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS),
        FMath::FloorToInt(WorldLocation.Y / ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS)
    );

    if (ADynamicBuildingChunkActor** Found = ActiveBuildingChunks.Find(Coord)) return *Found;

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters Params;
        Params.Owner = World->GetGameState();
        if (ADynamicBuildingChunkActor* NewChunk = World->SpawnActor<ADynamicBuildingChunkActor>(ADynamicBuildingChunkActor::StaticClass(), FTransform::Identity, Params))
        {
            NewChunk->InitializeChunk(Coord, ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS);
            ActiveBuildingChunks.Add(Coord, NewChunk);
            return NewChunk;
        }
    }
    return nullptr;
}

bool UDynamicBuildingSubsystem::ServerPlaceStructureGhost(const FTransform& IntendedTransform, FGameplayTag PieceTag, FGameplayTag MaterialTag, AActor* Instigator)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(PieceTag);
    const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(MaterialTag);

    if (!PieceDef || !MatDef) return false;

    // Condition Tree Check for access
    if (!PieceDef->PrerequisiteConditionTree.IsNone() && Instigator)
    {
        if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(Instigator, PieceDef->PrerequisiteConditionTree))
        {
            UAbilitySystemComponent* ASC = Instigator->FindComponentByClass<UAbilitySystemComponent>();
            FGameplayTagContainer STags, TTags;
            if (ASC) ASC->GetOwnedGameplayTags(STags);
            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, STags, TTags)) return false;
        }
    }

    FGuid BuilderGuid;
    if (Instigator)
    {
        if (UDynamicSocialComponent* Social = Instigator->FindComponentByClass<UDynamicSocialComponent>())
        {
            BuilderGuid = Social->CharacterPersistentGuid;
        }
    }

    ADynamicBuildingChunkActor* Chunk = GetOrCreateChunkAtLocation(IntendedTransform.GetLocation());
    if (!Chunk) return false;

    FStructureInstanceItem NewItem;
    NewItem.InstanceID = FGuid::NewGuid();
    NewItem.PieceTag = PieceTag;
    NewItem.MaterialTag = MaterialTag;
    NewItem.WorldTransform = IntendedTransform;
    NewItem.CurrentHP = PieceDef->BaseMaxHP * MatDef->DurabilityMultiplier;
    NewItem.SupportValue = 0.0f; 
    NewItem.OwnerGuid = BuilderGuid;
    
    // Ghost Properties
    NewItem.bIsGhost = true;
    NewItem.ConstructionProgress = 0.0f;
    NewItem.ProvidedMaterialsMask = 0;

    FStructureInstanceItem& Added = Chunk->ReplicatedStructures.Items.Add_GetRef(NewItem);
    Chunk->ReplicatedStructures.MarkItemDirty(Added);
    
    Chunk->UpdateInstancedMeshes();
    return true;
}

bool UDynamicBuildingSubsystem::ServerPlacePrefabGhosts(const FTransform& OriginTransform, FGameplayTag PrefabTag, FGameplayTag MaterialTag, AActor* Instigator)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    const FDynamicStructurePrefabDef* PrefabDef = GS->StructurePrefabRegistry.Find(PrefabTag);
    if (!PrefabDef) return false;

    if (!PrefabDef->PrerequisiteConditionTree.IsNone() && Instigator)
    {
        if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(Instigator, PrefabDef->PrerequisiteConditionTree))
        {
            UAbilitySystemComponent* ASC = Instigator->FindComponentByClass<UAbilitySystemComponent>();
            FGameplayTagContainer STags, TTags;
            if (ASC) ASC->GetOwnedGameplayTags(STags);
            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, STags, TTags)) return false;
        }
    }

    bool bSuccess = true;
    for (const FPrefabComponentDef& Comp : PrefabDef->Components)
    {
        FTransform WorldTM = Comp.RelativeTransform * OriginTransform;
        bSuccess &= ServerPlaceStructureGhost(WorldTM, Comp.PieceTag, MaterialTag, Instigator);
    }
    return bSuccess;
}

bool UDynamicBuildingSubsystem::CalculateFreeformPlacementTransform(const FHitResult& ViewHit, FGameplayTag PieceToPlace, const FRotator& IntendedPlayerRotation, FTransform& OutTransform) const
{
    if (!ViewHit.bBlockingHit) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicStructurePieceDef* ToPlaceDef = GS ? GS->StructurePieceRegistry.Find(PieceToPlace) : nullptr;
    if (!ToPlaceDef) return false;

    UHierarchicalInstancedStaticMeshComponent* HitHISM = Cast<UHierarchicalInstancedStaticMeshComponent>(ViewHit.GetComponent());
    ADynamicBuildingChunkActor* HitChunk = Cast<ADynamicBuildingChunkActor>(ViewHit.GetActor());

    // Volumetric Sub-Chunk Snapping (Stone Enshrouded Scale)
    if (ToPlaceDef->bIsVolumetricBlock)
    {
        const float SnapSize = ToPlaceDef->VolumetricGridSnapSize;
        const FVector Impact = ViewHit.ImpactPoint + (ViewHit.ImpactNormal * (SnapSize * 0.5f));
        
        float SnappedX = FMath::RoundToFloat(Impact.X / SnapSize) * SnapSize;
        float SnappedY = FMath::RoundToFloat(Impact.Y / SnapSize) * SnapSize;
        float SnappedZ = FMath::RoundToFloat(Impact.Z / SnapSize) * SnapSize;

        OutTransform = FTransform(FRotator::ZeroRotator, FVector(SnappedX, SnappedY, SnappedZ));
        return true;
    }
    
    // Freeform Terrain Snapping
    if (!HitHISM || !HitChunk)
    {
        FRotator TerrainRot = FRotationMatrix::MakeFromZX(ViewHit.ImpactNormal, IntendedPlayerRotation.Vector()).Rotator();
        OutTransform = FTransform(TerrainRot, ViewHit.ImpactPoint);
        return true;
    }

    // Socket Valheim-style Snapping
    if (const TMap<int32, FGuid>* InstMap = HitChunk->InstanceHitMap.Find(HitHISM))
    {
        if (const FGuid* TargetGuid = InstMap->Find(ViewHit.Item))
        {
            const FStructureInstanceItem* TargetItem = HitChunk->ReplicatedStructures.Items.FindByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == *TargetGuid; });
            if (TargetItem)
            {
                const FDynamicStructurePieceDef* TargetDef = GS->StructurePieceRegistry.Find(TargetItem->PieceTag);
                if (TargetDef && TargetDef->ConnectionSockets.Num() > 0)
                {
                    float BestDistSq = MAX_flt;
                    FTransform BestSocketWorldTM = FTransform::Identity;
                    
                    for (const FBuildingSocketDef& Socket : TargetDef->ConnectionSockets)
                    {
                        if (Socket.AllowedConnections.Num() > 0 && !Socket.AllowedConnections.HasTag(PieceToPlace)) continue;

                        FTransform SocketWorld = Socket.RelativeTransform * TargetItem->WorldTransform;
                        float DistSq = FVector::DistSquared(SocketWorld.GetLocation(), ViewHit.ImpactPoint);
                        
                        if (DistSq < BestDistSq)
                        {
                            BestDistSq = DistSq;
                            BestSocketWorldTM = SocketWorld;
                        }
                    }

                    FRotator SnappedRot = BestSocketWorldTM.Rotator();
                    SnappedRot.Yaw = IntendedPlayerRotation.Yaw; // Retain user facing choice
                    OutTransform = FTransform(SnappedRot, BestSocketWorldTM.GetLocation());
                    return true;
                }
            }
        }
    }

    OutTransform = FTransform(IntendedPlayerRotation, ViewHit.ImpactPoint);
    return true;
}

float UDynamicBuildingSubsystem::EvaluateShelterInsulationAtLocation(const FVector& WorldLocation) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return 0.0f;

    // 1. O(1) Short-Circuit: Provide instant full insulation if standing within a designated indoor room bounds
    for (const FDynamicRoomInstance& Room : ActiveRooms)
    {
        if (Room.WorldBounds.IsInsideOrOn(WorldLocation))
        {
            return Room.StructuralInsulationValue;
        }
    }

    // 2. Perform lowered-resolution check only if caught outdoors under cover
    float TotalInsulation = 0.0f;
    int32 ValidHits = 0;
    const int32 NumRays = 8; // Dropped from 16 for outdoor canopy checks
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShelterEvaluation), false);
    
    for (int32 i = 0; i < NumRays; ++i)
    {
        float Z = FMath::RandRange(0.2f, 1.0f);
        float Phi = FMath::RandRange(0.0f, PI * 2.0f);
        float R = FMath::Sqrt(1.0f - Z*Z);
        FVector Dir(R * FMath::Cos(Phi), R * FMath::Sin(Phi), Z);
        
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, WorldLocation, WorldLocation + (Dir * 1500.0f), ECC_Visibility, Params))
        {
            if (UHierarchicalInstancedStaticMeshComponent* HitHISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Hit.GetComponent()))
            {
                if (ADynamicBuildingChunkActor* Chunk = Cast<ADynamicBuildingChunkActor>(Hit.GetActor()))
                {
                    if (const TMap<int32, FGuid>* InstMap = Chunk->InstanceHitMap.Find(HitHISM))
                    {
                        if (const FGuid* TargetGuid = InstMap->Find(Hit.Item))
                        {
                            const FStructureInstanceItem* Item = Chunk->ReplicatedStructures.Items.FindByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == *TargetGuid; });
                            if (Item && !Item->bIsGhost)
                            {
                                if (const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(Item->MaterialTag))
                                {
                                    TotalInsulation += MatDef->InsulationValue;
                                    ValidHits++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    float CoverageDensity = static_cast<float>(ValidHits) / static_cast<float>(NumRays);
    float AverageInsulation = ValidHits > 0 ? (TotalInsulation / ValidHits) : 0.0f;
    return AverageInsulation * CoverageDensity;
}

void UDynamicBuildingSubsystem::ProcessBuildingStreaming()
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

    const float ChunkSize = ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS;
    const float LoadRadiusSq = FMath::Square(LoadRadius);
    const float UnloadRadiusSq = FMath::Square(UnloadRadius);

    TSet<FIntPoint> RequiredChunks;

    // 1. Calculate which spatial chunks are in range of any active player
    for (const FVector& PLoc : PlayerLocations)
    {
        const int32 RadiusInChunks = FMath::CeilToInt(LoadRadius / ChunkSize);
        const FIntPoint CenterCoord(
            FMath::FloorToInt(PLoc.X / ChunkSize),
            FMath::FloorToInt(PLoc.Y / ChunkSize)
        );

        for (int32 Y = -RadiusInChunks; Y <= RadiusInChunks; ++Y)
        {
            for (int32 X = -RadiusInChunks; X <= RadiusInChunks; ++X)
            {
                const FIntPoint EvalCoord = CenterCoord + FIntPoint(X, Y);
                const FVector ChunkCenter(
                    (EvalCoord.X * ChunkSize) + (ChunkSize * 0.5f),
                    (EvalCoord.Y * ChunkSize) + (ChunkSize * 0.5f),
                    PLoc.Z
                );

                if (FVector::DistSquared(PLoc, ChunkCenter) <= LoadRadiusSq)
                {
                    RequiredChunks.Add(EvalCoord);
                }
            }
        }
    }

    // 2. Safely unload chunks beyond the Hysteresis bounds
    TArray<FIntPoint> ChunksToUnload;
    for (const auto& Pair : ActiveBuildingChunks)
    {
        bool bKeepLoaded = false;
        FVector ChunkCenter(
            (Pair.Key.X * ChunkSize) + (ChunkSize * 0.5f),
            (Pair.Key.Y * ChunkSize) + (ChunkSize * 0.5f),
            0.0f
        );

        for (const FVector& PLoc : PlayerLocations)
        {
            // Flatten Z to prevent tall towers from despawning if a player flies over them
            ChunkCenter.Z = PLoc.Z; 
            if (FVector::DistSquared(PLoc, ChunkCenter) <= UnloadRadiusSq)
            {
                bKeepLoaded = true;
                break;
            }
        }

        if (!bKeepLoaded)
        {
            ChunksToUnload.Add(Pair.Key);
        }
    }

    for (const FIntPoint& Coord : ChunksToUnload)
    {
        if (ADynamicBuildingChunkActor* Chunk = ActiveBuildingChunks[Coord])
        {
            if (Chunk->ReplicatedStructures.Items.Num() > 0)
            {
                FBuildingChunkSnapshot& Snap = VirtualizedBuildingChunks.FindOrAdd(Coord);
                Snap.ChunkCoord = Coord;
                Snap.SavedStructures = Chunk->ReplicatedStructures.Items;
            }
            else
            {
                VirtualizedBuildingChunks.Remove(Coord);
            }
            Chunk->Destroy();
        }
        ActiveBuildingChunks.Remove(Coord);
    }

    // 3. Spawn and Hydrate Required Chunks
    for (const FIntPoint& Coord : RequiredChunks)
    {
        if (!ActiveBuildingChunks.Contains(Coord))
        {
            // Spawning empty chunks allows players to build in new coordinates
            const FVector SpawnLoc(Coord.X * ChunkSize, Coord.Y * ChunkSize, 0.0f);
            FActorSpawnParameters Params;
            Params.Owner = World->GetGameState();
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            if (ADynamicBuildingChunkActor* NewChunk = World->SpawnActor<ADynamicBuildingChunkActor>(ADynamicBuildingChunkActor::StaticClass(), FTransform(SpawnLoc), Params))
            {
                NewChunk->InitializeChunk(Coord, ChunkSize);
                
                if (const FBuildingChunkSnapshot* Snap = VirtualizedBuildingChunks.Find(Coord))
                {
                    NewChunk->ReplicatedStructures.Items = Snap->SavedStructures;
                    NewChunk->ReplicatedStructures.MarkArrayDirty();
                }
                
                ActiveBuildingChunks.Add(Coord, NewChunk);
                NewChunk->UpdateInstancedMeshes();
            }
        }
    }
}

void UDynamicBuildingSubsystem::SerializeWorldBuildingState(FWorldPersistenceSnapshot& OutSnapshot)
{
    // 1. Sync live spawned actors into the mathematical virtual cache before serializing to disk
    for (const auto& Pair : ActiveBuildingChunks)
    {
        if (ADynamicBuildingChunkActor* Chunk = Pair.Value)
        {
            if (Chunk->ReplicatedStructures.Items.Num() > 0)
            {
                FBuildingChunkSnapshot& Snap = VirtualizedBuildingChunks.FindOrAdd(Pair.Key);
                Snap.ChunkCoord = Pair.Key;
                Snap.SavedStructures = Chunk->ReplicatedStructures.Items;
            }
            else
            {
                VirtualizedBuildingChunks.Remove(Pair.Key);
            }
        }
    }

    // 2. Export the complete virtualized data to the snapshot
    OutSnapshot.ModifiedBuildingChunks.Empty(VirtualizedBuildingChunks.Num());
    for (const auto& Pair : VirtualizedBuildingChunks)
    {
        OutSnapshot.ModifiedBuildingChunks.Add(Pair.Value);
    }
    
    OutSnapshot.ActiveProperties = ActiveProperties;
}

void UDynamicBuildingSubsystem::RestoreWorldBuildingState(const FWorldPersistenceSnapshot& InSnapshot)
{
    VirtualizedBuildingChunks.Empty();
    ActiveProperties = InSnapshot.ActiveProperties;

    // Load purely into mathematical virtual space
    for (const FBuildingChunkSnapshot& Snap : InSnapshot.ModifiedBuildingChunks)
    {
        VirtualizedBuildingChunks.Add(Snap.ChunkCoord, Snap);
    }

    // Execute an immediate streaming tick so any players already spawned will instantly see surrounding buildings
    ProcessBuildingStreaming();
    RequestAsynchronousRoomEvaluation();
}

void UDynamicBuildingSubsystem::RequestAsynchronousRoomEvaluation()
{
    if (bRoomEvaluationPending) return;
    bRoomEvaluationPending = true;

    // Debounce the call to prevent spamming the thread pool during rapid structural cascades
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(this, &UDynamicBuildingSubsystem::ExecuteRoomFloodFillAsync);
    }
}

void UDynamicBuildingSubsystem::ExecuteRoomFloodFillAsync()
{
    bRoomEvaluationPending = false;
    UWorld* World = GetWorld();
    if (!World) return;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return;

    // 1. Snapshot all grounded structures for the background thread
    TArray<FStructureInstanceItem> GlobalStructures;
    for (auto& Pair : ActiveBuildingChunks)
    {
        if (Pair.Value)
        {
            GlobalStructures.Append(Pair.Value->ReplicatedStructures.Items);
        }
    }

    TWeakObjectPtr<UDynamicBuildingSubsystem> WeakThis(this);
    
    // 2. Dispatch to ThreadPool
    Async(EAsyncExecution::ThreadPool, [GlobalStructures, WeakThis]() {
        
        TArray<FDynamicRoomInstance> CalculatedRooms;
        TArray<FDynamicPropertyInstance> CalculatedProperties;

        // Abstracted: 3D Flood-Fill Algorithm generates bounds and groups contiguous rooms.
        FDynamicRoomInstance RoomA;
        RoomA.RoomID = FGuid::NewGuid();
        RoomA.WorldBounds = FBox(FVector(-500, -500, 0), FVector(500, 500, 400));
        RoomA.StructuralInsulationValue = 12.5f; 
        
        // Deduce Room Owner by finding the most common OwnerGuid among the walls forming this room
        TMap<FGuid, int32> OwnerVotes;
        for (const FStructureInstanceItem& Struct : GlobalStructures)
        {
            if (Struct.OwnerGuid.IsValid() && RoomA.WorldBounds.IsInsideOrOn(Struct.WorldTransform.GetLocation()))
            {
                OwnerVotes.FindOrAdd(Struct.OwnerGuid)++;
            }
        }
        
        int32 MaxVotes = 0;
        for (const auto& Vote : OwnerVotes)
        {
            if (Vote.Value > MaxVotes)
            {
                MaxVotes = Vote.Value;
                RoomA.OwnerGuid = Vote.Key;
            }
        }

        FDynamicPropertyInstance HouseA;
        HouseA.PropertyID = FGuid::NewGuid();
        HouseA.OwnerGuid = RoomA.OwnerGuid;
        HouseA.EncompassedRoomIDs.Add(RoomA.RoomID);
        HouseA.PropertyBounds = RoomA.WorldBounds;

        RoomA.PropertyID = HouseA.PropertyID;
        
        CalculatedRooms.Add(RoomA);
        CalculatedProperties.Add(HouseA);

        // 3. Return to GameThread to classify via Actors and modify environment
        Async(EAsyncExecution::TaskGraphMainThread, [WeakThis, CalculatedRooms = MoveTemp(CalculatedRooms), CalculatedProperties = MoveTemp(CalculatedProperties)]() mutable {
            if (UDynamicBuildingSubsystem* StrongThis = WeakThis.Get())
            {
                StrongThis->ActiveRooms = MoveTemp(CalculatedRooms);
                StrongThis->ActiveProperties = MoveTemp(CalculatedProperties);
                
                UWorld* LocalWorld = StrongThis->GetWorld();
                ADynamicGameState* LocalGS = LocalWorld ? LocalWorld->GetGameState<ADynamicGameState>() : nullptr;
                if (!LocalGS) return;

                // Classify contents and apply environment modifiers to the central grid
                for (FDynamicRoomInstance& Room : StrongThis->ActiveRooms)
                {
                    const float Volume = Room.WorldBounds.GetVolume();
                    Room.ClassifiedRoomTypeTag = StrongThis->ClassifyRoomByContents(Room.WorldBounds, Volume);

                    if (Room.ClassifiedRoomTypeTag.IsValid())
                    {
                        if (const FDynamicRoomTypeDef* RoomDef = LocalGS->RoomTypeRegistry.Find(Room.ClassifiedRoomTypeTag))
                        {
                            // RESOLVED C2248: Access grid settings via public getters
                            const int32 GW = LocalGS->GetGridWidth();
                            const int32 GH = LocalGS->GetGridHeight();
                            const float CellSize = LocalGS->GetCellSizeWorldUnits();

                            const float HalfWorldW = (GW * CellSize) * 0.5f;
                            const float HalfWorldH = (GH * CellSize) * 0.5f;

                            int32 MinX = FMath::Max(0, FMath::FloorToInt((Room.WorldBounds.Min.X + HalfWorldW) / CellSize));
                            int32 MinY = FMath::Max(0, FMath::FloorToInt((Room.WorldBounds.Min.Y + HalfWorldH) / CellSize));
                            int32 MaxX = FMath::Min(GW - 1, FMath::FloorToInt((Room.WorldBounds.Max.X + HalfWorldW) / CellSize));
                            int32 MaxY = FMath::Min(GH - 1, FMath::FloorToInt((Room.WorldBounds.Max.Y + HalfWorldH) / CellSize));

                            for (int32 Y = MinY; Y <= MaxY; ++Y)
                            {
                                for (int32 X = MinX; X <= MaxX; ++X)
                                {
                                    int32 CellIndex = (Y * GW) + X;
                                    Room.EncompassedEnvironmentCells.Add(CellIndex);
                                    
                                    // Map definitions safely onto the cell's physical attributes
                                    if (LocalGS->GetMutableEnvironmentReadBuffer().IsValidIndex(CellIndex))
                                    {
                                        FEnvironmentGridCellItem& TargetCell = LocalGS->GetMutableEnvironmentReadBuffer()[CellIndex];
                                        for (const FTagFloatPair& Modifier : RoomDef->EnvironmentModifiers.ReplicatedEntries)
                                        {
                                            float CurrentVal = TargetCell.DerivedPhysicalMetrics.LocalLookupMap.FindRef(Modifier.KeyTag);
                                            TargetCell.DerivedPhysicalMetrics.LocalLookupMap.Add(Modifier.KeyTag, CurrentVal + Modifier.Value);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });
    });
}

FGameplayTag UDynamicBuildingSubsystem::ClassifyRoomByContents(const FBox& RoomBounds, float RoomVolume) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return FGameplayTag::EmptyTag;

    // 1. Spatial box overlap replaces global TActorIterator<ADynamicPhysicsActor>
    TMap<FGameplayTag, int32> ContentCounts;
    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

    if (World->OverlapMultiByObjectType(Overlaps, RoomBounds.GetCenter(), FQuat::Identity, ObjParams, FCollisionShape::MakeBox(RoomBounds.GetExtent())))
    {
        TSet<const ADynamicPhysicsActor*> CountedActors;
        for (const FOverlapResult& Overlap : Overlaps)
        {
            if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
            {
                if (!CountedActors.Contains(PhysActor) && RoomBounds.IsInsideOrOn(PhysActor->GetActorLocation()))
                {
                    CountedActors.Add(PhysActor);
                    const FGameplayTag Archetype = PhysActor->GetActorArchetypeTag();
                    if (Archetype.IsValid())
                    {
                        ContentCounts.FindOrAdd(Archetype)++;
                    }
                }
            }
        }
    }

    // 2. Evaluate against Master Registry
    FGameplayTag BestRoomTag = FGameplayTag::EmptyTag;
    float HighestPriority = -1.0f;

    for (const auto& Pair : GS->RoomTypeRegistry)
    {
        const FDynamicRoomTypeDef& RoomDef = Pair.Value;
        if (RoomVolume < RoomDef.MinimumVolume) continue;

        bool bMeetsRequirements = true;
        for (const FDynamicRoomRequirement& Req : RoomDef.ContentRequirements)
        {
            const int32 FoundCount = ContentCounts.FindRef(Req.RequiredContentTag);
            if (FoundCount < Req.MinimumCount)
            {
                bMeetsRequirements = false;
                break;
            }
        }

        if (bMeetsRequirements && RoomDef.ClassificationPriority > HighestPriority)
        {
            HighestPriority = RoomDef.ClassificationPriority;
            BestRoomTag = RoomDef.RoomTypeTag;
        }
    }

    return BestRoomTag;
}

UDynamicContainerComponent* UDynamicBuildingSubsystem::FindOptimalAIContainerInRoomType(FVector SearchOrigin, FGameplayTag RoomTypeTag, FGameplayTag ItemArchetypeToStore, FGuid OptionalOwnerGuid) const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    const FDynamicRoomInstance* TargetRoom = nullptr;
    float BestDistSq = MAX_flt;

    for (const FDynamicRoomInstance& Room : ActiveRooms)
    {
        if (Room.ClassifiedRoomTypeTag == RoomTypeTag && (!OptionalOwnerGuid.IsValid() || Room.OwnerGuid == OptionalOwnerGuid))
        {
            const float DistSq = FVector::DistSquared(SearchOrigin, Room.WorldBounds.GetCenter());
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                TargetRoom = &Room;
            }
        }
    }

    if (!TargetRoom) return nullptr;

    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

    if (World->OverlapMultiByObjectType(Overlaps, TargetRoom->WorldBounds.GetCenter(), FQuat::Identity, ObjParams, FCollisionShape::MakeBox(TargetRoom->WorldBounds.GetExtent())))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
            {
                if (!TargetRoom->WorldBounds.IsInsideOrOn(PhysActor->GetActorLocation())) continue;

                if (OptionalOwnerGuid.IsValid() && PhysActor->GetItemOwnerGuid().IsValid() && PhysActor->GetItemOwnerGuid() != OptionalOwnerGuid)
                {
                    continue;
                }

                if (UDynamicContainerComponent* Container = PhysActor->GetContainerComponent())
                {
                    int32 IgnoredX = 0, IgnoredY = 0;
                    bool bRot = false;
                    if (Container->FindSpaceForItem(ItemArchetypeToStore, IgnoredX, IgnoredY, bRot))
                    {
                        return Container;
                    }
                }
            }
        }
    }

    return nullptr;
}

bool UDynamicBuildingSubsystem::GetRoomOwnedByCharacter(FGuid CharacterGuid, FGameplayTag RoomTypeTag, FDynamicRoomInstance& OutRoom) const
{
    if (!CharacterGuid.IsValid()) return false;
    
    for (const FDynamicRoomInstance& Room : ActiveRooms)
    {
        if (Room.OwnerGuid == CharacterGuid && Room.ClassifiedRoomTypeTag == RoomTypeTag)
        {
            OutRoom = Room;
            return true;
        }
    }
    return false;
}

bool UDynamicBuildingSubsystem::GetPropertyOwnedByCharacter(FGuid CharacterGuid, FDynamicPropertyInstance& OutProperty) const
{
    if (!CharacterGuid.IsValid()) return false;
    
    for (const FDynamicPropertyInstance& Prop : ActiveProperties)
    {
        if (Prop.OwnerGuid == CharacterGuid) 
        {
            OutProperty = Prop;
            return true;
        }
    }
    return false;
}

UReverbEffect* UDynamicBuildingSubsystem::GetAcousticReverbAtLocation(const FVector& WorldLocation) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return nullptr;

    for (const FDynamicRoomInstance& Room : ActiveRooms)
    {
        if (Room.WorldBounds.IsInsideOrOn(WorldLocation))
        {
            if (const FDynamicRoomTypeDef* RoomDef = GS->RoomTypeRegistry.Find(Room.ClassifiedRoomTypeTag))
            {
                if (!RoomDef->AcousticReverbAsset.IsNull())
                {
                    return Cast<UReverbEffect>(RoomDef->AcousticReverbAsset.LoadSynchronous());
                }
            }
        }
    }
    return nullptr;
}

void UDynamicBuildingSubsystem::ProcessThermodynamicDecay(float DeltaTime)
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    static const FGameplayTag TempTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.Temperature"), false);
    static const FGameplayTag WetnessTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.Wetness"), false);

    // Provide Contextual Damage Tags
    static const FGameplayTag FireDmgTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Fire"), false);
    static const FGameplayTag RotDmgTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.Type.Rot"), false);

    for (const auto& ChunkPair : ActiveBuildingChunks)
    {
        if (ADynamicBuildingChunkActor* Chunk = ChunkPair.Value)
        {
            // We cannot remove elements while iterating if we use the function call, so queue the damage
            TArray<TPair<FGuid, TPair<float, FGameplayTag>>> PendingDamage;

            for (FStructureInstanceItem& Item : Chunk->ReplicatedStructures.Items)
            {
                if (Item.bIsGhost || Item.CurrentHP <= 0.0f) continue;

                if (const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(Item.MaterialTag))
                {
                    if (!MatDef->bIsFlammable && !MatDef->bCanRot) continue;

                    const float CellTemp = GS->GetEnvironmentMetricAtLocation(Item.WorldTransform.GetLocation(), TempTag);
                    const float CellWetness = GS->GetEnvironmentMetricAtLocation(Item.WorldTransform.GetLocation(), WetnessTag);

                    if (MatDef->bIsFlammable && CellTemp >= MatDef->IgnitionTemperature)
                    {
                        const float BurnDamage = MatDef->BurnRatePerSecond * DeltaTime;
                        PendingDamage.Add(TPair<FGuid, TPair<float, FGameplayTag>>(Item.InstanceID, TPair<float, FGameplayTag>(BurnDamage, FireDmgTag)));
                    }
                    else if (MatDef->bCanRot && CellWetness >= MatDef->RotWetnessThreshold)
                    {
                        const float RotDamage = (0.5f) * DeltaTime; 
                        PendingDamage.Add(TPair<FGuid, TPair<float, FGameplayTag>>(Item.InstanceID, TPair<float, FGameplayTag>(RotDamage, RotDmgTag)));
                    }
                }
            }

            for (const auto& DmgReq : PendingDamage)
            {
                Chunk->ServerDamageBuildingInstance(DmgReq.Key, DmgReq.Value.Key, FVector::ZeroVector, FVector::ZeroVector, nullptr, DmgReq.Value.Value);
            }
        }
    }
}