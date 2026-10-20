// DynamicPhysicsActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "PhysicsControlComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicPhysicsActor.generated.h"

class UAbilitySystemComponent;
class UDynamicAttributeSet;
class UDynamicGASManagerComponent;
class UDynamicForceComponent;
class UDynamicInteractableComponent;
class UDynamicContainerComponent;

UCLASS()
class GDB_API ADynamicPhysicsActor : public AActor, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ADynamicPhysicsActor(const FObjectInitializer& ObjectInitializer);
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }
    UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
    UPrimitiveComponent* GetActivePhysicsPrimitive() const;
    UPhysicsControlComponent* GetPhysicsControlComponent() const { return PhysicsControlComponent; }
    const UDynamicAttributeSet* GetDynamicAttributeSet() const { return AttributeSet; }
    UDynamicInteractableComponent* GetInteractableComponent() const { return InteractableComponent; }
    UDynamicContainerComponent* GetContainerComponent(FGameplayTag ContainerIdentifierTag = FGameplayTag::EmptyTag) const;
    FGameplayTag GetActorArchetypeTag() const { return ActorArchetypeTag; }

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|PhysicsActor")
    void InitializeFromArchetype(FGameplayTag InArchetypeTag);

    void RestoreFromContainerSnapshot(const FDynamicContainerSlotItem& Snapshot);
    void DeactivateAndReturnToPool();
    void ServerProcessAuthoritativeLimbHit(const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren = true);

    UFUNCTION(BlueprintNativeEvent, Category = "DynamicGAS|Physics")
    void ApplyJointMotorMultipliers(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier);

    const FPhysicsLimbConfig* ResolveActorLimbConfig(uint8 LimbId) const;
    uint8 ResolveLimbIdForBone(const FName& BoneName) const;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnServerPrimitiveHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void OnRep_ActorArchetypeTag();

    UFUNCTION()
    void OnRep_IsActiveInWorld();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics")
    TObjectPtr<UPhysicsControlComponent> PhysicsControlComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics")
    TObjectPtr<UDynamicForceComponent> ForceComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Abilities")
    TObjectPtr<const UDynamicAttributeSet> AttributeSet;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
    TObjectPtr<UDynamicGASManagerComponent> DynamicGASManager;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
    TObjectPtr<UDynamicInteractableComponent> InteractableComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
    TObjectPtr<UDynamicContainerComponent> ContainerComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_ActorArchetypeTag, Category = "Dynamic Configuration")
    FGameplayTag ActorArchetypeTag;

    UPROPERTY(ReplicatedUsing = OnRep_IsActiveInWorld)
    bool bIsActiveInWorld = true;

private:
    void ApplyArchetypePresentationAndTopology(const FDynamicPhysicsActorDef& Def);

    TMap<uint8, const FPhysicsLimbConfig*> LocalLimbLookupMap;
    TMap<FName, uint8> LocalBoneToLimbMap;
    TMap<uint8, double> ServerLastLimbHitTimestamp;
    bool bAttributesInitializedOnServer = false;
};