// DynamicCharacter.cpp
#include "DynamicCharacter.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicInteractableComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicForceComponent.h"
#include "DynamicWorldDirectorSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicContainerComponent.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/Modes/SwimmingMode.h"
#include "EngineUtils.h"
#include "DynamicCustomizationComponent.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"

ADynamicCharacter::ADynamicCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bReplicates = true;
    SetReplicateMovement(false);
    bIsRagdolling = false;
    bIsHangingPhysical = false;
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

    CustomizationComponent = CreateDefaultSubobject<UDynamicCustomizationComponent>(TEXT("CustomizationComponent"));
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

        if (DefaultMovementStateTag.IsValid())
        {
            ServerSetMovementState(DefaultMovementStateTag);
        }
    }
}

void ADynamicCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (HasAuthority())
    {
        PreviousVelocity = GetVelocity();

        if (CapsuleComponent && MeshComponent)
        {
            const double CurrentTime = GetWorld()->GetTimeSeconds();
            FCharacterTransformHistorySnapshot& Snapshot = TransformHistoryBuffer[HistoryBufferWriteIndex];
            Snapshot.ServerTimestamp = CurrentTime;
            Snapshot.CapsuleLocation = CapsuleComponent->GetComponentLocation();
            Snapshot.CapsuleRotation = CapsuleComponent->GetComponentQuat();

            Snapshot.BoneTransforms.Reset();
            static const TArray<FName> CriticalBones = { FName("pelvis"), FName("spine_01"), FName("head"), FName("hand_r"), FName("hand_l") };
            for (const FName& Bone : CriticalBones)
            {
                if (MeshComponent->DoesSocketExist(Bone))
                {
                    Snapshot.BoneTransforms.Add(Bone, MeshComponent->GetSocketTransform(Bone, RTS_World));
                }
            }

            HistoryBufferWriteIndex = (HistoryBufferWriteIndex + 1) % HistoryRingBufferSize;
            HistoryBufferCount = FMath::Min(HistoryBufferCount + 1, HistoryRingBufferSize);
        }

        if (AttributeSet && ActiveMovementStateTag.IsValid())
        {
            if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
            {
                if (const FDynamicMovementStateDef* Def = GS->MovementStateRegistry.Find(ActiveMovementStateTag))
                {
                    auto EvaluateStat = [&](const FMovementStatConfig& Config) -> float
                    {
                        float ScalarSum = 0.0f;
                        for (const auto& Pair : Config.AttributeScalars.LocalLookupMap)
                        {
                            ScalarSum += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
                        }
                        return Config.BaseValue * FMath::Max(0.01f, 1.0f + ScalarSum);
                    };

                    CachedTargetCapsuleHalfHeight = EvaluateStat(Def->TargetCapsuleHalfHeight);

                    static const FGameplayTag SpeedTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.Speed"), false);
                    static const FGameplayTag AccelTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.Acceleration"), false);
                    static const FGameplayTag DecelTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.Deceleration"), false);
                    static const FGameplayTag FricTag  = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.GroundFriction"), false);
                    static const FGameplayTag GravTag  = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.GravityScale"), false);

                    if (SpeedTag.IsValid()) AttributeSet->SetDynamicAttributeValue(SpeedTag, EvaluateStat(Def->MaxSpeed), 0);
                    if (AccelTag.IsValid()) AttributeSet->SetDynamicAttributeValue(AccelTag, EvaluateStat(Def->Acceleration), 0);
                    if (DecelTag.IsValid()) AttributeSet->SetDynamicAttributeValue(DecelTag, EvaluateStat(Def->Deceleration), 0);
                    if (FricTag.IsValid())  AttributeSet->SetDynamicAttributeValue(FricTag, EvaluateStat(Def->GroundFriction), 0);
                    if (GravTag.IsValid())  AttributeSet->SetDynamicAttributeValue(GravTag, EvaluateStat(Def->GravityScale), 0);
                }
            }
        }

        static const FGameplayTag HangTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Hang"), false);
        static const FGameplayTag ClimbTag = FGameplayTag::RequestGameplayTag(TEXT("Movement.Mode.Climb"), false);
        if (ActiveMovementStateTag.MatchesTag(HangTag) || ActiveMovementStateTag.MatchesTag(ClimbTag))
        {
            UpdateClimbingFootingState();
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

void ADynamicCharacter::OnServerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !OtherActor || OtherActor == this) return;

    // Detect steep wall/cliff surfaces
    if (FMath::Abs(Hit.ImpactNormal.Z) < 0.35f)
    {
        const float ImpactSpeed = PreviousVelocity.Size();
        if (ImpactSpeed > 0.0f)
        {
            ProcessWallImpact(Hit, ImpactSpeed);
        }
    }
}

void ADynamicCharacter::ProcessWallImpact(const FHitResult& Hit, float ImpactVelocity)
{
    if (!HasAuthority() || !AttributeSet) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FPhysicsCalculationSettings& Settings = GS->GetCalculationSettings();
    const FPhysicsImpactCalculationConfig& ImpactConfig = Settings.WallImpactSettings;

    if (ImpactVelocity <= ImpactConfig.BaseImpactVelocityThreshold) return;

    // 1. Identify specific collided bone using skeletal mesh geometry
    FName HitBoneName = Hit.MyBoneName;
    if (HitBoneName == NAME_None && MeshComponent)
    {
        HitBoneName = MeshComponent->FindClosestBone(Hit.ImpactPoint);
    }
    if (HitBoneName == NAME_None)
    {
        HitBoneName = FName("pelvis");
    }

    const uint8 ResolvedLimbId = GS->GetLimbIdForBone(HitBoneName);
    const FPhysicsLimbConfig* LimbData = GS->GetLimbData(ResolvedLimbId);

    // 2. Velocity Mitigation evaluated dynamically via JSON scalars
    float TotalVelocityDamping = 0.0f;
    for (const auto& Pair : ImpactConfig.VelocityMitigationScalars.LocalLookupMap)
    {
        if (Pair.Key.IsValid())
        {
            const float GlobalVal = AttributeSet->GetDynamicAttributeValue(Pair.Key, 0);
            const float LimbVal = (ResolvedLimbId > 0) ? AttributeSet->GetDynamicAttributeValue(Pair.Key, ResolvedLimbId) : 0.0f;
            TotalVelocityDamping += (GlobalVal + LimbVal) * Pair.Value;
        }
    }

    const float EffectiveVelocity = FMath::Max(0.0f, ImpactVelocity - TotalVelocityDamping);
    if (EffectiveVelocity <= ImpactConfig.BaseImpactVelocityThreshold) return;

    // 3. Raw Damage Calculation & Per-Limb Damage Multiplier (e.g. Head = 3.0x, Feet = 0.4x)
    const float ExcessVelocity = EffectiveVelocity - ImpactConfig.BaseImpactVelocityThreshold;
    const float BaseDamage = ExcessVelocity * ImpactConfig.BaseImpactDamageScalar;
    const float LimbMultiplier = LimbData ? LimbData->DamageMultiplier : 1.0f;
    const float UnmitigatedDamage = BaseDamage * LimbMultiplier;

    // 4. Damage Mitigation evaluated dynamically via JSON scalars (Toughness, Armor, Conditioning)
    float TotalDamageAbsorption = 0.0f;
    for (const auto& Pair : ImpactConfig.DamageMitigationScalars.LocalLookupMap)
    {
        if (Pair.Key.IsValid())
        {
            const float GlobalVal = AttributeSet->GetDynamicAttributeValue(Pair.Key, 0);
            const float LimbVal = (ResolvedLimbId > 0) ? AttributeSet->GetDynamicAttributeValue(Pair.Key, ResolvedLimbId) : 0.0f;
            TotalDamageAbsorption += (GlobalVal + LimbVal) * Pair.Value;
        }
    }

    const float FinalDamage = UnmitigatedDamage * FMath::Clamp(1.0f - TotalDamageAbsorption, 0.05f, 5.0f);

    // 5. Authoritatively apply damage to per-limb integrity and global health
    if (LimbData && LimbData->IntegrityAttributeTag.IsValid())
    {
        const float CurLimbVal = AttributeSet->GetDynamicAttributeValue(LimbData->IntegrityAttributeTag, ResolvedLimbId);
        AttributeSet->SetDynamicAttributeValue(LimbData->IntegrityAttributeTag, FMath::Max(0.0f, CurLimbVal - FinalDamage), ResolvedLimbId);
    }

    if (Settings.PrimaryDamageTargetAttributeTag.IsValid())
    {
        const float CurHealth = AttributeSet->GetDynamicAttributeValue(Settings.PrimaryDamageTargetAttributeTag, 0);
        AttributeSet->SetDynamicAttributeValue(Settings.PrimaryDamageTargetAttributeTag, FMath::Max(0.0f, CurHealth - FinalDamage), 0);
    }

    // 6. Dynamic Grip Retention Evaluation (Resisting grip loss and falling)
    float GripBonus = 0.0f;
    const uint8 RightHandLimb = GS->GetLimbIdForBone(FName("hand_r"));
    const uint8 LeftHandLimb = GS->GetLimbIdForBone(FName("hand_l"));

    for (const auto& Pair : ImpactConfig.GripRetentionScalars.LocalLookupMap)
    {
        if (Pair.Key.IsValid())
        {
            GripBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, 0) * Pair.Value;
            if (ResolvedLimbId > 0) GripBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, ResolvedLimbId) * Pair.Value;
            if (RightHandLimb != ResolvedLimbId) GripBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, RightHandLimb) * Pair.Value;
            if (LeftHandLimb != ResolvedLimbId) GripBonus += AttributeSet->GetDynamicAttributeValue(Pair.Key, LeftHandLimb) * Pair.Value;
        }
    }

    const float EffectiveGripThreshold = ImpactConfig.BaseGripLossThreshold + GripBonus;

    // Slam force breaks grip: Knockout or fall into full ragdoll
    if (FinalDamage > EffectiveGripThreshold || (LimbData && FinalDamage >= LimbData->BreakRagdollThreshold))
    {
        StartRagdoll();
        if (ForceComponent)
        {
            const FVector ReboundDirection = (Hit.ImpactNormal + FVector::UpVector * 0.35f).GetSafeNormal();
            ForceComponent->QueueInstantMovementImpulse(ReboundDirection, FinalDamage * 25.0f);
        }
    }
}

void ADynamicCharacter::UpdateClimbingFootingState()
{
    if (!HasAuthority() || !CapsuleComponent) return;

    // Trace from below pelvis against wall to determine if footholds/surface exist
    const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, -40.0f);
    const FVector TraceEnd = TraceStart + (GetActorForwardVector() * 70.0f);

    FHitResult WallHit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ClimbingFootingTrace), false, this);
    const bool bWallFound = GetWorld()->LineTraceSingleByChannel(WallHit, TraceStart, TraceEnd, ECC_WorldStatic, Params);

    if (bWallFound != bCurrentFootingActive)
    {
        bCurrentFootingActive = bWallFound;
        ConfigureHangingPhysics(true, bCurrentFootingActive);
    }
}

void ADynamicCharacter::ConfigureHangingPhysics(bool bEnableHanging, bool bHasFooting)
{
    if (bIsHangingPhysical == bEnableHanging && bCurrentFootingActive == bHasFooting) return;
    bIsHangingPhysical = bEnableHanging;
    bCurrentFootingActive = bHasFooting;

    if (!MeshComponent || !PhysicsControlComponent) return;

    if (bEnableHanging)
    {
        MeshComponent->SetSimulatePhysics(true);

        // Hands firmly anchor to handholds
        static const TArray<FName> UpperBodyControls = { 
            FName("Control_hand_r"), FName("Control_hand_l"), FName("Control_spine_03") 
        };
        for (const FName& Control : UpperBodyControls)
        {
            FPhysicsControlMultiplier Mult;
            Mult.LinearStrengthMultiplier = FVector(2.0f);
            Mult.AngularStrengthMultiplier = 2.0f;
            Mult.LinearExtraDampingMultiplier = FVector(1.0f);
            Mult.AngularExtraDampingMultiplier = 1.0f;
            PhysicsControlComponent->SetControlMultiplier(Control, Mult);
        }

        // Spine and pelvis allow natural active ragdoll flexion
        static const TArray<FName> CoreControls = { 
            FName("Control_pelvis"), FName("Control_spine_01"), FName("Control_thigh_r"), FName("Control_thigh_l") 
        };
        for (const FName& Control : CoreControls)
        {
            FPhysicsControlMultiplier SlackMult;
            SlackMult.LinearStrengthMultiplier = bHasFooting ? FVector(0.8f) : FVector(0.0f);
            SlackMult.AngularStrengthMultiplier = bHasFooting ? 0.8f : 0.05f;
            SlackMult.LinearExtraDampingMultiplier = FVector(0.4f);
            SlackMult.AngularExtraDampingMultiplier = 0.4f;
            PhysicsControlComponent->SetControlMultiplier(Control, SlackMult);
        }

        // Feet: Planted rigidly if footing exists, otherwise physically dangle and sway under Chaos gravity
        static const TArray<FName> FootControls = { 
            FName("Control_foot_r"), FName("Control_foot_l"), FName("Control_calf_r"), FName("Control_calf_l") 
        };
        for (const FName& Control : FootControls)
        {
            FPhysicsControlMultiplier FootMult;
            if (bHasFooting) 
            {
                FootMult.LinearStrengthMultiplier = FVector(2.0f);
                FootMult.AngularStrengthMultiplier = 2.0f;
                FootMult.LinearExtraDampingMultiplier = FVector(1.0f);
                FootMult.AngularExtraDampingMultiplier = 1.0f;
            } 
            else 
            {
                FootMult.LinearStrengthMultiplier = FVector(0.0f);
                FootMult.AngularStrengthMultiplier = 0.04f;
                FootMult.LinearExtraDampingMultiplier = FVector(0.2f);
                FootMult.AngularExtraDampingMultiplier = 0.2f;
            }
            PhysicsControlComponent->SetControlMultiplier(Control, FootMult);
        }
    }
    else
    {
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
        ConfigureHangingPhysics(true, false);
    }
    else if (NewStateTag.MatchesTag(ClimbTag))
    {
        ConfigureHangingPhysics(true, true);
    }
    else if (OldStateTag.MatchesTag(HangTag) || OldStateTag.MatchesTag(ClimbTag))
    {
        ConfigureHangingPhysics(false, false);
    }
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
    GetWorld()->OverlapMultiByChannel(
        CandidateOverlaps, 
        BoxCenter, 
        FQuat::FindBetweenVectors(FVector::UpVector, (End - Start).GetSafeNormal()), 
        ECC_Pawn, 
        QueryBox, 
        BroadphaseParams
    );

    TArray<ADynamicCharacter*> RewoundCharacters;
    for (const FOverlapResult& Overlap : CandidateOverlaps)
    {
        if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(Overlap.GetActor()))
        {
            if (TargetChar != this && !RewoundCharacters.Contains(TargetChar))
            {
                TargetChar->RewindToHistoricalSnapshot(ClampedClientTimestamp);
                RewoundCharacters.Add(TargetChar);
            }
        }
    }

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ServerAuthoritativeBoneTrace), true, this);
    QueryParams.bReturnPhysicalMaterial = true;
    FHitResult ServerHit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(ServerHit, Start, End, ECC_Pawn, QueryParams);

    for (ADynamicCharacter* TargetChar : RewoundCharacters)
    {
        TargetChar->RestoreFromHistoricalSnapshot();
    }

    if (bHit)
    {
        if (ADynamicCharacter* HitCharacter = Cast<ADynamicCharacter>(ServerHit.GetActor()))
        {
            const float AuthoritativeForce = CalculateAuthoritativeStrikeForce();
            ServerProcessAuthoritativeLimbHit(HitCharacter, ServerHit.BoneName, AuthoritativeForce, 1.0f, ServerHit.ImpactPoint, ServerHit.ImpactNormal, this);
        }
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
        Snapshot.BoneTransforms.Reset();
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