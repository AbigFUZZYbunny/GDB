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
#include "Components/SphereComponent.h"
#include "DynamicGASTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "DynamicLifeComponent.h"
#include "DynamicLanguageComponent.h"
#include "DynamicQuestComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicCharacter.generated.h"

class UAbilitySystemComponent;
class UDynamicAttributeSet;
class UDynamicGASManagerComponent;
class UDynamicForceComponent;
class UDynamicInteractableComponent;
class UPhysicsConstraintComponent;

UCLASS()
class GDB_API ADynamicCharacter : public APawn, public IAbilitySystemInterface, public IDynamicPoolScrubbableInterface
{
    GENERATED_BODY()

public:
    ADynamicCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void Tick(float DeltaSeconds) override;
    virtual void PostInitializeComponents() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FollowCamera;

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Camera")
    void ApplyCameraProfile(FGameplayTag ProfileTag);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Camera")
    void SetCameraPerspective(bool bFirstPerson);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Camera")
    void ZoomCamera(float Delta);

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Camera")
    void TogglePerspective();

    UPROPERTY(BlueprintReadOnly, Category = "DynamicGAS|Camera")
    FGameplayTag ActiveCameraProfileTag;

    UPROPERTY(BlueprintReadOnly, Category = "DynamicGAS|Camera")
    bool bIsFirstPerson = false;

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    UPhysicsControlComponent* GetPhysicsControlComponent() const { return PhysicsControlComponent; }
    UDynamicAttributeSet* GetDynamicAttributeSet() const { return AttributeSet; }

    virtual void ResetToDefaultPoolState() override;

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "DynamicGAS|Combat")
    void Server_RequestAimedStrike(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, double ClientTargetTimestamp);

    void ServerProcessAuthoritativeLimbHit(
        ADynamicCharacter* TargetCharacter, 
        const FName& HitBoneName, 
        float RawImpulseMagnitude, 
        float MassRatio, 
        const FVector& HitLocation, 
        const FVector& HitNormal, 
        AActor* InstigatorActor,
        const FGameplayTagContainer& ImpactTypeTags = FGameplayTagContainer()
    );

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren = true);

    UFUNCTION(BlueprintNativeEvent, Category = "DynamicGAS|Physics")
    void ApplyJointMotorMultipliers(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DynamicGAS|Interaction")
    void AlignLimbToInteractionTarget(FName EffectorBoneName, const FTransform& TargetWorldTransform, USceneComponent* TargetComponent);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicCustomizationComponent> CustomizationComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicSocialComponent> SocialComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicEquipmentComponent> EquipmentComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicContainerComponent> ContainerComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicLifeComponent> LifeComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicLanguageComponent> LanguageComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDynamicQuestComponent> QuestComponent;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Customization")
    UDynamicCustomizationComponent* GetCustomizationComponent() const { return CustomizationComponent; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Social")
    UDynamicSocialComponent* GetSocialComponent() const { return SocialComponent; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quests")
    UDynamicQuestComponent* GetQuestComponent() const { return QuestComponent; }

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

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Combat")
    virtual void ServerProcessDeath(AActor* KillerActor);
    
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Mount")
    void ServerSetMount(AActor* MountActor, FGameplayTag SaddlePointTag, float BreakForce, float SaddleSlop);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Mount")
    void ServerDismount();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Tether")
    void ServerEstablishTether(AActor* TargetActor, FName EffectorBone, FGameplayTag TargetPointTag, float MaxLength, float BreakForce);

    UPROPERTY(ReplicatedUsing=OnRep_MountState, BlueprintReadOnly, Category = "DynamicGAS|Mount")
    TWeakObjectPtr<AActor> CurrentMount;

    UFUNCTION()
    void OnRep_MountState();

    UFUNCTION()
    void OnPhysicsConstraintBroken(int32 ConstraintIndex);

    void EvaluatePhysicalEncumbrance(float& OutTotalMass, FVector& OutCoMOffset) const;
    void ApplyEnvironmentalForces(float DeltaSeconds);

    // Dynamic Navigation Traversal
    UFUNCTION(BlueprintPure, Category = "DynamicGAS|Navigation")
    bool CanInitiateEmergencyClimbOrVault() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "DynamicGAS|Navigation")
    bool TryExecuteLedgeTraversal(const FVector& TargetLedgeLocation);
    
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "DynamicGAS|Movement")
    float TargetPaceScale = 1.0f;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "DynamicGAS|Movement")
    float TargetStanceScale = 1.0f; // 1.0 = Stand, 0.5= Crouch, 0.0 = Prone

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;

    UFUNCTION()
    void OnServerSkeletalMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void OnServerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void OnClimbProximityOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnClimbProximityEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    void HandleMovementModePhysicsStateChanged(const FGameplayTag& OldStateTag, const FGameplayTag& NewStateTag);
    void ProcessDynamicImpact(const FGameplayTag& ImpactTypeTag, const FHitResult& Hit, float ImpactVelocity);
    bool CalculatePseudoBoneIntersection(const FCharacterTransformHistorySnapshot& Snapshot, const FVector& StartTrace, const FVector& EndTrace, FName& OutHitBone);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCapsuleComponent* CapsuleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USkeletalMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USphereComponent* ClimbProximitySphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
    UMoverComponent* MoverComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics")
    UPhysicsControlComponent* PhysicsControlComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics")
    UDynamicForceComponent* ForceComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
    UAbilitySystemComponent* AbilitySystemComponent;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Abilities")
    TObjectPtr<UDynamicAttributeSet> AttributeSet;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
    UDynamicGASManagerComponent* DynamicGASManager;
    
    void ServerBroadcastSensoryStimulus(FGameplayTag StimulusTag, float BaseMagnitudeOverride);
    
    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void ConfigureRagdollJoints();

    UFUNCTION(BlueprintCallable, Category = "DynamicGAS|Physics")
    void UpdateRagdollHyperextension(float DeltaSeconds);
    
    void UpdateNetworkDormancy(float DeltaSeconds);
    
    UPROPERTY(Transient)
    TObjectPtr<UPhysicsConstraintComponent> ActivePhysicsConstraint;

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

    UPROPERTY()
    TArray<TWeakObjectPtr<UDynamicInteractableComponent>> NearbyClimbHolds;

    TMap<FName, FTransform> ActiveLimbHolds;

    float CachedTargetCapsuleHalfHeight = 88.0f;
    FVector PreviousVelocity = FVector::ZeroVector;
    FGameplayTagContainer GrantedMovementTags;
    FVector LastHoldSweepLocation = FVector::ZeroVector;
    float AccumulatedHoldSweepTime = 0.0f;
    
    float HistorySaveAccumulator = 0.0f;
    static constexpr float HistorySaveInterval = 1.0f / 20.0f;
    
    mutable float CachedTotalMass = 80.0f;
    mutable double LastMassCalculateTime = -100.0;
    
    TSet<FName> HyperextendedJoints;
    float IdleDormancyTimer = 0.0f;
};