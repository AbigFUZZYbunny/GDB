// DynamicCharacter.cpp
#include "DynamicCharacter.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicAIController.h"
#include "DynamicGameState.h"
#include "DynamicCustomizationComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicAudioSubsystem.h"
#include "DynamicSocialComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicForceComponent.h"
#include "DynamicWorldDirectorSubsystem.h"
#include "DynamicContainerComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/Modes/SwimmingMode.h"
#include "DynamicLogicEvaluator.h"
#include "Engine/OverlapResult.h"
#include "DynamicPhysicsActor.h"
#include "DynamicStateTreeComponent.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

ADynamicCharacter::ADynamicCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bReplicates = true;
    SetReplicateMovement(false);
    bIsRagdolling = false;
    bCurrentFootingActive = false;
    
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = ADynamicAIController::StaticClass();

    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
    CapsuleComponent->InitCapsuleSize(34.0f, 88.0f);
    CapsuleComponent->SetCollisionProfileName(TEXT("Pawn"));
    CapsuleComponent->CanCharacterStepUpOn = ECB_No;
    CapsuleComponent->SetShouldUpdatePhysicsVolume(true);
    CapsuleComponent->SetCanEverAffectNavigation(true);
    CapsuleComponent->bDynamicObstacle = true;
    RootComponent = CapsuleComponent;

    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(RootComponent);
    MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f), FRotator(0.0f, -90.0f, 0.0f));
    MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->bMultiBodyOverlap = true;

    MoverComponent = CreateDefaultSubobject<UMoverComponent>(TEXT("MoverComponent"));
    MoverComponent->SetUpdatedComponent(CapsuleComponent);
    MoverComponent->SetIsReplicated(true);
    SetupDefaultMoverModes();
    DefaultMovementStateTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Walk"), false);

    // Instantiation of Modular Data-Driven Components
    CustomizationComponent = CreateDefaultSubobject<UDynamicCustomizationComponent>(TEXT("CustomizationComponent"));
    SocialComponent = CreateDefaultSubobject<UDynamicSocialComponent>(TEXT("SocialComponent"));
    EquipmentComponent = CreateDefaultSubobject<UDynamicEquipmentComponent>(TEXT("EquipmentComponent"));
    ContainerComponent = CreateDefaultSubobject<UDynamicContainerComponent>(TEXT("ContainerComponent"));
    LifeComponent = CreateDefaultSubobject<UDynamicLifeComponent>(TEXT("LifeComponent"));
    LanguageComponent = CreateDefaultSubobject<UDynamicLanguageComponent>(TEXT("LanguageComponent"));
    QuestComponent = CreateDefaultSubobject<UDynamicQuestComponent>(TEXT("QuestComponent"));

    PhysicsControlComponent = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControlComponent"));
    
    ForceComponent = CreateDefaultSubobject<UDynamicForceComponent>(TEXT("ForceComponent"));
    ForceComponent->SetIsReplicated(true);

    AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    DynamicGASManager = CreateDefaultSubobject<UDynamicGASManagerComponent>(TEXT("DynamicGASManager"));
    AttributeSet = CreateDefaultSubobject<UDynamicAttributeSet>(TEXT("DynamicAttributeSet"));

    TransformHistoryBuffer.SetNum(HistoryRingBufferSize);
    HistoryBufferWriteIndex = 0;
    HistoryBufferCount = 0;

    ClimbProximitySphere = CreateDefaultSubobject<USphereComponent>(TEXT("ClimbProximitySphere"));
    ClimbProximitySphere->SetupAttachment(RootComponent);
    ClimbProximitySphere->InitSphereRadius(180.0f);
    ClimbProximitySphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ClimbProximitySphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    ClimbProximitySphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
    ClimbProximitySphere->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
    
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->TargetArmLength = 300.0f;
    CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 60.0f);
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 15.0f;

    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->bUsePawnControlRotation = false;
}

void ADynamicCharacter::SetupDefaultMoverModes()
{
    if (!MoverComponent) return;

    // Register native movement modes using the correct Mover 2.0 API
    MoverComponent->AddMovementModeFromClass(DefaultModeNames::Walking, UWalkingMode::StaticClass());
    MoverComponent->AddMovementModeFromClass(DefaultModeNames::Falling, UFallingMode::StaticClass());
    MoverComponent->AddMovementModeFromClass(DefaultModeNames::Flying, UFlyingMode::StaticClass());
    MoverComponent->AddMovementModeFromClass(DefaultModeNames::Swimming, USwimmingMode::StaticClass());

    // Configure Default Starting Mode
    MoverComponent->StartingMovementMode = DefaultModeNames::Walking;
}

void ADynamicCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (MoverComponent && CapsuleComponent)
    {
        MoverComponent->SetUpdatedComponent(CapsuleComponent);
    }
}

UAbilitySystemComponent* ADynamicCharacter::GetAbilitySystemComponent() const { return AbilitySystemComponent; }

void ADynamicCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicCharacter, ActiveMovementStateTag);
}

void ADynamicCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        if (MeshComponent) MeshComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicCharacter::OnServerSkeletalMeshHit);
        if (CapsuleComponent) CapsuleComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicCharacter::OnServerCapsuleHit);

        if (!IsPlayerControlled())
        {
            if (UWorld* World = GetWorld())
            {
                if (UDynamicWorldDirectorSubsystem* Director = World->GetSubsystem<UDynamicWorldDirectorSubsystem>())
                {
                    Director->RegisterActiveNPC(this);
                }
            }
        }
        
        if (ClimbProximitySphere)
        {
            ClimbProximitySphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ADynamicCharacter::OnClimbProximityOverlap);
            ClimbProximitySphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ADynamicCharacter::OnClimbProximityEndOverlap);
        }

        if (DefaultMovementStateTag.IsValid())
        {
            ServerSetMovementState(DefaultMovementStateTag);
        }
        
        // Force active on spawn, will evaluate dormancy in Tick
        NetDormancy = DORM_Awake;
    }
}

void ADynamicCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    PreviousVelocity = GetVelocity();

    if (HasAuthority())
    {
        if (CapsuleComponent)
        {
            HistorySaveAccumulator += DeltaSeconds;
            if (HistorySaveAccumulator >= HistorySaveInterval)
            {
                HistorySaveAccumulator = 0.0f;
                
                const double CurrentTime = GetWorld()->GetTimeSeconds();
                FCharacterTransformHistorySnapshot& Snapshot = TransformHistoryBuffer[HistoryBufferWriteIndex];
                Snapshot.ServerTimestamp = CurrentTime;
                Snapshot.CapsuleLocation = CapsuleComponent->GetComponentLocation();
                Snapshot.CapsuleRotation = CapsuleComponent->GetComponentQuat();
                Snapshot.ActiveMovementState = ActiveMovementStateTag;
                
                if (APlayerController* PC = Cast<APlayerController>(GetController()))
                {
                    Snapshot.ViewPitch = PC->GetControlRotation().Pitch;
                }
                else
                {
                    Snapshot.ViewPitch = GetActorRotation().Pitch;
                }
                
                HistoryBufferWriteIndex = (HistoryBufferWriteIndex + 1) % HistoryRingBufferSize;
                HistoryBufferCount = FMath::Min(HistoryBufferCount + 1, HistoryRingBufferSize);
            }
        }
        
        UpdateNetworkDormancy(DeltaSeconds);
    }

    static const FGameplayTag CapabilityClimbTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Movement.Climb"), false);
    if (AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(CapabilityClimbTag))
    {
        static const FGameplayTag HangTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Hang"), false);
        static const FGameplayTag ClimbTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Climb"), false);
        
        if (ActiveMovementStateTag.MatchesTag(HangTag) || ActiveMovementStateTag.MatchesTag(ClimbTag))
        {
            if (HasAuthority() || IsLocallyControlled())
            {
                UpdateAutomatedClimbingState(DeltaSeconds);
            }
        }
    }
    if (CapsuleComponent && CachedTargetCapsuleHalfHeight > 0.0f)
    {
        const float CurrentHeight = CapsuleComponent->GetUnscaledCapsuleHalfHeight();
        if (!FMath::IsNearlyEqual(CurrentHeight, CachedTargetCapsuleHalfHeight, 0.5f))
        {
            const float NewHeight = FMath::FInterpTo(CurrentHeight, CachedTargetCapsuleHalfHeight, DeltaSeconds, 12.0f);
            CapsuleComponent->SetCapsuleHalfHeight(NewHeight, true);
        }
    }
    if (bIsRagdolling && HasAuthority())
    {
        UpdateRagdollHyperextension(DeltaSeconds);
    }
    ApplyEnvironmentalForces(DeltaSeconds);
    
    if (UWorld* World = GetWorld())
    {
        if (World->GetNetMode() != NM_DedicatedServer)
        {
            if (UDynamicAudioSubsystem* AudioSub = World->GetSubsystem<UDynamicAudioSubsystem>())
            {
                FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(ScrapeTrace), false, this);

                // 1. Evaluate Ragdoll Sliding
                if (bIsRagdolling && MeshComponent)
                {
                    const float SlideSpeed = MeshComponent->GetComponentVelocity().Size();
                    if (SlideSpeed > 15.0f)
                    {
                        FHitResult FloorHit;
                        if (World->LineTraceSingleByChannel(FloorHit, GetActorLocation(), GetActorLocation() - FVector(0, 0, 75.0f), ECC_Visibility, TraceParams))
                        {
                            // Pressure = Mass * Gravity
                            const float NormalPressure = MeshComponent->GetMass() * FMath::Abs(World->GetGravityZ() * 0.01f);
                            AudioSub->UpdateMaterialScrapeAudio(this, FloorHit.GetActor(), FloorHit.ImpactPoint, SlideSpeed, NormalPressure);
                        }
                    }
                }

                // 2. Evaluate Tethered / Dragged Objects (Carts, Dead Bodies)
                if (AActor* DraggedActor = CurrentTetheredActor.Get())
                {
                    if (UPrimitiveComponent* DraggedPrim = Cast<UPrimitiveComponent>(DraggedActor->GetRootComponent()))
                    {
                        const float DragSpeed = DraggedPrim->GetComponentVelocity().Size();
                        if (DragSpeed > 15.0f)
                        {
                            TraceParams.AddIgnoredActor(DraggedActor);
                            FHitResult FloorHit;
                            if (World->LineTraceSingleByChannel(FloorHit, DraggedActor->GetActorLocation(), DraggedActor->GetActorLocation() - FVector(0, 0, 100.0f), ECC_Visibility, TraceParams))
                            {
                                // Pressure = Mass * Gravity
                                const float NormalPressure = DraggedPrim->GetMass() * FMath::Abs(World->GetGravityZ() * 0.01f);
                                AudioSub->UpdateMaterialScrapeAudio(DraggedActor, FloorHit.GetActor(), FloorHit.ImpactPoint, DragSpeed, NormalPressure);
                            }
                        }
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Mass & Climbing Systems
// ----------------------------------------------------------------------------

float ADynamicCharacter::CalculateTotalInventoryAndEquipmentMass() const
{
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if ((Now - LastMassCalculateTime) < 0.5) return CachedTotalMass;

    FVector CoMOffset;
    EvaluatePhysicalEncumbrance(CachedTotalMass, CoMOffset);
    
    // Apply physical COM shift to the character mesh/capsule for procedural leaning
    if (MeshComponent)
    {
        MeshComponent->SetCenterOfMass(CoMOffset, NAME_None);
    }

    LastMassCalculateTime = Now;
    return CachedTotalMass;
}

float ADynamicCharacter::GetEffectiveClimbingLoad() const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    const float BaselineMass = GS ? FMath::Max(1.0f, GS->GetCalculationSettings().BaselineCharacterMassKg) : 80.0f;

    const float TotalMass = CalculateTotalInventoryAndEquipmentMass();
    return FMath::Max(0.5f, TotalMass / BaselineMass);
}

void ADynamicCharacter::OnClimbProximityOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (OtherActor && OtherActor != this)
    {
        if (UDynamicInteractableComponent* Interactable = OtherActor->FindComponentByClass<UDynamicInteractableComponent>())
        {
            NearbyClimbHolds.AddUnique(Interactable);
        }
    }
}

void ADynamicCharacter::OnClimbProximityEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (OtherActor)
    {
        if (UDynamicInteractableComponent* Interactable = OtherActor->FindComponentByClass<UDynamicInteractableComponent>())
        {
            NearbyClimbHolds.RemoveSingleSwap(Interactable);
        }
    }
}

void ADynamicCharacter::UpdateAutomatedClimbingState(float DeltaSeconds)
{
    if ((!HasAuthority() && !IsLocallyControlled()) || !CapsuleComponent || !AttributeSet || !PhysicsControlComponent) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return;

    const float EffectiveLoad = GetEffectiveClimbingLoad();
    AccumulatedHoldSweepTime += DeltaSeconds;
    const FVector BaseLoc = GetActorLocation();

    // OPTIMIZATION: Overlap proximity iteration instead of multi-channel PhysX sphere sweeps
    const bool bMovedEnough = (BaseLoc - LastHoldSweepLocation).SizeSquared() >= FMath::Square(10.0f);
    const bool bTimeElapsed = (AccumulatedHoldSweepTime >= 0.15f);

    if (bMovedEnough || bTimeElapsed)
    {
        LastHoldSweepLocation = BaseLoc;
        AccumulatedHoldSweepTime = 0.0f;

        // Clean up any destroyed objects lazily
        NearbyClimbHolds.RemoveAll([](const TWeakObjectPtr<UDynamicInteractableComponent>& Ptr) { return !Ptr.IsValid(); });

        auto CanLimbAcquireHold = [&](FName BoneName) -> bool
        {
            const uint8 LimbId = GS->GetLimbIdForBone(BoneName);
            if (const FPhysicsLimbConfig* LimbData = GS->GetLimbData(LimbId))
            {
                if (LimbData->EnduranceAttributeTag.IsValid())
                {
                    return AttributeSet->GetDynamicAttributeValue(LimbData->EnduranceAttributeTag, LimbId) > 0.0f;
                }
            }
            return true;
        };

        auto FindBestHoldFromProximity = [&](const FVector& SearchOrigin, const FName& TargetTagStr) -> FTransform
        {
            FTransform BestTransform = FTransform::Identity;
            float BestDistSq = MAX_flt;
            FGameplayTag HoldTag = FGameplayTag::RequestGameplayTag(TargetTagStr, false);

            for (const TWeakObjectPtr<UDynamicInteractableComponent>& WeakInteractable : NearbyClimbHolds)
            {
                if (UDynamicInteractableComponent* Interactable = WeakInteractable.Get())
                {
                    FTransform HoldTransform;
                    USceneComponent* DummyComp = nullptr;
                    
                    if (HoldTag.IsValid() && Interactable->ResolveKinematicTargetTransform(HoldTag, HoldTransform, DummyComp))
                    {
                        const float DistSq = FVector::DistSquared(SearchOrigin, HoldTransform.GetLocation());
                        if (DistSq < BestDistSq)
                        {
                            BestDistSq = DistSq;
                            BestTransform = HoldTransform;
                        }
                    }
                }
            }
            return BestTransform;
        };

        if (CanLimbAcquireHold(FName("hand_r")))
        {
            FTransform RHandHold = FindBestHoldFromProximity(BaseLoc + FVector(30.0f, 30.0f, 50.0f), FName("KinematicTarget.Handhold.Right"));
            if (!RHandHold.Equals(FTransform::Identity)) ActiveLimbHolds.Add(FName("hand_r"), RHandHold);
        }
        if (CanLimbAcquireHold(FName("hand_l")))
        {
            FTransform LHandHold = FindBestHoldFromProximity(BaseLoc + FVector(30.0f, -30.0f, 50.0f), FName("KinematicTarget.Handhold.Left"));
            if (!LHandHold.Equals(FTransform::Identity)) ActiveLimbHolds.Add(FName("hand_l"), LHandHold);
        }
        if (CanLimbAcquireHold(FName("foot_r")))
        {
            FTransform RFootHold = FindBestHoldFromProximity(BaseLoc + FVector(30.0f, 20.0f, -50.0f), FName("KinematicTarget.Foothold.Right"));
            if (!RFootHold.Equals(FTransform::Identity)) ActiveLimbHolds.Add(FName("foot_r"), RFootHold);
        }
        if (CanLimbAcquireHold(FName("foot_l")))
        {
            FTransform LFootHold = FindBestHoldFromProximity(BaseLoc + FVector(30.0f, -20.0f, -50.0f), FName("KinematicTarget.Foothold.Left"));
            if (!LFootHold.Equals(FTransform::Identity)) ActiveLimbHolds.Add(FName("foot_l"), LFootHold);
        }

        const bool bHasAnyFooting = ActiveLimbHolds.Contains(FName("foot_r")) || ActiveLimbHolds.Contains(FName("foot_l"));
        if (bHasAnyFooting != bCurrentFootingActive)
        {
            bCurrentFootingActive = bHasAnyFooting;
            ConfigureClimbingPhysics(true, bCurrentFootingActive);
        }
    }

    // 2. Data-Driven Limb Fatigue (Distributed endurance drain across active holds)
    TArray<FName> FailedLimbHolds;
    const float BaseDrainRate = bCurrentFootingActive ? 2.5f : 10.0f; // Footing reduces strain on hands

    for (const auto& HoldPair : ActiveLimbHolds)
    {
        const uint8 LimbId = GS->GetLimbIdForBone(HoldPair.Key);
        if (const FPhysicsLimbConfig* LimbData = GS->GetLimbData(LimbId))
        {
            if (LimbData->EnduranceAttributeTag.IsValid())
            {
                float CurEndurance = AttributeSet->GetDynamicAttributeValue(LimbData->EnduranceAttributeTag, LimbId);
                const float DistributedDrain = (BaseDrainRate * EffectiveLoad * DeltaSeconds) / FMath::Max(1, ActiveLimbHolds.Num());
                CurEndurance -= DistributedDrain;
                AttributeSet->SetDynamicAttributeValue(LimbData->EnduranceAttributeTag, FMath::Max(0.0f, CurEndurance), LimbId);

                if (CurEndurance <= 0.0f)
                {
                    FailedLimbHolds.Add(HoldPair.Key);
                }
            }
        }
    }

    // 3. Process Localized Detachments
    for (const FName& FailedBone : FailedLimbHolds)
    {
        ActiveLimbHolds.Remove(FailedBone);

        const FName ControlName = FName(*FString::Printf(TEXT("Control_%s"), *FailedBone.ToString()));
        FPhysicsControlMultiplier DanglingMult;
        DanglingMult.LinearStrengthMultiplier = FVector(0.0f);
        DanglingMult.AngularStrengthMultiplier = 0.05f;
        DanglingMult.LinearExtraDampingMultiplier = FVector(0.2f);
        DanglingMult.AngularExtraDampingMultiplier = 0.2f;
        PhysicsControlComponent->SetControlMultiplier(ControlName, DanglingMult);
    }

    // 4. Evaluate Total Failure (Both hands lost)
    const bool bHasRightHand = ActiveLimbHolds.Contains(FName("hand_r"));
    const bool bHasLeftHand = ActiveLimbHolds.Contains(FName("hand_l"));

    if (!bHasRightHand && !bHasLeftHand)
    {
        if (HasAuthority())
        {
            StartRagdoll();
        }
        return;
    }

    // 5. Update Surviving Limb Targets to Physics Control Component
    for (const auto& Pair : ActiveLimbHolds)
    {
        const FName ControlName = FName(*FString::Printf(TEXT("Control_%s"), *Pair.Key.ToString()));
        PhysicsControlComponent->SetControlTargetPositionAndOrientation(
            ControlName,
            Pair.Value.GetLocation(),
            Pair.Value.GetRotation().Rotator(),
            100.0f,
            true
        );
    }
}

void ADynamicCharacter::ConfigureClimbingPhysics(bool bEnableClimbing, bool bHasFooting)
{
    if (bIsClimbingPhysical == bEnableClimbing && bCurrentFootingActive == bHasFooting) return;
    bIsClimbingPhysical = bEnableClimbing;
    bCurrentFootingActive = bHasFooting;

    if (!MeshComponent || !PhysicsControlComponent) return;

    if (bEnableClimbing)
    {
        MeshComponent->SetSimulatePhysics(true);

        // Hands firmly anchor to hold targets
        static const TArray<FName> UpperBodyControls = { FName("Control_hand_r"), FName("Control_hand_l") };
        for (const FName& Control : UpperBodyControls)
        {
            FPhysicsControlMultiplier Mult;
            Mult.LinearStrengthMultiplier = FVector(5.0f);
            Mult.AngularStrengthMultiplier = 5.0f;
            Mult.LinearExtraDampingMultiplier = FVector(1.0f);
            Mult.AngularExtraDampingMultiplier = 1.0f;
            PhysicsControlComponent->SetControlMultiplier(Control, Mult);
        }

        // Spine and pelvis allow natural mass settling and swaying
        static const TArray<FName> CoreControls = { FName("Control_pelvis"), FName("Control_spine_01") };
        for (const FName& Control : CoreControls)
        {
            FPhysicsControlMultiplier SlackMult;
            SlackMult.LinearStrengthMultiplier = bHasFooting ? FVector(0.8f) : FVector(0.0f);
            SlackMult.AngularStrengthMultiplier = bHasFooting ? 0.8f : 0.05f;
            SlackMult.LinearExtraDampingMultiplier = FVector(0.4f);
            SlackMult.AngularExtraDampingMultiplier = 0.4f;
            PhysicsControlComponent->SetControlMultiplier(Control, SlackMult);
        }

        // Feet: Planted rigidly if footing exists, otherwise physically dangle under gravity
        static const TArray<FName> FootControls = { FName("Control_foot_r"), FName("Control_foot_l"), FName("Control_calf_r"), FName("Control_calf_l") };
        for (const FName& Control : FootControls)
        {
            FPhysicsControlMultiplier FootMult;
            if (bHasFooting) 
            {
                FootMult.LinearStrengthMultiplier = FVector(3.0f);
                FootMult.AngularStrengthMultiplier = 3.0f;
            } 
            else 
            {
                FootMult.LinearStrengthMultiplier = FVector(0.0f);
                FootMult.AngularStrengthMultiplier = 0.04f;
            }
            FootMult.LinearExtraDampingMultiplier = FVector(0.2f);
            FootMult.AngularExtraDampingMultiplier = 0.2f;
            PhysicsControlComponent->SetControlMultiplier(Control, FootMult);
        }
    }
    else
    {
        ActiveLimbHolds.Empty();
        RecalculateLimbPhysicsMotors(0, true);

        if (!bIsRagdolling)
        {
            MeshComponent->SetSimulatePhysics(false);
            MeshComponent->AttachToComponent(CapsuleComponent, FAttachmentTransformRules::KeepRelativeTransform);
            MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f), FRotator(0.0f, -90.0f, 0.0f));
            MeshComponent->SetCollisionProfileName(TEXT("CharacterMesh"));
        }
    }
}

void ADynamicCharacter::HandleMovementModePhysicsStateChanged(const FGameplayTag& OldStateTag, const FGameplayTag& NewStateTag)
{
    static const FGameplayTag FallModeTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Fall"), false);
    static const FGameplayTag HangTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Hang"), false);
    static const FGameplayTag ClimbTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Climb"), false);

    if (NewStateTag.MatchesTag(HangTag))
    {
        ConfigureClimbingPhysics(true, false);
    }
    else if (NewStateTag.MatchesTag(ClimbTag))
    {
        ConfigureClimbingPhysics(true, true);
    }
    else if (NewStateTag.MatchesTag(FallModeTag))
    {
        // 1. DIRECTIONAL FALLING ANIMATIONS
        // If the character enters falling state with high lateral velocity, trigger a flailing/brace animation
        const FVector Velocity = GetVelocity();
        if (Velocity.SizeSquared2D() > FMath::Square(300.0f))
        {
            const float ForwardDot = FVector::DotProduct(GetActorForwardVector(), Velocity.GetSafeNormal2D());
            FGameplayTag FallAnimTag;
            
            if (ForwardDot > 0.5f) FallAnimTag = FGameplayTag::RequestGameplayTag(TEXT("Anim.Falling.ForwardBrace"), false);
            else if (ForwardDot < -0.5f) FallAnimTag = FGameplayTag::RequestGameplayTag(TEXT("Anim.Falling.BackwardFlail"), false);
            else FallAnimTag = FGameplayTag::RequestGameplayTag(TEXT("Anim.Falling.Tumble"), false);

            if (AbilitySystemComponent && FallAnimTag.IsValid())
            {
                // Let the AnimBP pick up the tag and transition to the brace/tumble sequence
                AbilitySystemComponent->AddLooseGameplayTags(FGameplayTagContainer(FallAnimTag));
            }
        }
    }
    else
    {
        if (OldStateTag.MatchesTag(HangTag) || OldStateTag.MatchesTag(ClimbTag))
        {
            ConfigureClimbingPhysics(false, false);
        }
        
        // Remove fall animation tags upon landing
        if (AbilitySystemComponent)
        {
            AbilitySystemComponent->RemoveLooseGameplayTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Anim.Falling"), false)));
        }
    }
}

void ADynamicCharacter::OnServerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !OtherActor || OtherActor == this) return;

    const float ImpactSpeed = PreviousVelocity.Size();
    if (ImpactSpeed <= 100.0f) return;

    // Detect steep wall/cliff surfaces vs. hard ground landings
    if (FMath::Abs(Hit.ImpactNormal.Z) < 0.35f)
    {
        static const FGameplayTag WallSlamTag = FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.WallSlam"), false);
        ProcessDynamicImpact(WallSlamTag, Hit, ImpactSpeed);
    }
    else if (Hit.ImpactNormal.Z >= 0.35f && PreviousVelocity.Z < -500.0f)
    {
        // High downward velocity on a floor surface
        static const FGameplayTag HardLandingTag = FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.FallLanding"), false);
        ProcessDynamicImpact(HardLandingTag, Hit, FMath::Abs(PreviousVelocity.Z));
    }
}

void ADynamicCharacter::ProcessDynamicImpact(const FGameplayTag& ImpactTypeTag, const FHitResult& Hit, float ImpactVelocity)
{
    if (!HasAuthority() || !AbilitySystemComponent || !ImpactTypeTag.IsValid()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !GS->GenericInstantEffect) return;

    const FPhysicsCalculationSettings& Settings = GS->GetCalculationSettings();
    const FDynamicImpactReactionRule* Rule = Settings.ImpactReactionRules.Find(ImpactTypeTag);
    if (!Rule) return;

    FName HitBoneName = Hit.MyBoneName;
    if (HitBoneName == NAME_None && MeshComponent)
    {
        FHitResult MeshHit;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ImpactBoneTrace), true, this);
        const FVector Start = Hit.ImpactPoint + (Hit.ImpactNormal * 5.0f);
        const FVector End = Hit.ImpactPoint - (Hit.ImpactNormal * 15.0f);

        if (MeshComponent->LineTraceComponent(MeshHit, Start, End, QueryParams))
        {
            HitBoneName = MeshHit.BoneName;
        }
        else
        {
            HitBoneName = MeshComponent->FindClosestBone(Hit.ImpactPoint);
        }
    }

    if (HitBoneName == NAME_None) HitBoneName = FName("pelvis");
    const uint8 ResolvedLimbId = GS->GetLimbIdForBone(HitBoneName);

    const float BaselineMass = FMath::Max(1.0f, Settings.BaselineCharacterMassKg);
    const float TotalMass = CalculateTotalInventoryAndEquipmentMass();
    const float MassFactor = 1.0f + ((TotalMass - BaselineMass) * Rule->MassImpactMultiplier);

    FDynamicGameplayEffectContext* CustomContext = new FDynamicGameplayEffectContext();
    CustomContext->ContextPayloadTags.AddTag(ImpactTypeTag);
    CustomContext->AddInstigator(this, this);
    CustomContext->SetAuthoritativePhysicsPayload(ResolvedLimbId, ImpactVelocity, MassFactor, Hit.ImpactNormal, Hit.ImpactPoint);

    FGameplayEffectContextHandle ContextHandle(CustomContext);
    FGameplayEffectSpec ImpactSpec(GS->GenericInstantEffect, ContextHandle, 1.0f);

    AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(ImpactSpec);
}

void ADynamicCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitializeAbilitySystem();

    if (NewController->IsLocalPlayerController())
    {
        // Hydrate default camera profile based on the character template
        if (UDynamicCustomizationComponent* CustComp = GetCustomizationComponent())
        {
            if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
            {
                FCharacterTemplateDef Def;
                if (GS->GetCharacterTemplate(CustComp->ActiveCharacterTemplateTag, Def) && Def.DefaultCameraProfileTag.IsValid())
                {
                    ApplyCameraProfile(Def.DefaultCameraProfileTag);
                }
                else
                {
                    ApplyCameraProfile(FGameplayTag::RequestGameplayTag(TEXT("Camera.Profile.Humanoid.Default"), false));
                }
            }
        }
    }
}

void ADynamicCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitializeAbilitySystem();
}

void ADynamicCharacter::InitializeAbilitySystem()
{
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->InitAbilityActorInfo(this, this);
    }

    if (!AttributeSet) return;

    if (HasAuthority() && !bAttributesInitializedOnServer)
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            for (const auto& AttrPair : GS->AttributeMasterRegistry)
            {
                AttributeSet->SetDynamicAttributeValue(AttrPair.Key, AttrPair.Value.DefaultValue, 0);
            }
            AttributeSet->InitializeLimbAttributesFromConfig(GS->GetLimbLookupMap());
            bAttributesInitializedOnServer = true;
        }
    }
    else
    {
        AttributeSet->OnRep_FastAttributes();
    }
}

void ADynamicCharacter::StartRagdoll()
{
    if (bIsRagdolling) return;
    bIsRagdolling = true;

    if (MoverComponent) MoverComponent->Deactivate();
    if (MeshComponent)
    {
        MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
        MeshComponent->SetSimulatePhysics(true);
        ConfigureRagdollJoints();
    }
}

void ADynamicCharacter::RecoverFromRagdoll()
{
    if (!bIsRagdolling) return;
    bIsRagdolling = false;
    HyperextendedJoints.Empty();

    if (MeshComponent)
    {
        MeshComponent->SetSimulatePhysics(false);
        MeshComponent->AttachToComponent(CapsuleComponent, FAttachmentTransformRules::KeepRelativeTransform);
        MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f), FRotator(0.0f, -90.0f, 0.0f));
        MeshComponent->SetCollisionProfileName(TEXT("CharacterMesh"));
    }
    if (MoverComponent) MoverComponent->Activate(true);
}

bool ADynamicCharacter::ServerSetMovementState(FGameplayTag NewStateTag)
{
    if (!HasAuthority() || !NewStateTag.IsValid() || NewStateTag == ActiveMovementStateTag) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicMovementStateDef* Def = GS->MovementStateRegistry.Find(NewStateTag);
    if (!Def) return false;

    if (AbilitySystemComponent && Def->BlockedByTags.Num() > 0)
    {
        FGameplayTagContainer OwnedTags;
        AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);
        if (OwnedTags.HasAny(Def->BlockedByTags))
        {
            return false;
        }
    }

    const FGameplayTag OldState = ActiveMovementStateTag;
    ActiveMovementStateTag = NewStateTag;
    
    ApplyMovementStateConfig(*Def);
    HandleMovementModePhysicsStateChanged(OldState, ActiveMovementStateTag);
    
    return true;
}

void ADynamicCharacter::OnRep_ActiveMovementState()
{
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const FDynamicMovementStateDef* Def = GS->MovementStateRegistry.Find(ActiveMovementStateTag))
        {
            ApplyMovementStateConfig(*Def);
        }
    }
}

void ADynamicCharacter::ApplyMovementStateConfig(const FDynamicMovementStateDef& Def)
{
    if (MoverComponent)
    {
        MoverComponent->QueueNextMode(Def.MoverModeName);
    }

    auto EvaluateMovementStat = [this](const FMovementStatConfig& StatConfig) -> float
    {
        float Multiplier = 1.0f;
        if (AttributeSet)
        {
            for (const auto& Pair : StatConfig.AttributeScalars.LocalLookupMap)
            {
                if (Pair.Key.IsValid())
                {
                    Multiplier += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                }
            }
        }
        return StatConfig.BaseValue * FMath::Max(0.01f, Multiplier);
    };

    // 1. DYNAMIC STANCE SCALING (Capsule Height sourced from Character Templates)
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS && CustomizationComponent)
    {
        FCharacterTemplateDef TemplateDef;
        if (GS->GetCharacterTemplate(CustomizationComponent->ActiveCharacterTemplateTag, TemplateDef))
        {
            auto EvaluateStanceMod = [&](const FDynamicStanceConfig& StanceConfig) -> float
            {
                float ModAccum = 0.0f;
                if (AttributeSet)
                {
                    for (const auto& Pair : StanceConfig.AttributeModifiers.LocalLookupMap)
                    {
                        ModAccum += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                    }
                }
                return StanceConfig.BaseHeight + ModAccum;
            };

            const float StandHeight = EvaluateStanceMod(TemplateDef.StanceStandConfig);
            const float CrouchHeight = EvaluateStanceMod(TemplateDef.StanceCrouchConfig);
            const float ProneHeight = EvaluateStanceMod(TemplateDef.StanceProneConfig);

            if (TargetStanceScale > 0.5f)
            {
                // Lerp Crouch -> Stand
                const float Alpha = (TargetStanceScale - 0.5f) * 2.0f;
                CachedTargetCapsuleHalfHeight = FMath::Lerp(CrouchHeight, StandHeight, Alpha);
            }
            else
            {
                // Lerp Prone -> Crouch
                const float Alpha = TargetStanceScale * 2.0f;
                CachedTargetCapsuleHalfHeight = FMath::Lerp(ProneHeight, CrouchHeight, Alpha);
            }
        }
    }
    
    // Set TargetSpeedPace inside the ASC to multiply final max velocities downstream inside UDynamicPhysicsMode
    if (HasAuthority() && AttributeSet)
    {
        static const FGameplayTag PaceTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.Pace"), false);
        AttributeSet->SetDynamicAttributeValue(PaceTag, TargetPaceScale, 0);
    }

    if (HasAuthority() && AbilitySystemComponent)
    {
        if (GrantedMovementTags.Num() > 0)
        {
            AbilitySystemComponent->RemoveLooseGameplayTags(GrantedMovementTags);
        }
        GrantedMovementTags = Def.GrantsTags;

        // Automatically assign logical tags based on current physical stance scale
        if (TargetStanceScale < 0.25f)
        {
            GrantedMovementTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Prone"), false));
        }
        else if (TargetStanceScale <= 0.75f)
        {
            GrantedMovementTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Crouch"), false));
        }

        if (GrantedMovementTags.Num() > 0)
        {
            AbilitySystemComponent->AddLooseGameplayTags(GrantedMovementTags);
        }
    }
}

float ADynamicCharacter::CalculateAuthoritativeStrikeForce() const
{
    float Force = 1000.0f;
    if (AttributeSet)
    {
        static const FGameplayTag StrengthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stat.Strength"), false);
        if (StrengthTag.IsValid())
        {
            Force += AttributeSet->GetDynamicAttributeValue(StrengthTag, 0) * 20.0f;
        }
    }

    if (const UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
    {
        static const FGameplayTag MainHandSlotTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
        const FGameplayTag WeaponTag = EquipComp->GetEquippedItemInSlot(MainHandSlotTag);
        if (WeaponTag.IsValid())
        {
            if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
            {
                if (const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(WeaponTag))
                {
                    Force += FMath::Max(1.0f, Def->MassOverride) * 150.0f;
                }
            }
        }
    }
    return FMath::Clamp(Force, 100.0f, 15000.0f);
}

void ADynamicCharacter::RewindToHistoricalSnapshot(double TargetTimestamp)
{
    if (HistoryBufferCount == 0 || bIsCurrentlyRewound) return;

    PreRewindCachedTransform.CapsuleLocation = CapsuleComponent->GetComponentLocation();
    PreRewindCachedTransform.CapsuleRotation = CapsuleComponent->GetComponentQuat();
    bIsCurrentlyRewound = true;

    if (MeshComponent)
    {
        bPreRewindSimulatingPhysics = MeshComponent->IsSimulatingPhysics();
        if (bPreRewindSimulatingPhysics)
        {
            MeshComponent->SetSimulatePhysics(false);
        }
    }

    if (MoverComponent)
    {
        bPreRewindMoverActive = MoverComponent->IsActive();
        if (bPreRewindMoverActive)
        {
            MoverComponent->Deactivate();
        }
    }

    if (PhysicsControlComponent)
    {
        PhysicsControlComponent->SetComponentTickEnabled(false);
    }

    int32 ClosestIdx = INDEX_NONE;
    double SmallestDiff = MAX_dbl;
    for (int32 i = 0; i < HistoryBufferCount; ++i)
    {
        const FCharacterTransformHistorySnapshot& Candidate = TransformHistoryBuffer[i];
        if (Candidate.ServerTimestamp <= 0.0) continue;
        const double Diff = FMath::Abs(Candidate.ServerTimestamp - TargetTimestamp);
        if (Diff < SmallestDiff)
        {
            SmallestDiff = Diff;
            ClosestIdx = i;
        }
    }

    if (ClosestIdx != INDEX_NONE)
    {
        const FCharacterTransformHistorySnapshot& Snapshot = TransformHistoryBuffer[ClosestIdx];
        CapsuleComponent->SetWorldLocationAndRotation(Snapshot.CapsuleLocation, Snapshot.CapsuleRotation, false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void ADynamicCharacter::RestoreFromHistoricalSnapshot()
{
    if (!bIsCurrentlyRewound) return;

    CapsuleComponent->SetWorldLocationAndRotation(PreRewindCachedTransform.CapsuleLocation, PreRewindCachedTransform.CapsuleRotation, false, nullptr, ETeleportType::TeleportPhysics);

    if (MoverComponent && bPreRewindMoverActive)
    {
        MoverComponent->Activate(true);
    }

    if (MeshComponent && bPreRewindSimulatingPhysics)
    {
        MeshComponent->SetSimulatePhysics(true);
    }

    if (PhysicsControlComponent)
    {
        PhysicsControlComponent->SetComponentTickEnabled(true);
        RecalculateLimbPhysicsMotors(0, true);
    }

    bIsCurrentlyRewound = false;
}

bool ADynamicCharacter::Server_RequestAimedStrike_Validate(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, double ClientTargetTimestamp)
{
    const float OriginDistSq = FVector::DistSquared(FVector(AimOrigin), GetActorLocation());
    return (OriginDistSq <= FMath::Square(500.0f)) && !AimDirection.IsZero();
}

void ADynamicCharacter::Server_RequestAimedStrike_Implementation(const FVector_NetQuantize10& AimOrigin, const FVector_NetQuantizeNormal& AimDirection, double ClientTargetTimestamp)
{
    if (!HasAuthority()) return;

    const double CurrentServerTime = GetWorld()->GetTimeSeconds();
    const double ClampedClientTimestamp = FMath::Clamp(ClientTargetTimestamp, CurrentServerTime - MaxHistorySeconds, CurrentServerTime);

    const FVector Start = FVector(AimOrigin);
    const FVector End = Start + (FVector(AimDirection) * 10000.0f);
    const FVector BoxCenter = (Start + End) * 0.5f;
    const FVector BoxExtent = FVector(500.0f, 500.0f, (End - Start).Size() * 0.5f);

    TArray<FOverlapResult> CandidateOverlaps;
    FCollisionShape QueryBox = FCollisionShape::MakeBox(BoxExtent);
    FCollisionQueryParams BroadphaseParams(SCENE_QUERY_STAT(LagCompensationBroadphase), false, this);

    GetWorld()->OverlapMultiByChannel(CandidateOverlaps, BoxCenter, FQuat::FindBetweenVectors(FVector::UpVector, (End - Start).GetSafeNormal()), ECC_Pawn, QueryBox, BroadphaseParams);

    bool bHit = false;
    ADynamicCharacter* HitCharacter = nullptr;
    FName HitBone = NAME_None;

    for (const FOverlapResult& Overlap : CandidateOverlaps)
    {
        if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(Overlap.GetActor()))
        {
            if (TargetChar != this)
            {
                int32 ClosestIdx = INDEX_NONE;
                double SmallestDiff = MAX_dbl;
                for (int32 i = 0; i < TargetChar->HistoryBufferCount; ++i)
                {
                    const FCharacterTransformHistorySnapshot& Candidate = TargetChar->TransformHistoryBuffer[i];
                    if (Candidate.ServerTimestamp <= 0.0) continue;
                    const double Diff = FMath::Abs(Candidate.ServerTimestamp - ClampedClientTimestamp);
                    if (Diff < SmallestDiff)
                    {
                        SmallestDiff = Diff;
                        ClosestIdx = i;
                    }
                }

                if (ClosestIdx != INDEX_NONE)
                {
                    if (CalculatePseudoBoneIntersection(TargetChar->TransformHistoryBuffer[ClosestIdx], Start, End, HitBone))
                    {
                        bHit = true;
                        HitCharacter = TargetChar;
                        break;
                    }
                }
            }
        }
    }

    if (bHit && HitCharacter)
    {
        const float AuthoritativeForce = CalculateAuthoritativeStrikeForce();

        // 1. Gather combined impact tags from the active ability and the weapon
        FGameplayTagContainer CombinedImpactTags;
        if (const UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
        {
            static const FGameplayTag MainHandSlotTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
            const FGameplayTag WeaponTag = EquipComp->GetEquippedItemInSlot(MainHandSlotTag);
            if (WeaponTag.IsValid())
            {
                if (ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
                {
                    if (const FDynamicPhysicsActorDef* Def = DynamicGS->MasterPhysicsActorRegistry.Find(WeaponTag))
                    {
                        CombinedImpactTags.AppendTags(Def->ImpactTypeTags);
                    }
                }
            }
        }

        if (CombinedImpactTags.IsEmpty())
        {
            CombinedImpactTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.Melee.Unarmed"), false));
        }

        ServerProcessAuthoritativeLimbHit(
            HitCharacter, 
            HitBone, 
            AuthoritativeForce, 
            1.0f, 
            HitCharacter->GetActorLocation(), 
            FVector(AimDirection) * -1.0f, 
            this, 
            CombinedImpactTags
        );
    }
}

void ADynamicCharacter::OnServerSkeletalMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || OtherActor == this) return;

    const float ImpulseMagnitude = NormalImpulse.Size();
    if (ImpulseMagnitude <= KINDA_SMALL_NUMBER) return;

    float MassRatio = 1.0f;
    if (OtherComp && OtherComp->IsSimulatingPhysics() && HitComponent && HitComponent->IsSimulatingPhysics())
    {
        const float SelfMass = FMath::Max(1.0f, HitComponent->GetMass());
        const float OtherMass = FMath::Max(1.0f, OtherComp->GetMass());
        MassRatio = FMath::Clamp(OtherMass / SelfMass, 0.25f, 4.0f);
    }

    FGameplayTagContainer CollisionImpactTags;
    if (ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(OtherActor))
        {
            if (const FDynamicPhysicsActorDef* Def = DynamicGS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag()))
            {
                CollisionImpactTags.AppendTags(Def->ImpactTypeTags);
            }
        }
        else if (const ADynamicCharacter* OtherChar = Cast<ADynamicCharacter>(OtherActor))
        {
            if (const UDynamicEquipmentComponent* EquipComp = OtherChar->FindComponentByClass<UDynamicEquipmentComponent>())
            {
                static const FGameplayTag MainHandSlotTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
                const FGameplayTag WeaponTag = EquipComp->GetEquippedItemInSlot(MainHandSlotTag);
                if (const FDynamicPhysicsActorDef* WeaponDef = DynamicGS->MasterPhysicsActorRegistry.Find(WeaponTag))
                {
                    CollisionImpactTags.AppendTags(WeaponDef->ImpactTypeTags);
                }
            }
        }
    }

    if (CollisionImpactTags.IsEmpty())
    {
        CollisionImpactTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.Physical.Blunt"), false));
    }

    const FName AuthoritativeBoneName = (Hit.MyBoneName != NAME_None) ? Hit.MyBoneName : Hit.BoneName;
    ServerProcessAuthoritativeLimbHit(
        this, 
        AuthoritativeBoneName, 
        ImpulseMagnitude, 
        MassRatio, 
        Hit.ImpactPoint, 
        Hit.ImpactNormal, 
        OtherActor, 
        CollisionImpactTags
    );
}

void ADynamicCharacter::ServerProcessAuthoritativeLimbHit(
    ADynamicCharacter* TargetCharacter, 
    const FName& HitBoneName, 
    float RawImpulseMagnitude, 
    float MassRatio, 
    const FVector& HitLocation, 
    const FVector& HitNormal, 
    AActor* InstigatorActor,
    const FGameplayTagContainer& ImpactTypeTags)
{
    if (!HasAuthority() || !TargetCharacter) return;

    ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!DynamicGS) return;

    const FPhysicsCalculationSettings& Settings = DynamicGS->GetCalculationSettings();
    if (RawImpulseMagnitude < Settings.MinImpulseThreshold) return;

    const uint8 ResolvedLimbId = DynamicGS->GetLimbIdForBone(HitBoneName);
    const double CurrentTime = GetWorld()->GetTimeSeconds();

    if (const double* LastHitTime = TargetCharacter->ServerLastLimbHitTimestamp.Find(ResolvedLimbId))
    {
        if ((CurrentTime - *LastHitTime) < Settings.CooldownBetweenHitsPerLimb) return;
    }
    TargetCharacter->ServerLastLimbHitTimestamp.Add(ResolvedLimbId, CurrentTime);

    FGameplayTagContainer EffectiveImpactTags = ImpactTypeTags;
    if (InstigatorActor && EffectiveImpactTags.IsEmpty())
    {
        if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(InstigatorActor))
        {
            if (const FDynamicPhysicsActorDef* Def = DynamicGS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag()))
            {
                EffectiveImpactTags.AppendTags(Def->ImpactTypeTags);
            }
        }
        else if (const ADynamicCharacter* AttackerChar = Cast<ADynamicCharacter>(InstigatorActor))
        {
            if (const UDynamicEquipmentComponent* EquipComp = AttackerChar->FindComponentByClass<UDynamicEquipmentComponent>())
            {
                static const FGameplayTag MainHandSlotTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
                const FGameplayTag WeaponTag = EquipComp->GetEquippedItemInSlot(MainHandSlotTag);
                if (const FDynamicPhysicsActorDef* Def = DynamicGS->MasterPhysicsActorRegistry.Find(WeaponTag))
                {
                    EffectiveImpactTags.AppendTags(Def->ImpactTypeTags);
                }
            }
        }
    }

    if (EffectiveImpactTags.IsEmpty())
    {
        EffectiveImpactTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.Physical.Blunt"), false));
    }

    const float WeaponMomentum = RawImpulseMagnitude * MassRatio;
    const float TargetBaseMassResistance = TargetCharacter->CalculateTotalInventoryAndEquipmentMass() * 5.0f;
    
    UDynamicAttributeSet* TargetAttr = TargetCharacter->GetDynamicAttributeSet();
    UAbilitySystemComponent* TargetASC = TargetCharacter->GetAbilitySystemComponent();

    auto GatherLocalResistance = [&](const FDynamicTagFloatMapWrapper& Scalars) -> float 
    {
        float AccumulatedResistance = 0.0f;
        
        if (TargetAttr)
        {
            for (const auto& Pair : Scalars.LocalLookupMap)
            {
                if (Pair.Key.IsValid())
                {
                    AccumulatedResistance += TargetAttr->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                    if (ResolvedLimbId > 0)
                    {
                        AccumulatedResistance += TargetAttr->GetDynamicAttributeValue(Pair.Key, ResolvedLimbId) * Pair.Value;
                    }
                }
            }
        }
        
        if (const UDynamicEquipmentComponent* TargetEquip = TargetCharacter->FindComponentByClass<UDynamicEquipmentComponent>())
        {
            for (const FDynamicEquipmentSlotItem& SlotItem : TargetEquip->ReplicatedEquipmentSlots.Items)
            {
                if (SlotItem.LinkedLimbId == ResolvedLimbId && SlotItem.EquippedItemArchetypeTag.IsValid())
                {
                    if (const FDynamicPhysicsActorDef* ArmorDef = DynamicGS->MasterPhysicsActorRegistry.Find(SlotItem.EquippedItemArchetypeTag))
                    {
                        for (const auto& Pair : Scalars.LocalLookupMap)
                        {
                            if (const float* ArmorAttrVal = ArmorDef->DefaultGlobalAttributes.LocalLookupMap.Find(Pair.Key))
                            {
                                AccumulatedResistance += (*ArmorAttrVal) * Pair.Value;
                            }
                        }
                    }
                }
            }
        }
        return AccumulatedResistance;
    };

    float HighestTargetImpulse = 0.0f;
    float HighestRecoilImpulse = 0.0f;

    for (const FGameplayTag& ImpactTag : EffectiveImpactTags)
    {
        if (const FDynamicImpactReactionRule* ImpactRule = Settings.ImpactReactionRules.Find(ImpactTag))
        {
            const float CurrentDefenderResistance = TargetBaseMassResistance + GatherLocalResistance(ImpactRule->StaggerResistanceScalars);
            const float SafeTotalPower = FMath::Max(1.0f, WeaponMomentum + CurrentDefenderResistance);
            const float TargetPowerRatio = FMath::Clamp(WeaponMomentum / SafeTotalPower, 0.0f, 1.0f);
            
            float RuleTargetImpulse = WeaponMomentum * TargetPowerRatio;
            float RuleRecoilImpulse = (WeaponMomentum * (1.0f - TargetPowerRatio)) * ImpactRule->BaseRecoilMultiplier;

            for (const FDynamicOverpowerRule& OverpowerRule : ImpactRule->OverpowerRules)
            {
                const float OverpowerResistance = OverpowerRule.BasePenetrationThreshold + GatherLocalResistance(OverpowerRule.ResistanceAttributeScalars);

                if (WeaponMomentum > OverpowerResistance)
                {
                    UPrimitiveComponent* AttackerPrim = nullptr;
                    if (ADynamicCharacter* Attacker = Cast<ADynamicCharacter>(InstigatorActor))
                    {
                        AttackerPrim = Attacker->GetMeshComponent();
                    }
                    else if (ADynamicPhysicsActor* PhysWeapon = Cast<ADynamicPhysicsActor>(InstigatorActor))
                    {
                        AttackerPrim = PhysWeapon->GetActivePhysicsPrimitive();
                    }

                    if (OverpowerRule.bIsPiercing && OverpowerRule.MaxPiercingDepth > 0.0f)
                    {
                        RuleRecoilImpulse *= 0.25f;
                        RuleTargetImpulse = WeaponMomentum;

                        if (USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMeshComponent())
                        {
                            if (AttackerPrim)
                            {
                                TargetMesh->IgnoreComponentWhenMoving(AttackerPrim, true);

                                UPhysicsConstraintComponent* PierceConstraint = NewObject<UPhysicsConstraintComponent>(TargetCharacter);
                                PierceConstraint->RegisterComponent();
                                PierceConstraint->SetWorldLocation(HitLocation);
                                
                                const FRotator ConstraintRot = (-HitNormal).Rotation();
                                PierceConstraint->SetWorldRotation(ConstraintRot);

                                PierceConstraint->SetAngularSwing1Limit(ACM_Locked, 0.0f);
                                PierceConstraint->SetAngularSwing2Limit(ACM_Locked, 0.0f);
                                PierceConstraint->SetAngularTwistLimit(ACM_Locked, 0.0f);
                                
                                PierceConstraint->SetLinearXLimit(LCM_Limited, OverpowerRule.MaxPiercingDepth);
                                PierceConstraint->SetLinearYLimit(LCM_Locked, 0.0f);
                                PierceConstraint->SetLinearZLimit(LCM_Locked, 0.0f);
                                
                                PierceConstraint->SetLinearPositionDrive(true, true, true);
                                PierceConstraint->SetLinearDriveParams(500.0f, 50.0f, 0.0f);
                                
                                PierceConstraint->SetConstrainedComponents(AttackerPrim, NAME_None, TargetMesh, HitBoneName);
                                
                                FTimerHandle PierceHandle;
                                TargetCharacter->GetWorldTimerManager().SetTimer(PierceHandle, [PierceConstraint, TargetMesh, AttackerPrim]() {
                                    if (IsValid(PierceConstraint))
                                    {
                                        PierceConstraint->BreakConstraint();
                                        PierceConstraint->DestroyComponent();
                                    }
                                    if (IsValid(TargetMesh) && IsValid(AttackerPrim))
                                    {
                                        TargetMesh->IgnoreComponentWhenMoving(AttackerPrim, false);
                                    }
                                }, OverpowerRule.IgnoreCollisionDuration, false);
                            }
                        }
                    }
                    else if (OverpowerRule.bIgnoreCollisionOnOverpower)
                    {
                        RuleRecoilImpulse *= 0.15f; 
                        RuleTargetImpulse = WeaponMomentum;

                        if (USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMeshComponent())
                        {
                            if (AttackerPrim)
                            {
                                TargetMesh->IgnoreComponentWhenMoving(AttackerPrim, true);

                                FTimerHandle RestoreHandle;
                                TargetCharacter->GetWorldTimerManager().SetTimer(RestoreHandle, [TargetMesh, AttackerPrim]() {
                                    if (IsValid(TargetMesh) && IsValid(AttackerPrim))
                                    {
                                        TargetMesh->IgnoreComponentWhenMoving(AttackerPrim, false);
                                    }
                                }, OverpowerRule.IgnoreCollisionDuration, false);
                            }
                        }
                    }

                    if (OverpowerRule.bHyperextendLimbOnOverpower)
                    {
                        RuleRecoilImpulse *= 0.3f;
                        if (UPhysicsControlComponent* PCC = TargetCharacter->GetPhysicsControlComponent())
                        {
                            const FName ControlName = FName(*FString::Printf(TEXT("Control_%s"), *HitBoneName.ToString()));
                            FPhysicsControlMultiplier BrokenMult;
                            BrokenMult.LinearStrengthMultiplier = FVector(0.0f);
                            BrokenMult.AngularStrengthMultiplier = 0.0f;
                            BrokenMult.LinearExtraDampingMultiplier = FVector(0.1f);
                            BrokenMult.AngularExtraDampingMultiplier = 0.1f;
                            PCC->SetControlMultiplier(ControlName, BrokenMult);
                        }
                    }

                    if (OverpowerRule.bForceRagdollOnOverpower)
                    {
                        TargetCharacter->StartRagdoll();
                    }

                    if (TargetASC && OverpowerRule.StatusEffectsToApply.Num() > 0)
                    {
                        FGameplayTagContainer DebuffContainer;
                        for (const FGameplayTag& DebuffTag : OverpowerRule.StatusEffectsToApply)
                        {
                            if (DebuffTag.IsValid())
                            {
                                DebuffContainer.AddTag(DebuffTag);
                            }
                        }
                        TargetASC->AddLooseGameplayTags(DebuffContainer);
                    }
                }
            }

            HighestTargetImpulse = FMath::Max(HighestTargetImpulse, RuleTargetImpulse);
            HighestRecoilImpulse = FMath::Max(HighestRecoilImpulse, RuleRecoilImpulse);
        }
    }

    HighestTargetImpulse = FMath::Min(HighestTargetImpulse, Settings.MaxImpulseCap);
    if (HighestTargetImpulse > Settings.MinImpulseThreshold)
    {
        if (UDynamicForceComponent* TargetForceComp = TargetCharacter->FindComponentByClass<UDynamicForceComponent>())
        {
            TargetForceComp->ServerApplyLimbForce(ResolvedLimbId, HitBoneName, HighestTargetImpulse, HitLocation, HitNormal);
        }
    }

    // REACTIVE STATE TREE EVENT DISPATCH: React immediately upon being hit
    if (AAIController* AICon = Cast<AAIController>(TargetCharacter->GetController()))
    {
        if (UDynamicStateTreeComponent* StateTreeComp = AICon->FindComponentByClass<UDynamicStateTreeComponent>())
        {
            FDynamicStateTreeEvent DmgEvent;
            DmgEvent.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Sense.Damage"), false);
            DmgEvent.EventInstigator = InstigatorActor;
            DmgEvent.EventLocation = HitLocation;
            DmgEvent.EventMagnitude = HighestTargetImpulse;
            StateTreeComp->SendStateTreeEvent(DmgEvent);
        }
    }

    HighestRecoilImpulse = FMath::Min(HighestRecoilImpulse, Settings.MaxImpulseCap);
    if (HighestRecoilImpulse > Settings.MinImpulseThreshold && InstigatorActor && InstigatorActor != TargetCharacter)
    {
        if (ADynamicCharacter* AttackerChar = Cast<ADynamicCharacter>(InstigatorActor))
        {
            if (UDynamicForceComponent* AttackerForceComp = AttackerChar->FindComponentByClass<UDynamicForceComponent>())
            {
                const FName AttackerBone = FName("hand_r"); 
                const uint8 AttackerLimbId = DynamicGS->GetLimbIdForBone(AttackerBone);
                AttackerForceComp->ServerApplyLimbForce(AttackerLimbId, AttackerBone, HighestRecoilImpulse, InstigatorActor->GetActorLocation(), HitNormal);
            }
        }
    }
    
    if (TargetCharacter->NetDormancy == DORM_DormantAll)
    {
        TargetCharacter->SetNetDormancy(DORM_Awake);
        TargetCharacter->FlushNetDormancy();
    }
}

void ADynamicCharacter::RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren)
{
    if (!AttributeSet || LimbId == 0) return;

    ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!DynamicGS) return;

    const FPhysicsLimbConfig* LimbConfig = DynamicGS->GetLimbData(LimbId);
    if (!LimbConfig) return;

    float IntegrityRatio = 1.0f;
    if (LimbConfig->IntegrityAttributeTag.IsValid() && LimbConfig->MaxIntegrityAttributeTag.IsValid())
    {
        const float MaxIntegrity = FMath::Max(1.0f, AttributeSet->GetDynamicAttributeValue(LimbConfig->MaxIntegrityAttributeTag, LimbId));
        IntegrityRatio = FMath::Clamp(AttributeSet->GetDynamicAttributeValue(LimbConfig->IntegrityAttributeTag, LimbId) / MaxIntegrity, 0.0f, 1.0f);
    }

    float EnduranceRatio = 1.0f;
    if (LimbConfig->EnduranceAttributeTag.IsValid() && LimbConfig->MaxEnduranceAttributeTag.IsValid())
    {
        const float MaxEndurance = FMath::Max(1.0f, AttributeSet->GetDynamicAttributeValue(LimbConfig->MaxEnduranceAttributeTag, LimbId));
        EnduranceRatio = FMath::Clamp(AttributeSet->GetDynamicAttributeValue(LimbConfig->EnduranceAttributeTag, LimbId) / MaxEndurance, 0.15f, 1.0f);
    }

    float ParentIntegrity = 1.0f;
    if (LimbConfig->ParentLimbId > 0 && LimbConfig->ParentLimbId != LimbId)
    {
        if (const FPhysicsLimbConfig* ParentConfig = DynamicGS->GetLimbData(LimbConfig->ParentLimbId))
        {
            if (ParentConfig->IntegrityAttributeTag.IsValid() && ParentConfig->MaxIntegrityAttributeTag.IsValid())
            {
                const float ParentMax = FMath::Max(1.0f, AttributeSet->GetDynamicAttributeValue(ParentConfig->MaxIntegrityAttributeTag, LimbConfig->ParentLimbId));
                ParentIntegrity = FMath::Clamp(AttributeSet->GetDynamicAttributeValue(ParentConfig->IntegrityAttributeTag, LimbConfig->ParentLimbId) / ParentMax, 0.0f, 1.0f);
            }
        }
    }

    float AngularAttributeBonus = 0.0f;
    for (const auto& Pair : LimbConfig->AngularMotorAttributeScalars.LocalLookupMap)
    {
        if (Pair.Key.IsValid())
        {
            AngularAttributeBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, LimbId) * Pair.Value;
        }
    }

    float LinearAttributeBonus = 0.0f;
    for (const auto& Pair : LimbConfig->LinearMotorAttributeScalars.LocalLookupMap)
    {
        if (Pair.Key.IsValid())
        {
            LinearAttributeBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, LimbId) * Pair.Value;
        }
    }

    const float BaseIntegrity = IntegrityRatio * EnduranceRatio * ParentIntegrity;
    const float AngularDriveMult = BaseIntegrity * FMath::Max(0.0f, 1.0f + AngularAttributeBonus);
    const float LinearDriveMult = BaseIntegrity * FMath::Max(0.0f, 1.0f + LinearAttributeBonus);
    const float DampingMult = FMath::Lerp(0.25f, 1.0f, EnduranceRatio);

    for (const FPhysicsJointNodeConfig& JointNode : LimbConfig->JointChain)
    {
        if (!JointNode.BoneName.IsNone())
        {
            ApplyJointMotorMultipliers(
                JointNode.BoneName,
                JointNode.JointChainIndex,
                AngularDriveMult * JointNode.MotorDriveWeight,
                LinearDriveMult * JointNode.MotorDriveWeight,
                DampingMult);
        }
    }

    if (bCascadeToChildren)
    {
        for (const auto& Pair : DynamicGS->GetLimbLookupMap())
        {
            if (Pair.Value.ParentLimbId == LimbId && Pair.Key != LimbId)
            {
                RecalculateLimbPhysicsMotors(Pair.Key, false);
            }
        }
    }
}

void ADynamicCharacter::ApplyJointMotorMultipliers_Implementation(
    FName BoneName, 
    uint8 JointChainIndex, 
    float AngularSpringMultiplier, 
    float LinearSpringMultiplier, 
    float DampingMultiplier)
{
    if (!PhysicsControlComponent) return;

    const FName ControlName = FName(*FString::Printf(TEXT("Control_%s"), *BoneName.ToString()));
    FPhysicsControlMultiplier Multiplier;
    Multiplier.LinearStrengthMultiplier = FVector(LinearSpringMultiplier);
    Multiplier.LinearExtraDampingMultiplier = FVector(DampingMultiplier);
    Multiplier.AngularStrengthMultiplier = AngularSpringMultiplier;
    Multiplier.AngularExtraDampingMultiplier = DampingMultiplier;

    PhysicsControlComponent->SetControlMultiplier(ControlName, Multiplier);
}

void ADynamicCharacter::AlignLimbToInteractionTarget_Implementation(FName EffectorBoneName, const FTransform& TargetWorldTransform, USceneComponent* TargetComponent)
{
}

void ADynamicCharacter::ResetToDefaultPoolState()
{
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAllAbilities();
        FGameplayEffectQuery AllEffectsQuery;
        AbilitySystemComponent->RemoveActiveEffects(AllEffectsQuery);
        FGameplayTagContainer ActiveTags;
        AbilitySystemComponent->GetOwnedGameplayTags(ActiveTags);
        AbilitySystemComponent->RemoveLooseGameplayTags(ActiveTags);
        AbilitySystemComponent->ClearAllAbilities();
        AbilitySystemComponent->RemoveAllGameplayCues();
    }
    RecoverFromRagdoll();
    if (ForceComponent)
    {
        ForceComponent->ConsumePendingImpulse();
    }
    if (MoverComponent)
    {
        MoverComponent->Deactivate();
    }
    if (UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
    {
        EquipComp->ClearEquipmentForPooling();
    }
    if (UDynamicContainerComponent* Container = FindComponentByClass<UDynamicContainerComponent>())
    {
        Container->ClearContainerForPooling();
    }
    HistoryBufferWriteIndex = 0;
    HistoryBufferCount = 0;
    for (FCharacterTransformHistorySnapshot& Snapshot : TransformHistoryBuffer)
    {
        Snapshot.ServerTimestamp = 0.0;
        // Snapshot.BoneTransforms.Reset(); // REMOVED: BoneTransforms was decoupled in mathematical lag compensation
    }
    bIsCurrentlyRewound = false;
    ServerLastLimbHitTimestamp.Empty();
    if (UDynamicCustomizationComponent* Cust = GetCustomizationComponent())
    {
        Cust->ReplicatedMorphs.Items.Empty();
        Cust->ReplicatedMorphs.MarkArrayDirty();
        Cust->OnRep_ReplicatedMorphs();
    }
    if (UDynamicInteractableComponent* Interactable = FindComponentByClass<UDynamicInteractableComponent>())
    {
        Interactable->ResetForPooling();
    }
    HyperextendedJoints.Empty();
}

bool ADynamicCharacter::CalculatePseudoBoneIntersection(const FCharacterTransformHistorySnapshot& Snapshot, const FVector& StartTrace, const FVector& EndTrace, FName& OutHitBone)
{
    // Mathematical lag compensation based strictly on the snapshot state
    const FVector CapLoc = Snapshot.CapsuleLocation;
    const FVector Forward = Snapshot.CapsuleRotation.GetForwardVector();
    const FVector Right = Snapshot.CapsuleRotation.GetRightVector();
    const FVector Up = Snapshot.CapsuleRotation.GetUpVector();

    // Define approximate local offsets based on 88 half height
    const FVector HeadCenter = CapLoc + (Up * 60.0f) + (Forward * FMath::Sin(FMath::DegreesToRadians(Snapshot.ViewPitch)) * 10.0f);
    const FVector PelvisCenter = CapLoc - (Up * 10.0f);
    
    // Simple point-to-line distance checks acting as collision spheres
    const FVector LineDir = (EndTrace - StartTrace).GetSafeNormal();
    
    auto DistToLineSq = [&](const FVector& Point) -> float
    {
        const FVector PointToStart = Point - StartTrace;
        const float ProjLength = FVector::DotProduct(PointToStart, LineDir);
        if (ProjLength < 0.0f || ProjLength > FVector::Distance(StartTrace, EndTrace)) return MAX_flt;
        const FVector ProjPoint = StartTrace + (LineDir * ProjLength);
        return FVector::DistSquared(Point, ProjPoint);
    };

    if (DistToLineSq(HeadCenter) < FMath::Square(25.0f))
    {
        OutHitBone = FName("head");
        return true;
    }
    if (DistToLineSq(PelvisCenter) < FMath::Square(40.0f))
    {
        OutHitBone = FName("pelvis");
        return true;
    }
    return false;
}

void ADynamicCharacter::ServerProcessDeath(AActor* KillerActor)
{
    if (!HasAuthority()) return;

    StartRagdoll();

    if (KillerActor && KillerActor != this)
    {
        if (UDynamicSocialComponent* SocialComp = FindComponentByClass<UDynamicSocialComponent>())
        {
            SocialComp->ServerEvaluateAndBroadcastAction(
                FGameplayTag::RequestGameplayTag(TEXT("Action.Combat.Kill"), false), 
                KillerActor, 
                this, 
                FGuid(),
                TArray<FGuid>()
            );
        }
    }

    if (UDynamicWorldDirectorSubsystem* Director = GetWorld()->GetSubsystem<UDynamicWorldDirectorSubsystem>())
    {
        Director->UnregisterActiveNPC(this);
    }
}

void ADynamicCharacter::ServerBroadcastSensoryStimulus(FGameplayTag StimulusTag, float BaseMagnitudeOverride)
{
    if (!HasAuthority() || !StimulusTag.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return;

    float FinalMagnitude = BaseMagnitudeOverride;
    if (FinalMagnitude <= 0.0f)
    {
        FinalMagnitude = 500.0f; 
    }

    const float TotalMass = CalculateTotalInventoryAndEquipmentMass();
    FinalMagnitude *= FMath::Max(0.5f, TotalMass / 80.0f);

    TMap<FGameplayTag, float> Signatures;
    Signatures.Add(StimulusTag, FinalMagnitude);

    const FVector Loc = GetActorLocation();

    // OPTIMIZATION: Replaced global character map iteration with PhysX BVH spatial query
    TArray<FOverlapResult> Overlaps;
    FCollisionShape SphereShape = FCollisionShape::MakeSphere(FinalMagnitude);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SensoryBroadcast), false, this);

    if (World->OverlapMultiByChannel(Overlaps, Loc, FQuat::Identity, ECC_Pawn, SphereShape, QueryParams))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            if (ADynamicCharacter* OtherChar = Cast<ADynamicCharacter>(Overlap.GetActor()))
            {
                if (UDynamicSocialComponent* OtherSocial = OtherChar->FindComponentByClass<UDynamicSocialComponent>())
                {
                    OtherSocial->ServerProcessCrimeSensoryEvent(
                        FGameplayTag(),
                        this,
                        nullptr,
                        FGuid(),
                        TArray<FGuid>(),
                        Signatures
                    );
                }
            }
        }
    }
}

void ADynamicCharacter::ConfigureRagdollJoints()
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !MeshComponent || !AttributeSet || !CustomizationComponent) return;
    
    const FGameplayTag TemplateTag = CustomizationComponent->ActiveCharacterTemplateTag;
    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(TemplateTag, TemplateDef)) return;
    
    const FDynamicSkeletonDef* SkelDef = GS->SkeletonMasterRegistry.Find(TemplateDef.SkeletonTypeTag);
    if (!SkelDef) return;
    
    for (const FDynamicJointLimitProfile& JointProfile : SkelDef->JointLimits)
    {
        // Calculate Flexibility (Range of Motion)
        float ROMScale = 1.0f;
        for (const auto& Pair : JointProfile.RangeOfMotionAttributeScalars.LocalLookupMap)
        {
            if (Pair.Key.IsValid()) ROMScale += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
        }
        ROMScale = FMath::Max(0.1f, ROMScale);
        
        // Calculate Physical Resistance (Soft Constraint Stiffness)
        float ResistanceMult = 1.0f;
        for (const auto& Pair : JointProfile.ResistanceAttributeScalars.LocalLookupMap)
        {
            if (Pair.Key.IsValid()) ResistanceMult += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
        }
        ResistanceMult = FMath::Max(0.1f, ResistanceMult);
        
        // Configure specific bone constraint dynamically
        if (FConstraintInstance* Constraint = MeshComponent->FindConstraintInstance(JointProfile.BoneName))
        {
            Constraint->SetAngularSwing1Limit(ACM_Limited, JointProfile.BaseSwing1Limit * ROMScale);
            Constraint->SetAngularSwing2Limit(ACM_Limited, JointProfile.BaseSwing2Limit * ROMScale);
            Constraint->SetAngularTwistLimit(ACM_Limited, JointProfile.BaseTwistLimit * ROMScale);
            
            // Soft limits prevent hard bouncing and simulate bodily resistance tearing
            Constraint->ProfileInstance.ConeLimit.bSoftConstraint = true;
            Constraint->ProfileInstance.ConeLimit.Stiffness = 500.0f * ResistanceMult;
            Constraint->ProfileInstance.ConeLimit.Damping = 50.0f * ResistanceMult;
            Constraint->UpdateAngularLimit();
        }
    }
}

void ADynamicCharacter::UpdateRagdollHyperextension(float DeltaSeconds)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !MeshComponent || !AttributeSet || !CustomizationComponent) return;
    
    const FGameplayTag TemplateTag = CustomizationComponent->ActiveCharacterTemplateTag;
    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(TemplateTag, TemplateDef)) return;
    
    const FDynamicSkeletonDef* SkelDef = GS->SkeletonMasterRegistry.Find(TemplateDef.SkeletonTypeTag);
    if (!SkelDef) return;
    
    for (const FDynamicJointLimitProfile& JointProfile : SkelDef->JointLimits)
    {
        if (HyperextendedJoints.Contains(JointProfile.BoneName)) continue;
        
        const FName ParentBone = MeshComponent->GetParentBone(JointProfile.BoneName);
        if (ParentBone == NAME_None) continue;
        
        const FTransform ChildTransform = MeshComponent->GetSocketTransform(JointProfile.BoneName, RTS_World);
        const FTransform ParentTransform = MeshComponent->GetSocketTransform(ParentBone, RTS_World);
        
        // Find the absolute angular divergence between parent and child segments in degrees
        const FTransform RelativeTransform = ChildTransform.GetRelativeTransform(ParentTransform);
        const float AngleDiff = FMath::RadiansToDegrees(RelativeTransform.GetRotation().GetAngle());
        
        float ROMScale = 1.0f;
        for (const auto& Pair : JointProfile.RangeOfMotionAttributeScalars.LocalLookupMap)
        {
            if (Pair.Key.IsValid()) ROMScale += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
        }
        
        // Find the maximum allowed limit before injury occurs
        const float MaxLimit = FMath::Max3(JointProfile.BaseSwing1Limit, JointProfile.BaseSwing2Limit, JointProfile.BaseTwistLimit) * ROMScale;
        
        // Evaluate Hyperextension
        if (AngleDiff > (MaxLimit + JointProfile.HyperextensionThresholdAngle))
        {
            HyperextendedJoints.Add(JointProfile.BoneName);
            
            if (PhysicsControlComponent)
            {
                const FName ControlName = FName(*FString::Printf(TEXT("Control_%s"), *JointProfile.BoneName.ToString()));
                FPhysicsControlMultiplier BrokenMult;
                BrokenMult.LinearStrengthMultiplier = FVector(0.0f);
                BrokenMult.AngularStrengthMultiplier = 0.0f;
                PhysicsControlComponent->SetControlMultiplier(ControlName, BrokenMult);
            }

            // 2. DISLOCATION SEPARATION: Apply specific dislocation tags, separate from blunt fractures
            if (AbilitySystemComponent)
            {
                FGameplayTagContainer DebuffContainer;
                // Add the generic dislocation tag
                DebuffContainer.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Debuff.Dislocated"), false));
                
                // Add specific configured effects from the JSON definition
                for (const FGameplayTag& EffTag : JointProfile.HyperextensionEffects)
                {
                    if (EffTag.IsValid()) DebuffContainer.AddTag(EffTag);
                }
                AbilitySystemComponent->AddLooseGameplayTags(DebuffContainer);
            }
        }
    }
}

void ADynamicCharacter::UpdateNetworkDormancy(float DeltaSeconds)
{
    // Do not put possessed player characters to sleep
    if (IsPlayerControlled()) return;

    bool bCanSleep = true;

    // 1. Ensure velocity is 0
    if (GetVelocity().SizeSquared() > 10.0f)
    {
        bCanSleep = false;
    }

    // 2. Ensure they aren't engaged in conversation or processing an LLM response
    if (SocialComponent)
    {
        if (SocialComponent->ActiveTurnState != EConversationalTurnState::Idle)
        {
            bCanSleep = false;
        }
    }

    // 3. Ensure they aren't actively executing an ability
    if (AbilitySystemComponent)
    {
        for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
        {
            if (Spec.IsActive())
            {
                bCanSleep = false;
                break;
            }
        }
    }

    if (bCanSleep)
    {
        IdleDormancyTimer += DeltaSeconds;
        // If idle for 5 seconds, shut down replication to all clients
        if (IdleDormancyTimer >= 5.0f && NetDormancy != DORM_DormantAll)
        {
            SetNetDormancy(DORM_DormantAll);
        }
    }
    else
    {
        IdleDormancyTimer = 0.0f;
        if (NetDormancy == DORM_DormantAll)
        {
            // Instantly wake up the actor on the network to resume syncing movement/chat
            SetNetDormancy(DORM_Awake);
            FlushNetDormancy();
        }
    }
}

void ADynamicCharacter::OnRep_MountState()
{
    if (!CurrentMount.IsValid())
    {
        RecoverFromRagdoll(); // Auto-recover if unmounted gracefully
    }
}

void ADynamicCharacter::OnPhysicsConstraintBroken(int32 ConstraintIndex)
{
    if (HasAuthority())
    {
        // If the mount/tether snaps due to massive impact force, the character violently ragdolls
        ServerDismount();
        StartRagdoll();
        
        if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
        {
            ASC->AddLooseGameplayTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("State.Debuff.Stunned"), false)));
        }
    }
}

void ADynamicCharacter::ServerSetMount(AActor* MountActor, FGameplayTag SaddlePointTag, float BreakForce, float SaddleSlop)
{
    if (!HasAuthority() || !MountActor) return;
    
    CurrentMount = MountActor;
    
    FTransform SaddleTransform;
    USceneComponent* SaddleComp = nullptr;
    if (UDynamicInteractableComponent* MountInteractable = MountActor->FindComponentByClass<UDynamicInteractableComponent>())
    {
        MountInteractable->ResolveKinematicTargetTransform(SaddlePointTag, SaddleTransform, SaddleComp);
    }
    if (!SaddleComp) SaddleComp = MountActor->GetRootComponent();

    // Position Rider
    SetActorLocationAndRotation(SaddleTransform.GetLocation(), SaddleTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
    
    if (USkeletalMeshComponent* MountMesh = Cast<USkeletalMeshComponent>(SaddleComp))
    {
        MeshComponent->IgnoreComponentWhenMoving(MountMesh, true);
        CapsuleComponent->IgnoreComponentWhenMoving(MountMesh, true);
    }

    ServerSetMovementState(FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Mounted"), false));

    if (!ActivePhysicsConstraint)
    {
        ActivePhysicsConstraint = NewObject<UPhysicsConstraintComponent>(this);
        ActivePhysicsConstraint->RegisterComponent();
        ActivePhysicsConstraint->OnConstraintBroken.AddDynamic(this, &ADynamicCharacter::OnPhysicsConstraintBroken);
    }

    ActivePhysicsConstraint->SetWorldLocation(SaddleTransform.GetLocation());
    
    // Soft lock allows dynamic leaning/bouncing in the saddle
    ActivePhysicsConstraint->SetAngularSwing1Limit(ACM_Limited, 20.0f);
    ActivePhysicsConstraint->SetAngularSwing2Limit(ACM_Limited, 20.0f);
    ActivePhysicsConstraint->SetAngularTwistLimit(ACM_Limited, 10.0f);
    
    // Linear slop dictates how much the rider bounces vertically/horizontally
    ActivePhysicsConstraint->SetLinearXLimit(LCM_Limited, SaddleSlop);
    ActivePhysicsConstraint->SetLinearYLimit(LCM_Limited, SaddleSlop);
    ActivePhysicsConstraint->SetLinearZLimit(LCM_Limited, SaddleSlop * 2.0f);
    
    ActivePhysicsConstraint->SetLinearPositionDrive(true, true, true);
    ActivePhysicsConstraint->SetLinearDriveParams(1500.0f, 150.0f, 0.0f);

    if (BreakForce > 0.0f)
    {
        ActivePhysicsConstraint->SetLinearBreakable(true, BreakForce);
        ActivePhysicsConstraint->SetAngularBreakable(true, BreakForce);
    }
    
    ActivePhysicsConstraint->SetConstrainedComponents(MeshComponent, FName("pelvis"), Cast<UPrimitiveComponent>(SaddleComp), NAME_None);
}

void ADynamicCharacter::ServerDismount()
{
    if (!HasAuthority()) return;
    
    if (CurrentMount.IsValid())
    {
        if (USkeletalMeshComponent* MountMesh = Cast<USkeletalMeshComponent>(CurrentMount->GetRootComponent()))
        {
            MeshComponent->IgnoreComponentWhenMoving(MountMesh, false);
            CapsuleComponent->IgnoreComponentWhenMoving(MountMesh, false);
        }
    }

    CurrentMount.Reset();
    CurrentTetheredActor.Reset(); // Clear the tracked tether
    
    if (ActivePhysicsConstraint)
    {
        ActivePhysicsConstraint->BreakConstraint();
        ActivePhysicsConstraint->DestroyComponent();
        ActivePhysicsConstraint = nullptr;
    }

    ServerSetMovementState(DefaultMovementStateTag);
}

void ADynamicCharacter::ServerEstablishTether(AActor* TargetActor, FName EffectorBone, FGameplayTag TargetPointTag, float MaxLength, float BreakForce)
{
    if (!HasAuthority() || !TargetActor) return;

    FTransform AnchorTransform;
    USceneComponent* AnchorComp = nullptr;
    if (UDynamicInteractableComponent* TargetInteractable = TargetActor->FindComponentByClass<UDynamicInteractableComponent>())
    {
        TargetInteractable->ResolveKinematicTargetTransform(TargetPointTag, AnchorTransform, AnchorComp);
    }
    if (!AnchorComp) AnchorComp = TargetActor->GetRootComponent();

    if (!ActivePhysicsConstraint)
    {
        ActivePhysicsConstraint = NewObject<UPhysicsConstraintComponent>(this);
        ActivePhysicsConstraint->RegisterComponent();
        ActivePhysicsConstraint->OnConstraintBroken.AddDynamic(this, &ADynamicCharacter::OnPhysicsConstraintBroken);
    }

    ActivePhysicsConstraint->SetWorldLocation(MeshComponent->GetSocketLocation(EffectorBone));
    
    // Free rotation, limited distance
    ActivePhysicsConstraint->SetAngularSwing1Limit(ACM_Free, 0.0f);
    ActivePhysicsConstraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
    ActivePhysicsConstraint->SetAngularTwistLimit(ACM_Free, 0.0f);
    
    ActivePhysicsConstraint->SetLinearXLimit(LCM_Limited, MaxLength);
    ActivePhysicsConstraint->SetLinearYLimit(LCM_Limited, MaxLength);
    ActivePhysicsConstraint->SetLinearZLimit(LCM_Limited, MaxLength);

    if (BreakForce > 0.0f)
    {
        ActivePhysicsConstraint->SetLinearBreakable(true, BreakForce);
    }
    
    ActivePhysicsConstraint->SetConstrainedComponents(MeshComponent, EffectorBone, Cast<UPrimitiveComponent>(AnchorComp), NAME_None);
    CurrentTetheredActor = TargetActor; // Track for scraping audio
}

void ADynamicCharacter::ApplyEnvironmentalForces(float DeltaSeconds)
{
    if (!HasAuthority() || IsPendingKillPending() || IsHidden()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    // Fetch live wind/weather metric from the global Voxel/Environment cells
    static const FGameplayTag WindSpeedTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.WindSpeed"), false);
    const float WindSpeed = GS->GetEnvironmentMetricAtLocation(GetActorLocation(), WindSpeedTag);

    if (WindSpeed > 10.0f) // Minimum wind threshold to affect characters (10 m/s)
    {
        const int32 CellIdx = GS->GetEnvironmentCellIndexAtLocation(GetActorLocation());
        if (GS->GetEnvironmentReadBuffer().IsValidIndex(CellIdx))
        {
            const FVector WindDir = FVector(GS->GetEnvironmentReadBuffer()[CellIdx].WindDirection);
            
            // Physics characters (ragdolling, falling, or flying) catch wind
            static const FGameplayTag FallModeTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Fall"), false);
            static const FGameplayTag FlyModeTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Fly"), false);

            const bool bIsAirborne = ActiveMovementStateTag.MatchesTag(FallModeTag) ||
                                     ActiveMovementStateTag.MatchesTag(FlyModeTag);

            if (bIsRagdolling || bIsAirborne)
            {
                // Surface Area approximation (standing up catches more wind than prone)
                const float SurfaceAreaMult = bIsRagdolling ? 0.5f : 1.25f; 
                
                // Wind Force = Speed * Area * Constant
                const FVector WindForce = WindDir * (WindSpeed * 15.0f * SurfaceAreaMult);
                
                if (bIsRagdolling && MeshComponent)
                {
                    MeshComponent->AddForce(WindForce, NAME_None, true); // true = acceleration (mass independent)
                }
                else if (bIsAirborne && MoverComponent)
                {
                    // For Mover 2.0, queue an instantaneous velocity layer
                    if (UDynamicForceComponent* ForceComp = FindComponentByClass<UDynamicForceComponent>())
                    {
                        ForceComp->QueueInstantMovementImpulse(WindDir, WindSpeed * 2.0f);
                    }
                }
            }
        }
    }
}

void ADynamicCharacter::EvaluatePhysicalEncumbrance(float& OutTotalMass, FVector& OutCoMOffset) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    
    const float BaselineMass = GS ? FMath::Max(1.0f, GS->GetCalculationSettings().BaselineCharacterMassKg) : 80.0f;
    const float ProximityRadius = GS ? FMath::Max(50.0f, GS->GetCalculationSettings().CarriedMassProximityRadius) : 300.0f;
    const FGameplayTag WeightTag = (GS && GS->GetCalculationSettings().ObjectWeightAttributeTag.IsValid())
        ? GS->GetCalculationSettings().ObjectWeightAttributeTag
        : FGameplayTag::RequestGameplayTag(TEXT("Attribute.Object.Weight"), false);

    OutTotalMass = BaselineMass;
    OutCoMOffset = FVector::ZeroVector;

    float WeightedCoMOffsetForward = 0.0f;
    float WeightedCoMOffsetRight = 0.0f;
    float WeightedCoMOffsetUp = 0.0f;

    // 1. Evaluate Equipped Items & Slot Directional Mass Offsets
    if (const UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
    {
        for (const FDynamicEquipmentSlotItem& Slot : EquipComp->ReplicatedEquipmentSlots.Items)
        {
            if (Slot.EquippedItemArchetypeTag.IsValid())
            {
                float ItemMass = 0.0f;

                // Priority A: Saved dynamic attributes on the item instance
                for (const FDynamicAttributeItem& Attr : Slot.SavedItemAttributeState)
                {
                    if (Attr.LimbId == 0 && Attr.AttributeTag == WeightTag)
                    {
                        ItemMass += Attr.Value;
                    }
                }

                // Priority B: Master Registry Archetype Defaults
                if (ItemMass <= 0.0f && GS)
                {
                    if (const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(Slot.EquippedItemArchetypeTag))
                    {
                        if (WeightTag.IsValid())
                        {
                            if (const float* FoundWeight = Def->DefaultGlobalAttributes.LocalLookupMap.Find(WeightTag))
                            {
                                ItemMass = *FoundWeight;
                            }
                        }
                        if (ItemMass <= 0.0f)
                        {
                            ItemMass = Def->MassOverride;
                        }
                    }
                }

                OutTotalMass += ItemMass;

                // Center of Mass physical bias based on slot location
                const FString SlotStr = Slot.SlotTag.ToString();
                if (SlotStr.Contains(TEXT("Back")) || SlotStr.Contains(TEXT("Shoulder.Over")))
                {
                    WeightedCoMOffsetForward -= (ItemMass * 0.25f);
                    WeightedCoMOffsetUp += (ItemMass * 0.15f);
                }
                else if (SlotStr.Contains(TEXT("Chest.Outer")) || SlotStr.Contains(TEXT("Chest.Main")))
                {
                    WeightedCoMOffsetForward += (ItemMass * 0.10f);
                    WeightedCoMOffsetUp += (ItemMass * 0.10f);
                }
                else if (SlotStr.Contains(TEXT("Hand.Right")))
                {
                    WeightedCoMOffsetForward += (ItemMass * 0.20f);
                    WeightedCoMOffsetRight += (ItemMass * 0.20f);
                }
                else if (SlotStr.Contains(TEXT("Hand.Left")))
                {
                    WeightedCoMOffsetForward += (ItemMass * 0.20f);
                    WeightedCoMOffsetRight -= (ItemMass * 0.20f);
                }
                else if (SlotStr.Contains(TEXT("Waist")) || SlotStr.Contains(TEXT("Pelvis")))
                {
                    WeightedCoMOffsetUp -= (ItemMass * 0.10f);
                }
            }
        }
    }

    // 2. Evaluate Internal Container Inventory Mass
    if (const UDynamicContainerComponent* Container = FindComponentByClass<UDynamicContainerComponent>())
    {
        const float ContainerMass = Container->GetCurrentTotalWeight();
        OutTotalMass += ContainerMass;

        // Container items typically sit in a backpack or belt pouches (shift CoM slightly backward/downward)
        WeightedCoMOffsetForward -= (ContainerMass * 0.20f);
        WeightedCoMOffsetUp += (ContainerMass * 0.05f);
    }

    // 3. Evaluate Physically Attached Actors (e.g. Carried items, hitched trailers, weapons in hand)
    TSet<const ADynamicPhysicsActor*> ProcessedPhysicsActors;

    TArray<AActor*> AttachedActors;
    GetAttachedActors(AttachedActors, true);
    for (AActor* Attached : AttachedActors)
    {
        if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Attached))
        {
            if (!PhysActor->IsPendingKillPending())
            {
                ProcessedPhysicsActors.Add(PhysActor);
                if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
                {
                    const float AttachedMass = Prim->GetMass();
                    OutTotalMass += AttachedMass;

                    // Compute local offset relative to character transform
                    const FVector LocalAttachPos = GetTransform().InverseTransformPosition(PhysActor->GetActorLocation());
                    WeightedCoMOffsetForward += LocalAttachPos.X * (AttachedMass / FMath::Max(1.0f, OutTotalMass));
                    WeightedCoMOffsetRight += LocalAttachPos.Y * (AttachedMass / FMath::Max(1.0f, OutTotalMass));
                    WeightedCoMOffsetUp += LocalAttachPos.Z * (AttachedMass / FMath::Max(1.0f, OutTotalMass));
                }
            }
        }
    }

    // 4. Evaluate Nearby Proximity Dragged/Tethered Physics Bodies
    if (World)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
        ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EvaluatePhysicalEncumbranceSweep), false, this);

        if (World->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, ObjParams, FCollisionShape::MakeSphere(ProximityRadius), QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
                {
                    if (!PhysActor->IsPendingKillPending() && !ProcessedPhysicsActors.Contains(PhysActor))
                    {
                        // Check if the physics actor is physically carried or dragged
                        if (const UDynamicInteractableComponent* Interactable = PhysActor->GetInteractableComponent())
                        {
                            static const FGameplayTag CarriedTag = FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.Carried"), false);
                            if (CarriedTag.IsValid() && Interactable->HasStateTag(CarriedTag))
                            {
                                ProcessedPhysicsActors.Add(PhysActor);
                                if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
                                {
                                    const float TetheredMass = Prim->GetMass();
                                    OutTotalMass += TetheredMass;

                                    const FVector LocalTetherPos = GetTransform().InverseTransformPosition(PhysActor->GetActorLocation());
                                    WeightedCoMOffsetForward += LocalTetherPos.X * (TetheredMass / FMath::Max(1.0f, OutTotalMass));
                                    WeightedCoMOffsetRight += LocalTetherPos.Y * (TetheredMass / FMath::Max(1.0f, OutTotalMass));
                                    WeightedCoMOffsetUp += LocalTetherPos.Z * (TetheredMass / FMath::Max(1.0f, OutTotalMass));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Normalize CoM shift by total mass and clamp to realistic physiological envelope
    const float SafeTotalMass = FMath::Max(1.0f, OutTotalMass);
    OutCoMOffset.X = FMath::Clamp(WeightedCoMOffsetForward / SafeTotalMass, -45.0f, 45.0f);
    OutCoMOffset.Y = FMath::Clamp(WeightedCoMOffsetRight / SafeTotalMass, -30.0f, 30.0f);
    OutCoMOffset.Z = FMath::Clamp(WeightedCoMOffsetUp / SafeTotalMass, -50.0f, 50.0f);
}

bool ADynamicCharacter::CanInitiateEmergencyClimbOrVault() const
{
    // Players have agency to climb at will; NPCs evaluate condition trees
    if (IsPlayerControlled()) return true;

    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC) return false;

    // 1. Condition Tree evaluation (e.g. Condition.AI.CanEmergencyClimbOrVault)
    const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(
        this, 
        FName(TEXT("Condition.AI.CanEmergencyClimbOrVault"))
    );

    if (Tree)
    {
        FGameplayTagContainer OwnedTags;
        ASC->GetOwnedGameplayTags(OwnedTags);
        if (UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, OwnedTags, OwnedTags))
        {
            return true;
        }
    }

    // 2. Direct Tag Fallback: Being attacked or pursued authorizes emergency traversal
    static const FGameplayTag AttackedTag = FGameplayTag::RequestGameplayTag(TEXT("State.Combat.AttackingMe"), false);
    static const FGameplayTag TrappedTag = FGameplayTag::RequestGameplayTag(TEXT("State.Navigation.TrappedNoExit"), false);
    static const FGameplayTag FleeingTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Flee"), false);

    if (AttackedTag.IsValid() && ASC->HasMatchingGameplayTag(AttackedTag)) return true;
    if (TrappedTag.IsValid() && ASC->HasMatchingGameplayTag(TrappedTag)) return true;
    if (FleeingTag.IsValid() && ActiveMovementStateTag.MatchesTag(FleeingTag)) return true;

    return false;
}

bool ADynamicCharacter::TryExecuteLedgeTraversal(const FVector& TargetLedgeLocation)
{
    if (!HasAuthority() || !CanInitiateEmergencyClimbOrVault()) return false;

    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC) return false;

    // Must have climbing capability granted via GAS
    static const FGameplayTag CapabilityClimbTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Movement.Climb"), false);
    if (!ASC->HasMatchingGameplayTag(CapabilityClimbTag))
    {
        return false;
    }

    const FVector CurrentLoc = GetActorLocation();
    const float HeightDelta = TargetLedgeLocation.Z - CurrentLoc.Z;

    // Ensure within physical reach (e.g. up to 2.5m obstacle)
    if (HeightDelta <= 0.0f || HeightDelta > 260.0f)
    {
        return false;
    }

    // 1. Transition into the exact same physical Climbing mode used by players
    static const FGameplayTag ClimbStateTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Climb"), false);
    if (!ServerSetMovementState(ClimbStateTag))
    {
        return false;
    }

    // 2. Register hands to the ledge anchor coordinates
    ActiveLimbHolds.Add(FName("hand_r"), FTransform(TargetLedgeLocation + FVector(0.0f, 25.0f, 0.0f)));
    ActiveLimbHolds.Add(FName("hand_l"), FTransform(TargetLedgeLocation - FVector(0.0f, 25.0f, 0.0f)));

    ConfigureClimbingPhysics(true, false);

    // 3. Trigger the data-driven GAS ability for mantle / climb completion
    FGameplayTagContainer VaultAbilityTags;
    VaultAbilityTags.AddTag(CapabilityClimbTag);
    ASC->TryActivateAbilitiesByTag(VaultAbilityTags);

    return true;
}

void ADynamicCharacter::ApplyCameraProfile(FGameplayTag ProfileTag)
{
    if (!ProfileTag.IsValid()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    if (const FDynamicCameraProfileDef* Profile = GS->CameraMasterRegistry.Find(ProfileTag))
    {
        ActiveCameraProfileTag = ProfileTag;
        
        // Default to third person if allowed, otherwise first person
        if (Profile->bAllowsThirdPerson)
        {
            SetCameraPerspective(false);
        }
        else if (Profile->bAllowsFirstPerson)
        {
            SetCameraPerspective(true);
        }
    }
}

void ADynamicCharacter::SetCameraPerspective(bool bSetFirstPerson)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !ActiveCameraProfileTag.IsValid()) return;

    const FDynamicCameraProfileDef* Profile = GS->CameraMasterRegistry.Find(ActiveCameraProfileTag);
    if (!Profile) return;

    if (bSetFirstPerson && !Profile->bAllowsFirstPerson) return;
    if (!bSetFirstPerson && !Profile->bAllowsThirdPerson) return;

    bIsFirstPerson = bSetFirstPerson;
    const FCameraPerspectiveDef& Config = bIsFirstPerson ? Profile->FirstPersonConfig : Profile->ThirdPersonConfig;

    CameraBoom->TargetArmLength = Config.DefaultDistance;
    CameraBoom->SocketOffset = Config.CameraOffset;
    CameraBoom->bUsePawnControlRotation = Config.bUsePawnControlRotation;
    FollowCamera->SetFieldOfView(Config.FOV);

    // In first person, we hide the head locally so it doesn't clip the camera
    if (USkeletalMeshComponent* Mesh = GetMeshComponent())
    {
        // Usually handled by OwnerNoSee on specific head bones via material masking, 
        // or by attaching the camera directly to a head socket. We use offset for stability.
    }
}

void ADynamicCharacter::TogglePerspective()
{
    SetCameraPerspective(!bIsFirstPerson);
}

void ADynamicCharacter::ZoomCamera(float Delta)
{
    if (FMath::IsNearlyZero(Delta)) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !ActiveCameraProfileTag.IsValid()) return;

    const FDynamicCameraProfileDef* Profile = GS->CameraMasterRegistry.Find(ActiveCameraProfileTag);
    if (!Profile) return;

    if (bIsFirstPerson)
    {
        // Scroll backwards (Delta < 0 typically) to zoom out into Third Person
        if (Delta < 0.0f && Profile->bAllowsThirdPerson)
        {
            SetCameraPerspective(false);
            CameraBoom->TargetArmLength = Profile->ThirdPersonConfig.MinDistance + Profile->ZoomSpeed;
        }
    }
    else
    {
        const float NewDistance = CameraBoom->TargetArmLength - (Delta * Profile->ZoomSpeed);
        
        if (NewDistance < Profile->AutoTransitionDistance && Profile->bAllowsFirstPerson)
        {
            SetCameraPerspective(true);
        }
        else
        {
            CameraBoom->TargetArmLength = FMath::Clamp(NewDistance, Profile->ThirdPersonConfig.MinDistance, Profile->ThirdPersonConfig.MaxDistance);
        }
    }
}