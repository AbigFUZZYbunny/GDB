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
#include "EngineUtils.h"
#include "DynamicCustomizationComponent.h"
#include "Engine/OverlapResult.h"

ADynamicCharacter::ADynamicCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bReplicates = true;
    bIsRagdolling = false;
    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
    RootComponent = CapsuleComponent;
    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(RootComponent);

    CustomizationComponent = CreateDefaultSubobject<UDynamicCustomizationComponent>(TEXT("CustomizationComponent"));
    MoverComponent = CreateDefaultSubobject<UMoverComponent>(TEXT("MoverComponent"));
    PhysicsControlComponent = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControlComponent"));
    ForceComponent = CreateDefaultSubobject<UDynamicForceComponent>(TEXT("ForceComponent"));
    ForceComponent->SetIsReplicated(true);
    AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    DynamicGASManager = CreateDefaultSubobject<UDynamicGASManagerComponent>(TEXT("DynamicGASManager"));
    AttributeSet = CreateDefaultSubobject<UDynamicAttributeSet>(TEXT("DynamicAttributeSet"));

    MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->bMultiBodyOverlap = true;

    // Pre-allocate the fixed ring buffer memory once at construction
    TransformHistoryBuffer.SetNum(HistoryRingBufferSize);
    HistoryBufferWriteIndex = 0;
    HistoryBufferCount = 0;
}

UAbilitySystemComponent* ADynamicCharacter::GetAbilitySystemComponent() const { return AbilitySystemComponent; }

void ADynamicCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority() && MeshComponent)
    {
        MeshComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicCharacter::OnServerSkeletalMeshHit);
    }

    // Fix B: Autonomous registration into the World Director Subsystem
    if (HasAuthority() && !IsPlayerControlled())
    {
        if (UWorld* World = GetWorld())
        {
            if (UDynamicWorldDirectorSubsystem* Director = World->GetSubsystem<UDynamicWorldDirectorSubsystem>())
            {
                Director->RegisterActiveNPC(this);
            }
        }
    }
}

void ADynamicCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Record server-authoritative history snapshot in place (O(1) circular ring write, zero allocations)
    if (HasAuthority() && CapsuleComponent && MeshComponent)
    {
        const double CurrentTime = GetWorld()->GetTimeSeconds();
        FCharacterTransformHistorySnapshot& Snapshot = TransformHistoryBuffer[HistoryBufferWriteIndex];
        Snapshot.ServerTimestamp = CurrentTime;
        Snapshot.CapsuleLocation = CapsuleComponent->GetComponentLocation();
        Snapshot.CapsuleRotation = CapsuleComponent->GetComponentQuat();

        // Retain map capacity to prevent heap reallocation
        Snapshot.BoneTransforms.Reset();

        static const TArray<FName> CriticalBones = { FName("pelvis"), FName("spine_01"), FName("head"), FName("hand_r"), FName("hand_l") };
        for (const FName& Bone : CriticalBones)
        {
            if (MeshComponent->DoesSocketExist(Bone))
            {
                Snapshot.BoneTransforms.Add(Bone, MeshComponent->GetSocketTransform(Bone, RTS_World));
            }
        }

        // Advance circular head index and count
        HistoryBufferWriteIndex = (HistoryBufferWriteIndex + 1) % HistoryRingBufferSize;
        HistoryBufferCount = FMath::Min(HistoryBufferCount + 1, HistoryRingBufferSize);
    }
}

void ADynamicCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitializeAbilitySystem();
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

    // Optimization C: Direct access without const_cast
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
    if (MeshComponent && PhysicsControlComponent)
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
        MeshComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        MeshComponent->SetCollisionProfileName(TEXT("CharacterMesh"));
    }
    if (MoverComponent) MoverComponent->Activate(true);
}

float ADynamicCharacter::CalculateAuthoritativeStrikeForce() const
{
    float Force = 1000.0f;

    // 1. Attribute scaling
    if (AttributeSet)
    {
        static const FGameplayTag StrengthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stat.Strength"), false);
        if (StrengthTag.IsValid())
        {
            Force += AttributeSet->GetDynamicAttributeValue(StrengthTag, 0) * 20.0f;
        }
    }

    // 2. Weapon mass scaling
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

    // Locate closest historical frame
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
        
        // If Active Ragdoll or physics simulation is enabled, synchronize root physics body
        if (MeshComponent && MeshComponent->IsSimulatingPhysics())
        {
            MeshComponent->SetAllPhysicsPosition(Snapshot.CapsuleLocation);
            MeshComponent->SetAllPhysicsRotation(Snapshot.CapsuleRotation.Rotator());
        }
    }
}

void ADynamicCharacter::RestoreFromHistoricalSnapshot()
{
    if (!bIsCurrentlyRewound) return;
    CapsuleComponent->SetWorldLocationAndRotation(PreRewindCachedTransform.CapsuleLocation, PreRewindCachedTransform.CapsuleRotation, false, nullptr, ETeleportType::TeleportPhysics);
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

    // 1. Broadphase: Query characters within the trace bounding cylinder
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

    // 2. Fix F: Complex line trace against the rewound skeletal geometry
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ServerAuthoritativeBoneTrace), true, this);
    QueryParams.bReturnPhysicalMaterial = true;

    FHitResult ServerHit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(ServerHit, Start, End, ECC_Pawn, QueryParams);

    // 3. Instantly restore characters to their present authoritative positions
    for (ADynamicCharacter* TargetChar : RewoundCharacters)
    {
        TargetChar->RestoreFromHistoricalSnapshot();
    }

    // 4. Resolve hit using authoritative computed strike force and specific bone
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

    // 1. Evaluate Data-Driven Structural Integrity Ratio
    float IntegrityRatio = 1.0f;
    if (LimbConfig->IntegrityAttributeTag.IsValid() && LimbConfig->MaxIntegrityAttributeTag.IsValid())
    {
        const float MaxIntegrity = FMath::Max(1.0f, AttributeSet->GetDynamicAttributeValue(LimbConfig->MaxIntegrityAttributeTag, LimbId));
        IntegrityRatio = FMath::Clamp(AttributeSet->GetDynamicAttributeValue(LimbConfig->IntegrityAttributeTag, LimbId) / MaxIntegrity, 0.0f, 1.0f);
    }

    // 2. Evaluate Data-Driven Endurance / Stamina Ratio
    float EnduranceRatio = 1.0f;
    if (LimbConfig->EnduranceAttributeTag.IsValid() && LimbConfig->MaxEnduranceAttributeTag.IsValid())
    {
        const float MaxEndurance = FMath::Max(1.0f, AttributeSet->GetDynamicAttributeValue(LimbConfig->MaxEnduranceAttributeTag, LimbId));
        EnduranceRatio = FMath::Clamp(AttributeSet->GetDynamicAttributeValue(LimbConfig->EnduranceAttributeTag, LimbId) / MaxEndurance, 0.15f, 1.0f);
    }

    // 3. Evaluate Parent Limb Integrity
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

    // 4. Accumulate Angular & Linear Motor Multipliers from arbitrary JSON-configured attribute tags
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
    
    // Linear strength and damping are 3D vectors (X, Y, Z)
    Multiplier.LinearStrengthMultiplier = FVector(LinearSpringMultiplier);
    Multiplier.LinearExtraDampingMultiplier = FVector(DampingMultiplier);
    
    // Angular strength and damping are scalar floats
    Multiplier.AngularStrengthMultiplier = AngularSpringMultiplier;
    Multiplier.AngularExtraDampingMultiplier = DampingMultiplier;

    PhysicsControlComponent->SetControlMultiplier(ControlName, Multiplier);
}

void ADynamicCharacter::AlignLimbToInteractionTarget_Implementation(FName EffectorBoneName, const FTransform& TargetWorldTransform, USceneComponent* TargetComponent)
{
    // Native hook for IK / Physics effector alignment
}

void ADynamicCharacter::ResetToDefaultPoolState()
{
    // 1. Purge GAS Effects and Granted Abilities
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

    // 2. Physical & Mover recovery
    RecoverFromRagdoll();
    if (ForceComponent)
    {
        ForceComponent->ConsumePendingImpulse();
    }

    // Optimization B: Deactivate MoverComponent to stop WorldTickGroup simulation while in pool
    if (MoverComponent)
    {
        MoverComponent->Deactivate();
    }

    // 3. Dismantle equipped gear and containers without dropping physical actors into the world
    if (UDynamicEquipmentComponent* EquipComp = FindComponentByClass<UDynamicEquipmentComponent>())
    {
        EquipComp->ClearEquipmentForPooling();
    }

    if (UDynamicContainerComponent* Container = FindComponentByClass<UDynamicContainerComponent>())
    {
        Container->ClearContainerForPooling();
    }

    // 4. Reset Lag Compensation Ring Buffer
    HistoryBufferWriteIndex = 0;
    HistoryBufferCount = 0;
    for (FCharacterTransformHistorySnapshot& Snapshot : TransformHistoryBuffer)
    {
        Snapshot.ServerTimestamp = 0.0;
        Snapshot.BoneTransforms.Reset();
    }
    bIsCurrentlyRewound = false;
    ServerLastLimbHitTimestamp.Empty();

    // 5. Reset Customization morphs & colors
    if (UDynamicCustomizationComponent* Cust = GetCustomizationComponent())
    {
        Cust->ReplicatedMorphs.Items.Empty();
        Cust->ReplicatedMorphs.MarkArrayDirty();
        Cust->OnRep_ReplicatedMorphs();
    }

    // 6. Reset Interactable component if attached
    if (UDynamicInteractableComponent* Interactable = FindComponentByClass<UDynamicInteractableComponent>())
    {
        Interactable->ResetForPooling();
    }
}