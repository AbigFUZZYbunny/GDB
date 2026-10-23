// DynamicNetworkComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicNetworkComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnNetworkConnectionChangedSignature, class UDynamicNetworkComponent*, OtherNode, bool, bIsConnected, FGameplayTag, ResourceTag);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicNetworkComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicNetworkComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Network")
    FOnNetworkConnectionChangedSignature OnNetworkConnectionChanged;

    UPROPERTY(ReplicatedUsing = OnRep_NetworkNodeDefTag, BlueprintReadOnly, Category = "Dynamic GAS|Network")
    FGameplayTag NetworkNodeDefTag;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedConnections)
    FFastNetworkConnectionArray ReplicatedConnections;

    UFUNCTION()
    void OnRep_NetworkNodeDefTag();

    UFUNCTION()
    void OnRep_ReplicatedConnections();

    void HandleSingleConnectionReplicated(const FNetworkConnectionItem& Item, bool bWasRemoved);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Network")
    void InitializeNetworkNode(FGameplayTag NodeDefTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Network")
    bool ConnectToNode(UDynamicNetworkComponent* OtherNode);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Network")
    bool DisconnectFromNode(UDynamicNetworkComponent* OtherNode);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Network")
    void ExecuteBurstTransfer(FGameplayTag ResourceTag);

    const FDynamicNetworkNodeDef* GetNodeDefinition() const;

    // Transient Graph Tracking (Server Only)
    TMap<FGameplayTag, int32> ActiveGraphIDs;
};