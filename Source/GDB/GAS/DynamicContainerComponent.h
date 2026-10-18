// DynamicContainerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicContainerComponent.generated.h"

class ADynamicPhysicsActor;
class ADynamicCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDynamicContainerSlotChangedSignature, int32, GridX, int32, GridY, FGameplayTag, StoredArchetypeTag, int32, NewStackCount);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicContainerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicContainerComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Container")
    FDynamicContainerSlotChangedSignature OnContainerSlotChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Container")
    FDynamicContainerDef ContainerConfig;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSlots)
    FFastContainerSlotArray ReplicatedSlots;

    UFUNCTION()
    void OnRep_ReplicatedSlots();

    void HandleSingleSlotReplicated(const FDynamicContainerSlotItem& Item, bool bWasRemoved = false);
    void InitializeContainerFromDef(const FDynamicContainerDef& InDef);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Container")
    bool IsGridSpaceFree(int32 GridX, int32 GridY, int32 SizeX, int32 SizeY, int32 IgnoreFlatKey = -1) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Container")
    bool FindSpaceForItem(FGameplayTag ArchetypeTag, int32& OutGridX, int32& OutGridY, bool& OutRotated) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Container")
    bool ServerStorePhysicalActor(ADynamicPhysicsActor* ActorToStore, AActor* InstigatorActor = nullptr);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Container")
    bool ServerAddActorByTag(FGameplayTag ArchetypeTag, int32 Quantity, const TArray<FDynamicAttributeItem>& CustomAttributes);

    // FIXED: Properly declared inside the class header
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Container")
    bool ServerRemoveStoredActorByTag(FGameplayTag ArchetypeTag, int32 Quantity = 1);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Container")
    ADynamicPhysicsActor* ServerExtractActorToWorld(int32 GridX, int32 GridY, int32 QuantityToExtract, const FTransform& SpawnTransform, AActor* InstigatorActor = nullptr);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Container")
    ADynamicPhysicsActor* ServerExtractActorToHands(int32 GridX, int32 GridY, ADynamicCharacter* Looter, FName HandBone = TEXT("hand_r"));

    // FIXED: Properly declared inside the class header
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Container")
    int32 GetTotalCountOfArchetype(FGameplayTag ArchetypeTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Container")
    float GetCurrentTotalWeight() const { return CachedTotalWeight; }

    FFastContainerSlotArray& GetMutableReplicatedSlots() { return ReplicatedSlots; }
    const FFastContainerSlotArray& GetReplicatedSlots() const { return ReplicatedSlots; }

private:
    void RebuildLocalSlotCache();
    float ResolveArchetypeUnitWeight(FGameplayTag ArchetypeTag, const TArray<FDynamicAttributeItem>& AttributeOverrides) const;

    TMap<int32, int32> FlatKeyToArrayIndexMap; 
    float CachedTotalWeight = 0.0f;
};