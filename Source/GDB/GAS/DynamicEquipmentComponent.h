// DynamicEquipmentComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicEquipmentComponent.generated.h"

class ADynamicPhysicsActor;
class UDynamicEvolutionComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicEquipmentChangedSignature, FGameplayTag, SlotTag, FGameplayTag, ItemArchetypeTag, bool, bIsEquipped);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicEquipmentComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Equipment")
    FDynamicEquipmentChangedSignature OnEquipmentChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Equipment")
    TArray<FDynamicEquipmentSlotProfile> ConfiguredSlots;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedEquipmentSlots)
    FFastEquipmentSlotArray ReplicatedEquipmentSlots;

    UFUNCTION()
    void OnRep_ReplicatedEquipmentSlots();

    void HandleSingleSlotReplicated(const FDynamicEquipmentSlotItem& Item, bool bIsUnequipped);
    void InitializeEquipmentSlots(const TArray<FDynamicEquipmentSlotProfile>& InSlots);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Equipment")
    bool CanEquipItem(ADynamicPhysicsActor* ActorToEquip) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Equipment")
    bool ServerEquipItem(ADynamicPhysicsActor* ActorToEquip);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Equipment")
    ADynamicPhysicsActor* ServerUnequipSlot(FGameplayTag SlotTag, const FTransform& SpawnTransform);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Equipment")
    FGameplayTag GetEquippedItemInSlot(FGameplayTag SlotTag) const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic GAS|Equipment Evolution")
    TObjectPtr<UDynamicEvolutionComponent> ItemEvolutionComponent;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Equipment Evolution")
    void ServerGrantItemXP(float XPGrantAmount);

private:
    TMap<FGameplayTag, int32> SlotTagToArrayIndexMap;
    void RebuildLocalCache();
};