// DynamicCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "MoverComponent.h"
#include "PhysicsControlComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DynamicCharacter.generated.h"

class UAbilitySystemComponent;
class UDynamicAttributeSet;
class UDynamicGASManagerComponent;
class UDynamicForceComponent;

UCLASS()
class GDB_API ADynamicCharacter : public APawn, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ADynamicCharacter(const FObjectInitializer& ObjectInitializer);

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    
    // Mesh accessor for Pawn-derived DynamicCharacter
    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    USkeletalMeshComponent* GetMesh() const { return MeshComponent; }

    // Secure aim-input trace strictly evaluated on server
    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "DynamicGAS|Combat")
    void Server_RequestAimedStrike(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, float ClientStrikeForce);

    void ServerProcessAuthoritativeLimbHit(ADynamicCharacter* TargetCharacter, const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor);

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;

    // Strictly server-bound Chaos collision handler
    UFUNCTION()
    void OnServerSkeletalMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    UCapsuleComponent* CapsuleComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    USkeletalMeshComponent* MeshComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
    UMoverComponent* MoverComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics", meta = (AllowPrivateAccess = "true"))
    UPhysicsControlComponent* PhysicsControlComponent;
    
    // Required to execute fast replication ring buffers for kinetic flinches
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics", meta = (AllowPrivateAccess = "true"))
    UDynamicForceComponent* ForceComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UAbilitySystemComponent* AbilitySystemComponent;
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Abilities")
    const UDynamicAttributeSet* AttributeSet;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UDynamicGASManagerComponent* DynamicGASManager;

    UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
    void StartRagdoll();
    UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
    void RecoverFromRagdoll();

private:
    void InitializeAbilitySystem();
    bool bIsRagdolling;
    
    // Cooldown map to prevent network saturation from physics jitter
    TMap<uint8, double> ServerLastLimbHitTimestamp;
};