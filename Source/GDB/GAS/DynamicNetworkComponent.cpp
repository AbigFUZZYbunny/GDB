// DynamicNetworkComponent.cpp
#include "DynamicNetworkComponent.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

UDynamicNetworkComponent::UDynamicNetworkComponent()
{
    PrimaryComponentTick.bCanEverTick = false; // Zero tick overhead
    SetIsReplicatedByDefault(true);
    ReplicatedConnections.OwningComponent = this;
}

void UDynamicNetworkComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedConnections.OwningComponent = this;
}

void UDynamicNetworkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        // Copy array to prevent modification during iteration
        TArray<FNetworkConnectionItem> ConnectionsToSever = ReplicatedConnections.Items;
        for (const FNetworkConnectionItem& Item : ConnectionsToSever)
        {
            if (Item.ConnectedNode.IsValid())
            {
                DisconnectFromNode(Item.ConnectedNode.Get());
            }
        }
        
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                GS->UnregisterNetworkNode(this);
            }
        }
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicNetworkComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicNetworkComponent, NetworkNodeDefTag);
    DOREPLIFETIME(UDynamicNetworkComponent, ReplicatedConnections);
}

void FNetworkConnectionItem::PreReplicatedRemove(const FFastNetworkConnectionArray& InArrayOwner)
{
    if (InArrayOwner.OwningComponent) InArrayOwner.OwningComponent->HandleSingleConnectionReplicated(*this, true);
}

void FNetworkConnectionItem::PostReplicatedAdd(const FFastNetworkConnectionArray& InArrayOwner)
{
    if (InArrayOwner.OwningComponent) InArrayOwner.OwningComponent->HandleSingleConnectionReplicated(*this, false);
}

void FNetworkConnectionItem::PostReplicatedChange(const FFastNetworkConnectionArray& InArrayOwner)
{
    if (InArrayOwner.OwningComponent) InArrayOwner.OwningComponent->HandleSingleConnectionReplicated(*this, false);
}

void UDynamicNetworkComponent::HandleSingleConnectionReplicated(const FNetworkConnectionItem& Item, bool bWasRemoved)
{
    OnNetworkConnectionChanged.Broadcast(Item.ConnectedNode.Get(), !bWasRemoved, FGameplayTag::EmptyTag);
}

void UDynamicNetworkComponent::OnRep_NetworkNodeDefTag()
{
    // Local visual hydration if necessary
}

void UDynamicNetworkComponent::OnRep_ReplicatedConnections()
{
    for (const FNetworkConnectionItem& Item : ReplicatedConnections.Items)
    {
        OnNetworkConnectionChanged.Broadcast(Item.ConnectedNode.Get(), true, FGameplayTag::EmptyTag);
    }
}

const FDynamicNetworkNodeDef* UDynamicNetworkComponent::GetNodeDefinition() const
{
    if (!NetworkNodeDefTag.IsValid()) return nullptr;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    return GS ? GS->NetworkMasterRegistry.Find(NetworkNodeDefTag) : nullptr;
}

void UDynamicNetworkComponent::InitializeNetworkNode(FGameplayTag NodeDefTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NodeDefTag.IsValid()) return;
    NetworkNodeDefTag = NodeDefTag;
    
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            GS->RegisterNetworkNode(this);
        }
    }
}

bool UDynamicNetworkComponent::ConnectToNode(UDynamicNetworkComponent* OtherNode)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !OtherNode || OtherNode == this) return false;

    auto IsConnected = [](const FFastNetworkConnectionArray& Array, UDynamicNetworkComponent* Target) {
        return Array.Items.ContainsByPredicate([Target](const FNetworkConnectionItem& Item) { return Item.ConnectedNode.Get() == Target; });
    };

    if (IsConnected(ReplicatedConnections, OtherNode)) return false;

    FNetworkConnectionItem NewConnection;
    NewConnection.ConnectedNode = OtherNode;

    FNetworkConnectionItem& Added = ReplicatedConnections.Items.Add_GetRef(NewConnection);
    ReplicatedConnections.MarkItemDirty(Added);
    ReplicatedConnections.MarkArrayDirty();

    // Symmetrical connection
    if (!IsConnected(OtherNode->ReplicatedConnections, this))
    {
        OtherNode->ConnectToNode(this);
    }

    if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
    {
        GS->MarkNetworkGraphDirty(this);
    }

    return true;
}

bool UDynamicNetworkComponent::DisconnectFromNode(UDynamicNetworkComponent* OtherNode)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !OtherNode) return false;

    const int32 Removed = ReplicatedConnections.Items.RemoveAll([OtherNode](const FNetworkConnectionItem& Item) {
        return Item.ConnectedNode.Get() == OtherNode;
    });

    if (Removed > 0)
    {
        ReplicatedConnections.MarkArrayDirty();
        OtherNode->DisconnectFromNode(this); // Symmetrical removal

        if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
        {
            GS->MarkNetworkGraphDirty(this);
            GS->MarkNetworkGraphDirty(OtherNode);
        }
        return true;
    }
    return false;
}

void UDynamicNetworkComponent::ExecuteBurstTransfer(FGameplayTag ResourceTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ResourceTag.IsValid()) return;
    
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        GS->QueueNetworkBurstTransfer(this, ResourceTag);
    }
}