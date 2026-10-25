// DynamicBuildingSubsystem.cpp
#include "DynamicBuildingSubsystem.h"
#include "DynamicGameState.h"
#include "GameFramework/PlayerController.h"
#include "DynamicLogicEvaluator.h"
#include "AbilitySystemComponent.h"
#include "DynamicGASManagerComponent.h"
#include "Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h"

void UDynamicBuildingSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); }
void UDynamicBuildingSubsystem::Deinitialize() { ActiveBuildingChunks.Empty(); Super::Deinitialize(); }
void UDynamicBuildingSubsystem::Tick(float DeltaTime) { Super::Tick(DeltaTime); }

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
    const FDynamicStructureMaterialDef* MatDef = GS->StructureMaterialRegistry.Find(MaterialTag);

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

    ADynamicBuildingChunkActor* Chunk = GetOrCreateChunkAtLocation(IntendedTransform.GetLocation());
    if (!Chunk) return false;

    FStructureInstanceItem NewItem;
    NewItem.InstanceID = FGuid::NewGuid();
    NewItem.PieceTag = PieceTag;
    NewItem.MaterialTag = MaterialTag;
    NewItem.WorldTransform = IntendedTransform;
    NewItem.CurrentHP = PieceDef->BaseMaxHP * MatDef->DurabilityMultiplier;
    NewItem.SupportValue = 0.0f; 
    
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

    float TotalInsulation = 0.0f;
    int32 ValidHits = 0;

    const int32 NumRays = 16;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShelterEvaluation), false);
    
    for (int32 i = 0; i < NumRays; ++i)
    {
        float Z = FMath::RandRange(0.1f, 1.0f);
        float Phi = FMath::RandRange(0.0f, PI * 2.0f);
        float R = FMath::Sqrt(1.0f - Z*Z);
        FVector Dir(R * FMath::Cos(Phi), R * FMath::Sin(Phi), Z);
        
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, WorldLocation, WorldLocation + (Dir * 1000.0f), ECC_Visibility, Params))
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
                                if (const FDynamicStructureMaterialDef* MatDef = GS->StructureMaterialRegistry.Find(Item->MaterialTag))
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
    // Streams building chunks similarly to voxel chunks, dropping actors that have no players within 400m
    // and storing their FastArray state in a virtualized hash map if needed.
    // (Implementation skipped for brevity, follows exact logic of Voxel streaming virtualization)
}

void UDynamicBuildingSubsystem::SerializeWorldBuildingState(FWorldPersistenceSnapshot& OutSnapshot)
{
    // Map ActiveBuildingChunks to OutSnapshot.ModifiedBuildingChunks
}

void UDynamicBuildingSubsystem::RestoreWorldBuildingState(const FWorldPersistenceSnapshot& InSnapshot)
{
    // Map InSnapshot.ModifiedBuildingChunks back to chunks via GetOrCreateChunkAtLocation
}