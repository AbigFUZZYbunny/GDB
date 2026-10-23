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
    virtual void PostInitializeComponents() override;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    USkeletalMeshComponent* GetMesh() const { return MeshComponent; }
    UPhysicsControlComponent* GetPhysicsControlComponent() const { return PhysicsControlComponent; }
    UDynamicAttributeSet* GetDynamicAttributeSet() const { return AttributeSet; }

    virtual void ResetToDefaultPoolState() override;

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "DynamicGAS|Combat")
    void Server_RequestAimedStrike(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, double ClientTargetTimestamp);

    void ServerProcessAuthoritativeLimbHit(ADynamicCharacter* TargetCharacter, const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren = true);

    UFUNCTION(BlueprintNativeEvent, Category = "DynamicGAS|Physics")
    void ApplyJointMotorMultipliers(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier);

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

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Movement")
    bool ServerSetMovementState(FGameplayTag NewStateTag);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Movement")
    void ApplyMovementStateConfig(const FDynamicMovementStateDef& Def);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DynamicGAS|Movement")
    FGameplayTag DefaultMovementStateTag;

    UPROPERTY(ReplicatedUsing=OnRep_ActiveMovementState, BlueprintReadOnly, Category = "DynamicGAS|Movement")
    FGameplayTag ActiveMovementStateTag;

    UFUNCTION()
    void OnRep_ActiveMovementState();

    UFUNCTION(BlueprintPure, Category = "DynamicGAS|Physics|Mass")
    float CalculateTotalInventoryAndEquipmentMass() const;

    UFUNCTION(BlueprintPure, Category = "DynamicGAS|Physics|Mass")
    float GetEffectiveClimbingLoad() const;

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void ConfigureClimbingPhysics(bool bEnableClimbing, bool bHasFooting = false);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void UpdateAutomatedClimbingState(float DeltaSeconds);

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;

    UFUNCTION()
    void OnServerSkeletalMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void OnServerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    void HandleMovementModePhysicsStateChanged(const FGameplayTag& OldStateTag, const FGameplayTag& NewStateTag);
    void ProcessDynamicImpact(const FGameplayTag& ImpactTypeTag, const FHitResult& Hit, float ImpactVelocity);

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
    TObjectPtr<UDynamicAttributeSet> AttributeSet;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UDynamicGASManagerComponent* DynamicGASManager;

private:
    void InitializeAbilitySystem();
    float CalculateAuthoritativeStrikeForce() const;
    void SetupDefaultMoverModes();

    bool bIsRagdolling;
    bool bAttributesInitializedOnServer = false;
    TMap<uint8, double> ServerLastLimbHitTimestamp;

    static constexpr int32 HistoryRingBufferSize = 128;
    static constexpr double MaxHistorySeconds = 0.5;

    UPROPERTY()
    TArray<FCharacterTransformHistorySnapshot> TransformHistoryBuffer;
    int32 HistoryBufferWriteIndex = 0;
    int32 HistoryBufferCount = 0;

    FCharacterTransformHistorySnapshot PreRewindCachedTransform;
    bool bIsCurrentlyRewound = false;

    bool bPreRewindSimulatingPhysics = false;
    bool bPreRewindMoverActive = false;

    bool bIsClimbingPhysical = false;
    bool bCurrentFootingActive = false;
    TMap<FName, FTransform> ActiveLimbHolds;

    float CachedTargetCapsuleHalfHeight = 88.0f;
    FVector PreviousVelocity = FVector::ZeroVector;
    FGameplayTagContainer GrantedMovementTags;

    FVector LastHoldSweepLocation = FVector::ZeroVector;
    float AccumulatedHoldSweepTime = 0.0f;
};