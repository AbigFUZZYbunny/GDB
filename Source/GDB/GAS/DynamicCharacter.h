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
#include "DynamicGASTypes.h"
#include "DynamicCharacter.generated.h"

class UAbilitySystemComponent;
class UDynamicAttributeSet;
class UDynamicGASManagerComponent;
class UDynamicForceComponent;
class UDynamicCustomizationComponent;

// Server-side lag compensation snapshot
USTRUCT()
struct FCharacterTransformHistorySnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    double ServerTimestamp = 0.0;

    UPROPERTY()
    FVector CapsuleLocation = FVector::ZeroVector;

    UPROPERTY()
    FQuat CapsuleRotation = FQuat::Identity;

    UPROPERTY()
    TMap<FName, FTransform> BoneTransforms;
};

UCLASS()
class GDB_API ADynamicCharacter : public APawn, public IAbilitySystemInterface, public IDynamicPoolScrubbableInterface
{
    GENERATED_BODY()

public:
    ADynamicCharacter(const FObjectInitializer& ObjectInitializer);

    virtual void Tick(float DeltaSeconds) override;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    USkeletalMeshComponent* GetMesh() const { return MeshComponent; }
    UPhysicsControlComponent* GetPhysicsControlComponent() const { return PhysicsControlComponent; }
    const UDynamicAttributeSet* GetDynamicAttributeSet() const { return AttributeSet; }

    // IDynamicPoolScrubbableInterface implementation
    virtual void ResetToDefaultPoolState() override;

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "DynamicGAS|Combat")
    void Server_RequestAimedStrike(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, double ClientTargetTimestamp);

    void ServerProcessAuthoritativeLimbHit(ADynamicCharacter* TargetCharacter, const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren = true);
    
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DynamicGAS|Interaction")
    void AlignLimbToInteractionTarget(FName EffectorBoneName, const FTransform& TargetWorldTransform, USceneComponent* TargetComponent);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customization", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UDynamicCustomizationComponent> CustomizationComponent;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Customization")
    UDynamicCustomizationComponent* GetCustomizationComponent() const { return CustomizationComponent; }

    UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
    void StartRagdoll();

    UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
    void RecoverFromRagdoll();

    void RewindToHistoricalSnapshot(double TargetTimestamp);
    void RestoreFromHistoricalSnapshot();
    
    UFUNCTION(BlueprintNativeEvent, Category = "DynamicGAS|Physics")
    void ApplyJointMotorMultipliers(
        FName BoneName, 
        uint8 JointChainIndex, 
        float AngularSpringMultiplier, 
        float LinearSpringMultiplier, 
        float DampingMultiplier
    );
    virtual void ApplyJointMotorMultipliers_Implementation(
        FName BoneName, 
        uint8 JointChainIndex, 
        float AngularSpringMultiplier, 
        float LinearSpringMultiplier, 
        float DampingMultiplier
    );

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics", meta = (AllowPrivateAccess = "true"))
    UDynamicForceComponent* ForceComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UAbilitySystemComponent* AbilitySystemComponent;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Abilities")
    const UDynamicAttributeSet* AttributeSet;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UDynamicGASManagerComponent* DynamicGASManager;

private:
    void InitializeAbilitySystem();
    float CalculateAuthoritativeStrikeForce() const;

    bool bIsRagdolling;
    bool bAttributesInitializedOnServer = false;
    TMap<uint8, double> ServerLastLimbHitTimestamp;

    // Fixed-size pre-allocated circular history buffer (replaces O(N) array shifting)
    static constexpr int32 HistoryRingBufferSize = 128;
    static constexpr double MaxHistorySeconds = 0.5;

    UPROPERTY()
    TArray<FCharacterTransformHistorySnapshot> TransformHistoryBuffer;

    int32 HistoryBufferWriteIndex = 0;
    int32 HistoryBufferCount = 0;

    FCharacterTransformHistorySnapshot PreRewindCachedTransform;
    bool bIsCurrentlyRewound = false;
};