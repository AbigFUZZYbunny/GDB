// DynamicEnvironmentChunkActor.cpp
#include "DynamicEnvironmentChunkActor.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicEnvironmentChunkActor::ADynamicEnvironmentChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = false;
    NetPriority = 1.5f;

    // Fast array serializer back-pointer
    ChunkCells.OwningChunkActor = this;
}

void ADynamicEnvironmentChunkActor::BeginPlay()
{
    Super::BeginPlay();
    ChunkCells.OwningChunkActor = this;

    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            GS->RegisterEnvironmentChunk(this);
        }
    }
}

void ADynamicEnvironmentChunkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            GS->UnregisterEnvironmentChunk(this);
        }
    }
    Super::EndPlay(EndPlayReason);
}

void ADynamicEnvironmentChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicEnvironmentChunkActor, ChunkCoord);
    DOREPLIFETIME(ADynamicEnvironmentChunkActor, ChunkDimX);
    DOREPLIFETIME(ADynamicEnvironmentChunkActor, ChunkDimY);
    DOREPLIFETIME(ADynamicEnvironmentChunkActor, ChunkCells);
}

void ADynamicEnvironmentChunkActor::InitializeChunk(FIntPoint InCoord, int32 InDimX, int32 InDimY, float CellSizeWorldUnits, const FVector& WorldOrigin)
{
    ChunkCoord = InCoord;
    ChunkDimX = InDimX;
    ChunkDimY = InDimY;

    // Center the chunk actor in the physical world and configure its net cull radius
    const float ChunkWorldWidth = InDimX * CellSizeWorldUnits;
    const float ChunkWorldHeight = InDimY * CellSizeWorldUnits;
    const FVector CenterLocation = WorldOrigin + FVector(ChunkWorldWidth * 0.5f, ChunkWorldHeight * 0.5f, 0.0f);
    SetActorLocation(CenterLocation);

    // Clients cull this chunk beyond 1.5x chunk diameter, keeping replication tightly localized
    const float CullRadius = FMath::Max(ChunkWorldWidth, ChunkWorldHeight) * 1.5f;
    SetNetCullDistanceSquared(FMath::Square(CullRadius));
}

void ADynamicEnvironmentChunkActor::UpdateCellData(int32 GlobalCellIndex, const FEnvironmentGridCellItem& NewCellData)
{
    if (const int32* FoundLocalIdx = GlobalToLocalCellIndexMap.Find(GlobalCellIndex))
    {
        ChunkCells.Items[*FoundLocalIdx] = NewCellData;
        ChunkCells.MarkItemDirty(ChunkCells.Items[*FoundLocalIdx]);
        return;
    }

    const int32 NewIdx = ChunkCells.Items.Add(NewCellData);
    GlobalToLocalCellIndexMap.Add(GlobalCellIndex, NewIdx);
    ChunkCells.MarkItemDirty(ChunkCells.Items[NewIdx]);
}

const FEnvironmentGridCellItem* ADynamicEnvironmentChunkActor::FindCellItemByGlobalIndex(int32 GlobalCellIndex) const
{
    if (const int32* FoundLocalIdx = GlobalToLocalCellIndexMap.Find(GlobalCellIndex))
    {
        if (ChunkCells.Items.IsValidIndex(*FoundLocalIdx))
        {
            return &ChunkCells.Items[*FoundLocalIdx];
        }
    }
    return nullptr;
}

void ADynamicEnvironmentChunkActor::OnRep_ChunkCells()
{
    GlobalToLocalCellIndexMap.Empty(ChunkCells.Items.Num());
    for (int32 i = 0; i < ChunkCells.Items.Num(); ++i)
    {
        GlobalToLocalCellIndexMap.Add(ChunkCells.Items[i].CellIndex, i);
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                GS->RecalculateCellPhysicalMetrics(ChunkCells.Items[i]);
            }
        }
    }
}

void ADynamicEnvironmentChunkActor::HandleSingleCellReplicated(const FEnvironmentGridCellItem& Item)
{
    GlobalToLocalCellIndexMap.Add(Item.CellIndex, ChunkCells.Items.IndexOfByPredicate([&Item](const FEnvironmentGridCellItem& El) {
        return El.CellIndex == Item.CellIndex;
    }));

    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            FEnvironmentGridCellItem& MutableItem = const_cast<FEnvironmentGridCellItem&>(Item);
            GS->RecalculateCellPhysicalMetrics(MutableItem);
        }
    }
}

void FEnvironmentGridCellItem::PostReplicatedAdd(const FFastEnvironmentGridArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunkActor)
    {
        InArrayOwner.OwningChunkActor->HandleSingleCellReplicated(*this);
    }
}

void FEnvironmentGridCellItem::PostReplicatedChange(const FFastEnvironmentGridArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunkActor)
    {
        InArrayOwner.OwningChunkActor->HandleSingleCellReplicated(*this);
    }
}