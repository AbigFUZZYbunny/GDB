// DynamicItemChunkActor.cpp
#include "DynamicItemChunkActor.h"
#include "DynamicGameState.h"
#include "DynamicGroundItemSubsystem.h"
#include "DynamicPhysicsActor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Net/UnrealNetwork.h"
#include "Runtime/Engine/Public/Net/UnrealNetwork.h"
#include "Engine/AssetManager.h"
#include "Runtime/Engine/Classes/Engine/StreamableManager.h"

ADynamicItemChunkActor::ADynamicItemChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = false;
    NetPriority = 1.0f;
    SetNetUpdateFrequency(2.0f);
    
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    ReplicatedGroundItems.OwningChunk = this;
}

void ADynamicItemChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ADynamicItemChunkActor, ChunkCoord, COND_InitialOnly);
    DOREPLIFETIME(ADynamicItemChunkActor, ReplicatedGroundItems);
}

void ADynamicItemChunkActor::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedGroundItems.OwningChunk = this;
}

void ADynamicItemChunkActor::InitializeChunk(FIntPoint InCoord, float InChunkSize)
{
    ChunkCoord = InCoord;
    ChunkWorldSize = InChunkSize;
    SetActorLocation(FVector(ChunkCoord.X * ChunkWorldSize, ChunkCoord.Y * ChunkWorldSize, 0.0f));
    SetNetCullDistanceSquared(FMath::Square(ChunkWorldSize * 2.0f));
}

void ADynamicItemChunkActor::OnRep_ChunkCoord() { UpdateInstancedMeshes(); }
void ADynamicItemChunkActor::OnRep_GroundItems() { UpdateInstancedMeshes(); }
void ADynamicItemChunkActor::HandleSingleItemReplicated(const FGroundItemRecordItem& Item) { UpdateInstancedMeshes(); }

void FGroundItemRecordItem::PreReplicatedRemove(const FFastGroundItemArray& InArrayOwner) { if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleItemReplicated(*this); }
void FGroundItemRecordItem::PostReplicatedAdd(const FFastGroundItemArray& InArrayOwner) { if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleItemReplicated(*this); }
void FGroundItemRecordItem::PostReplicatedChange(const FFastGroundItemArray& InArrayOwner) { if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleItemReplicated(*this); }

UHierarchicalInstancedStaticMeshComponent* ADynamicItemChunkActor::GetOrCreateHISM(FGameplayTag ArchetypeTag)
{
    if (UHierarchicalInstancedStaticMeshComponent** Found = InstancedMeshPool.Find(ArchetypeTag)) return *Found;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    const FDynamicPhysicsActorDef* ActorDef = GS->MasterPhysicsActorRegistry.Find(ArchetypeTag);
    if (!ActorDef) return nullptr;

    UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    
    // Enable physical collisions for HISM instances
    NewHISM->SetCollisionProfileName(TEXT("PhysicsActor"));
    NewHISM->SetNotifyRigidBodyCollision(true);
    
    // Bind kinetic wake-up listener
    NewHISM->OnComponentHit.AddUniqueDynamic(this, &ADynamicItemChunkActor::OnHISMHit);

    if (!ActorDef->PresentationAsset.IsNull())
    {
        FSoftObjectPath AssetPath = ActorDef->PresentationAsset.ToSoftObjectPath();
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

    InstancedMeshPool.Add(ArchetypeTag, NewHISM);
    return NewHISM;
}

void ADynamicItemChunkActor::OnHISMHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !OtherActor || Hit.Item == INDEX_NONE) return;

    // Ignore tiny settling collisions to prevent infinite sleep/wake loops
    if (OtherActor->GetVelocity().SizeSquared() < 5000.0f) return;

    UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(HitComponent);
    if (!HISM) return;

    if (const TMap<int32, FGuid>* HitMap = InstanceHitMap.Find(HISM))
    {
        if (const FGuid* TargetGuid = HitMap->Find(Hit.Item))
        {
            if (UDynamicGroundItemSubsystem* Sub = GetWorld()->GetSubsystem<UDynamicGroundItemSubsystem>())
            {
                // Instantiate the real physics actor from the Virtual registry
                if (ADynamicPhysicsActor* WokenActor = Sub->WakeGroundItem(*TargetGuid, ChunkCoord, OtherActor))
                {
                    // Transfer the kinetic momentum of the player/actor that kicked it
                    if (UPrimitiveComponent* Prim = WokenActor->GetActivePhysicsPrimitive())
                    {
                        const FVector TransferImpulse = OtherActor->GetVelocity() * (OtherComp ? OtherComp->GetMass() : 80.0f);
                        Prim->AddImpulseAtLocation(TransferImpulse, Hit.ImpactPoint);
                    }
                }
            }
        }
    }
}

void ADynamicItemChunkActor::UpdateInstancedMeshes()
{
    for (auto& Pair : InstancedMeshPool)
    {
        if (Pair.Value) Pair.Value->ClearInstances();
    }
    
    InstanceHitMap.Empty();

    struct FBulkItemPayload
    {
        TArray<FTransform> Transforms;
        TArray<FGuid> InstanceIDs;
    };
    TMap<UHierarchicalInstancedStaticMeshComponent*, FBulkItemPayload> BulkAddMap;

    for (const FGroundItemRecordItem& Item : ReplicatedGroundItems.Items)
    {
        if (UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Item.Payload.ActorArchetypeTag))
        {
            FBulkItemPayload& Payload = BulkAddMap.FindOrAdd(HISM);
            Payload.Transforms.Add(Item.Payload.WorldTransform);
            Payload.InstanceIDs.Add(Item.InstanceID);
        }
    }

    for (auto& Pair : BulkAddMap)
    {
        UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Key;
        const FBulkItemPayload& Payload = Pair.Value;

        TArray<int32> AssignedIndices = HISM->AddInstances(Payload.Transforms, false, true);
        TMap<int32, FGuid>& LocalHitMap = InstanceHitMap.FindOrAdd(HISM);

        for (int32 i = 0; i < AssignedIndices.Num() && i < Payload.InstanceIDs.Num(); ++i)
        {
            LocalHitMap.Add(AssignedIndices[i], Payload.InstanceIDs[i]);
        }
        
        HISM->BuildTreeIfOutdated(true, false);
        HISM->MarkRenderStateDirty();
    }
}