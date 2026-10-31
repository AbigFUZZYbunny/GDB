// DynamicCharacter.cpp
#include "DynamicCharacter.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicInteractableComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicSocialComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicForceComponent.h"
#include "DynamicWorldDirectorSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicContainerComponent.h"
#include "DynamicEffectExecution.h"
#include "DynamicGameplayEffectContext.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/Modes/SwimmingMode.h"
#include "EngineUtils.h"
#include "DynamicCustomizationComponent.h"
#include "Engine/OverlapResult.h"
#include "DynamicPhysicsActor.h"
#include "Net/UnrealNetwork.h"

ADynamicCharacter::ADynamicCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bReplicates = true;
    SetReplicateMovement(false);
    bIsRagdolling = false;
    bCurrentFootingActive = false;

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

    // DYNAMIC GATING: We only evaluate complex climbing overlaps if the character actively possesses the climbing capability
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
}

// ----------------------------------------------------------------------------
// Mass & Climbing Systems
// ----------------------------------------------------------------------------

float ADynamicCharacter::CalculateTotalInventoryAndEquipmentMass() const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    const float BaselineMass = GS ? FMath::Max(1.0f, GS->GetCalculationSettings().BaselineCharacterMassKg) : 80.0f;
    const float ProximityRadius = GS ? FMath::Max(50.0f, GS->GetCalculationSettings().CarriedMassProximityRadius) : 300.0f;
    const FGameplayTag WeightTag = (GS && GS->GetCalculationSettings().ObjectWeightAttributeTag.IsValid())
        ? GS->GetCalculationSettings().ObjectWeightAttributeTag
        : FGameplayTag::RequestGameplayTag(TEXT("Attribute.Object.Weight"), false);

    float TotalMassKg = BaselineMass;

    if (const UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
    {
        for (const FDynamicEquipmentSlotItem& Slot : EquipComp->ReplicatedEquipmentSlots.Items)
        {
            if (Slot.EquippedItemArchetypeTag.IsValid())
            {
                for (const FDynamicAttributeItem& Attr : Slot.SavedItemAttributeState)
                {
                    if (Attr.AttributeTag == WeightTag)
                    {
                        TotalMassKg += Attr.Value;
                    }
                }
            }
        }
    }

    if (const UDynamicContainerComponent* Container = FindComponentByClass<UDynamicContainerComponent>())
    {
        TotalMassKg += Container->GetCurrentTotalWeight();
    }

    TSet<const ADynamicPhysicsActor*> ProcessedPhysicsActors;

    // 1. Directly attached/carried physics actors (O(1) hierarchy query)
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
                    TotalMassKg += Prim->GetMass();
                }
            }
        }
    }

    // 2. Localized spatial sphere overlap for dragged/tethered physics actors (replaces TActorIterator)
    if (World)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
        ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CharacterMassOverlap), false, this);

        if (World->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, ObjParams, FCollisionShape::MakeSphere(ProximityRadius), QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
                {
                    if (!PhysActor->IsPendingKillPending() && !ProcessedPhysicsActors.Contains(PhysActor))
                    {
                        ProcessedPhysicsActors.Add(PhysActor);
                        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
                        {
                            TotalMassKg += Prim->GetMass();
                        }
                    }
                }
            }
        }
    }

    return TotalMassKg;
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
    else if (OldStateTag.MatchesTag(HangTag) || OldStateTag.MatchesTag(ClimbTag))
    {
        ConfigureClimbingPhysics(false, false);
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
    CustomContext->ContextPayloadTag = ImpactTypeTag;
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

    if (HasAuthority() && ActiveMovementStateTag.IsValid())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            if (const FDynamicMovementStateDef* Def = GS->MovementStateRegistry.Find(ActiveMovementStateTag))
            {
                ApplyMovementStateConfig(*Def);
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
    }
}

void ADynamicCharacter::RecoverFromRagdoll()
{
    if (!bIsRagdolling) return;
    bIsRagdolling = false;

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

    if (Def.TargetCapsuleHalfHeight.BaseValue > 0.0f)
    {
        CachedTargetCapsuleHalfHeight = EvaluateMovementStat(Def.TargetCapsuleHalfHeight);
    }

    if (HasAuthority() && AbilitySystemComponent)
    {
        if (GrantedMovementTags.Num() > 0)
        {
            AbilitySystemComponent->RemoveLooseGameplayTags(GrantedMovementTags);
        }
        GrantedMovementTags = Def.GrantsTags;
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

    // Evaluate mathematical bounding boxes on the server instead of animating rewound skeletal meshes
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
        // Fallback impact point estimate on hit
        ServerProcessAuthoritativeLimbHit(HitCharacter, HitBone, AuthoritativeForce, 1.0f, HitCharacter->GetActorLocation(), FVector(AimDirection) * -1.0f, this);
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

    const FName AuthoritativeBoneName = (Hit.MyBoneName != NAME_None) ? Hit.MyBoneName : Hit.BoneName;
    ServerProcessAuthoritativeLimbHit(this, AuthoritativeBoneName, ImpulseMagnitude, MassRatio, Hit.ImpactPoint, Hit.ImpactNormal, OtherActor);
}

void ADynamicCharacter::ServerProcessAuthoritativeLimbHit(ADynamicCharacter* TargetCharacter, const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor)
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

    const float ClampedImpulse = FMath::Min(RawImpulseMagnitude, Settings.MaxImpulseCap);
    if (UDynamicForceComponent* TargetForceComp = TargetCharacter->FindComponentByClass<UDynamicForceComponent>())
    {
        TargetForceComp->ServerApplyLimbForce(ResolvedLimbId, HitBoneName, ClampedImpulse, HitLocation, HitNormal);
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
    if (!GS) return;

    // Hypothetical MasterStimulusRegistry access (ensure it's added to ADynamicGameState)
    // const FDynamicStimulusDef* StimDef = GS->MasterStimulusRegistry.Find(StimulusTag);
    // if (!StimDef) return;

    float FinalMagnitude = BaseMagnitudeOverride; // Typically fallback to StimDef->BaseMagnitude
    
    // Scale the stimulus using dynamic attributes (e.g. Mass/Weight affecting Footstep noise)
    // for (const FTagFloatPair& Scalar : StimDef->AttributeMagnitudeScalars.ReplicatedEntries)
    // {
    //     if (AttributeSet)
    //     {
    //         FinalMagnitude += AttributeSet->GetDynamicAttributeValue(Scalar.KeyTag, 0) * Scalar.Value;
    //     }
    // }

    // Broadcast `FinalMagnitude` and `StimulusTag` to your AI Perception or sensory grid...
}