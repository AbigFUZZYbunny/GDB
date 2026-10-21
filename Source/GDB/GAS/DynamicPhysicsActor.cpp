// DynamicPhysicsActor.cpp
#include "DynamicPhysicsActor.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicForceComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicGameplayAbility.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicPhysicsActor::ADynamicPhysicsActor(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bReplicates = true;
    SetReplicateMovement(true);

    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMeshComponent"));
    RootComponent = SkeletalMeshComponent;
    SkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    SkeletalMeshComponent->SetNotifyRigidBodyCollision(true);
    SkeletalMeshComponent->bMultiBodyOverlap = true;

    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
    StaticMeshComponent->SetupAttachment(RootComponent);
    StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StaticMeshComponent->SetHiddenInGame(true);

    PhysicsControlComponent = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControlComponent"));
    ForceComponent = CreateDefaultSubobject<UDynamicForceComponent>(TEXT("ForceComponent"));
    ForceComponent->SetIsReplicated(true);

    AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    DynamicGASManager = CreateDefaultSubobject<UDynamicGASManagerComponent>(TEXT("DynamicGASManager"));
    AttributeSet = CreateDefaultSubobject<UDynamicAttributeSet>(TEXT("DynamicAttributeSet"));
    InteractableComponent = CreateDefaultSubobject<UDynamicInteractableComponent>(TEXT("InteractableComponent"));
    ContainerComponent = CreateDefaultSubobject<UDynamicContainerComponent>(TEXT("ContainerComponent"));
}

UAbilitySystemComponent* ADynamicPhysicsActor::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

UPrimitiveComponent* ADynamicPhysicsActor::GetActivePhysicsPrimitive() const
{
    if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset() != nullptr) return SkeletalMeshComponent;
    if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() != nullptr) return StaticMeshComponent;
    return SkeletalMeshComponent;
}

void ADynamicPhysicsActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicPhysicsActor, ActorArchetypeTag);
    DOREPLIFETIME(ADynamicPhysicsActor, bIsActiveInWorld);
}

void ADynamicPhysicsActor::BeginPlay()
{
    Super::BeginPlay();
    if (AbilitySystemComponent) AbilitySystemComponent->InitAbilityActorInfo(this, this);

    if (HasAuthority())
    {
        if (SkeletalMeshComponent) SkeletalMeshComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicPhysicsActor::OnServerPrimitiveHit);
        if (StaticMeshComponent) StaticMeshComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicPhysicsActor::OnServerPrimitiveHit);

        if (ActorArchetypeTag.IsValid() && !bAttributesInitializedOnServer)
        {
            InitializeFromArchetype(ActorArchetypeTag);
        }
    }
    else if (ActorArchetypeTag.IsValid())
    {
        OnRep_ActorArchetypeTag();
    }
}

UDynamicContainerComponent* ADynamicPhysicsActor::GetContainerComponent(FGameplayTag ContainerIdentifierTag) const
{
    if (!ContainerIdentifierTag.IsValid()) return ContainerComponent;

    TArray<UActorComponent*> Comps;
    GetComponents(UDynamicContainerComponent::StaticClass(), Comps);
    for (UActorComponent* Comp : Comps)
    {
        if (UDynamicContainerComponent* Container = Cast<UDynamicContainerComponent>(Comp))
        {
            if (Container->ContainerConfig.ContainerIdentifierTag == ContainerIdentifierTag) return Container;
        }
    }
    return ContainerComponent;
}

void ADynamicPhysicsActor::InitializeFromArchetype(FGameplayTag InArchetypeTag)
{
    if (!InArchetypeTag.IsValid()) return;
    ActorArchetypeTag = InArchetypeTag;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ActorArchetypeTag);
    if (!Def) return;

    bIsActiveInWorld = true;
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);

    ApplyArchetypePresentationAndTopology(*Def);

    if (!HasAuthority()) return;

    UDynamicAttributeSet* MutableAttr = const_cast<UDynamicAttributeSet*>(AttributeSet.Get());
    if (MutableAttr)
    {
        for (const FTagFloatPair& Pair : Def->DefaultGlobalAttributes.ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid()) MutableAttr->SetDynamicAttributeValue(Pair.KeyTag, Pair.Value, 0);
        }
        
        // FIXED: Iterate over the optimized pointer map directly to initialize limb attributes
        for (const auto& Pair : LocalLimbLookupMap)
        {
            if (Pair.Value)
            {
                for (const FTagFloatPair& AttrPair : Pair.Value->DefaultLimbAttributes.ReplicatedEntries)
                {
                    if (AttrPair.KeyTag.IsValid())
                    {
                        MutableAttr->SetDynamicAttributeValue(AttrPair.KeyTag, AttrPair.Value, Pair.Key);
                    }
                }
            }
        }
        bAttributesInitializedOnServer = true;
    }

    if (InteractableComponent)
    {
        InteractableComponent->KinematicTargetPoints = Def->KinematicTargetPoints;
        for (const FGameplayTag& StateTag : Def->InitialInteractableStateTags)
        {
            InteractableComponent->AddStateTag(StateTag);
        }
    }

    // Initialize multi-containers dynamically from archetype
    bool bFirstContainer = true;
    for (const FDynamicContainerDef& ContainerDef : Def->Containers)
    {
        UDynamicContainerComponent* TargetContainer = nullptr;
        if (bFirstContainer && ContainerComponent)
        {
            TargetContainer = ContainerComponent;
            bFirstContainer = false;
        }
        else
        {
            TargetContainer = NewObject<UDynamicContainerComponent>(this);
            TargetContainer->SetIsReplicated(true);
            TargetContainer->RegisterComponent();
            AddInstanceComponent(TargetContainer);
        }
        TargetContainer->InitializeContainerFromDef(ContainerDef);
    }
}

void ADynamicPhysicsActor::ApplyArchetypePresentationAndTopology(const FDynamicPhysicsActorDef& Def)
{
    LocalLimbLookupMap.Empty(Def.CustomLimbs.Num());
    LocalBoneToLimbMap.Empty();

    for (const FPhysicsLimbConfig& Limb : Def.CustomLimbs)
    {
        LocalLimbLookupMap.Add(Limb.LimbId, &Limb);
        for (const FPhysicsJointNodeConfig& Joint : Limb.JointChain)
        {
            if (!Joint.BoneName.IsNone()) LocalBoneToLimbMap.Add(Joint.BoneName, Limb.LimbId);
        }
    }

    if (!Def.SkeletalMeshAsset.IsNull())
    {
        if (USkeletalMesh* SkelMesh = Def.SkeletalMeshAsset.LoadSynchronous())
        {
            // Detach and set active primitive as root
            if (RootComponent != SkeletalMeshComponent)
            {
                StaticMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                SkeletalMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                SetRootComponent(SkeletalMeshComponent);
                StaticMeshComponent->SetupAttachment(RootComponent);
            }

            SkeletalMeshComponent->SetSkeletalMeshAsset(SkelMesh);
            SkeletalMeshComponent->SetHiddenInGame(false);
            SkeletalMeshComponent->SetCollisionProfileName(Def.CollisionProfileName);
            SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            SkeletalMeshComponent->SetSimulatePhysics(Def.bSimulatePhysicsOnSpawn);
            if (Def.MassOverride > 0.0f) SkeletalMeshComponent->SetMassOverrideInKg(NAME_None, Def.MassOverride, true);
        }
        StaticMeshComponent->SetSimulatePhysics(false);
        StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        StaticMeshComponent->SetHiddenInGame(true);
    }
    else if (!Def.StaticMeshAsset.IsNull())
    {
        if (UStaticMesh* StatMesh = Def.StaticMeshAsset.LoadSynchronous())
        {
            // Detach and set active primitive as root
            if (RootComponent != StaticMeshComponent)
            {
                SkeletalMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                StaticMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                SetRootComponent(StaticMeshComponent);
                SkeletalMeshComponent->SetupAttachment(RootComponent);
            }

            StaticMeshComponent->SetStaticMesh(StatMesh);
            StaticMeshComponent->SetHiddenInGame(false);
            StaticMeshComponent->SetCollisionProfileName(Def.CollisionProfileName);
            StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            StaticMeshComponent->SetNotifyRigidBodyCollision(true);
            StaticMeshComponent->SetSimulatePhysics(Def.bSimulatePhysicsOnSpawn);
            if (Def.MassOverride > 0.0f) StaticMeshComponent->SetMassOverrideInKg(NAME_None, Def.MassOverride, true);
        }
        SkeletalMeshComponent->SetSimulatePhysics(false);
        SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkeletalMeshComponent->SetHiddenInGame(true);
    }
}

void ADynamicPhysicsActor::RestoreFromContainerSnapshot(const FDynamicContainerSlotItem& Snapshot)
{
    if (!HasAuthority()) return;

    if (UDynamicAttributeSet* MutableAttr = const_cast<UDynamicAttributeSet*>(AttributeSet.Get()))
    {
        for (const FDynamicAttributeItem& SavedAttr : Snapshot.SavedAttributeState)
        {
            if (SavedAttr.AttributeTag.IsValid()) MutableAttr->SetDynamicAttributeValue(SavedAttr.AttributeTag, SavedAttr.Value, SavedAttr.LimbId);
        }
    }

    if (InteractableComponent && Snapshot.SavedInteractableStates.Num() > 0)
    {
        // Copy to avoid iterator invalidation during mutation
        const FGameplayTagContainer OldTags = InteractableComponent->GetCurrentStateTags();
        for (const FGameplayTag& OldTag : OldTags)
        {
            InteractableComponent->RemoveStateTag(OldTag);
        }
        for (const FGameplayTag& SavedTag : Snapshot.SavedInteractableStates)
        {
            InteractableComponent->AddStateTag(SavedTag);
        }
    }
}

void ADynamicPhysicsActor::ResetToDefaultPoolState()
{
    // 1. Ability System Component: strip all active effects and granted abilities
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAllAbilities();
        FGameplayTagContainer AllTags;
        AbilitySystemComponent->GetOwnedGameplayTags(AllTags);
        AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(AllTags);

        // Remove loose dynamic tags
        AbilitySystemComponent->RemoveLooseGameplayTags(AllTags);

        // Clear granted ability specs
        TArray<FGameplayAbilitySpecHandle> AbilitiesToRemove;
        for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
        {
            AbilitiesToRemove.Add(Spec.Handle);
        }
        for (const FGameplayAbilitySpecHandle& Handle : AbilitiesToRemove)
        {
            AbilitySystemComponent->ClearAbility(Handle);
        }
    }

    // 2. Dynamic Attribute Set: completely empty fast array items and maps
    if (UDynamicAttributeSet* MutableAttr = const_cast<UDynamicAttributeSet*>(AttributeSet.Get()))
    {
        MutableAttr->FastAttributes.Items.Empty();
        MutableAttr->FastAttributes.MarkArrayDirty();
        MutableAttr->OnRep_FastAttributes();
    }

    // 3. Dynamic Force Component: cancel velocities and pending impulses
    if (ForceComponent)
    {
        ForceComponent->ConsumePendingImpulse();
    }

    // 4. Physical Primitives: reset transforms, linear/angular velocities, and physics controls
    if (SkeletalMeshComponent)
    {
        SkeletalMeshComponent->SetSimulatePhysics(false);
        SkeletalMeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
        SkeletalMeshComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        SkeletalMeshComponent->EmptyOverrideMaterials();
    }
    if (StaticMeshComponent)
    {
        StaticMeshComponent->SetSimulatePhysics(false);
        StaticMeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
        StaticMeshComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        StaticMeshComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        StaticMeshComponent->EmptyOverrideMaterials();
    }

    // 5. Container Component: clear slots and weight tracking
    if (ContainerComponent)
    {
        ContainerComponent->ClearContainerForPooling();
    }

    // 6. Dynamic Instance Components: destroy any extra dynamic sub-containers added at runtime
    TArray<UActorComponent*> InstanceComps = GetInstanceComponents();
    for (UActorComponent* Comp : InstanceComps)
    {
        if (UDynamicContainerComponent* ExtraContainer = Cast<UDynamicContainerComponent>(Comp))
        {
            RemoveInstanceComponent(ExtraContainer);
            ExtraContainer->DestroyComponent();
        }
    }

    // 7. Interactable Component: clear state tags and invalidate cached target component pointers
    if (InteractableComponent)
    {
        InteractableComponent->StartupStateTags.Reset();
        InteractableComponent->ReplicatedStateTags.Items.Empty();
        InteractableComponent->ReplicatedStateTags.MarkArrayDirty();
        InteractableComponent->OnRep_ReplicatedStateTags();

        for (FInteractionTargetPoint& Point : InteractableComponent->KinematicTargetPoints)
        {
            Point.InvalidateCache();
        }
    }

    bAttributesInitializedOnServer = false;
    LocalLimbLookupMap.Empty();
    LocalBoneToLimbMap.Empty();
    ServerLastLimbHitTimestamp.Empty();
}

void ADynamicPhysicsActor::DeactivateAndReturnToPool()
{
    bIsActiveInWorld = false;

    // Execute complete scrub before putting actor to sleep
    ResetToDefaultPoolState();

    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);

    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            GS->ReleasePhysicsActorToPool(this);
        }
    }
}

void ADynamicPhysicsActor::OnRep_ActorArchetypeTag()
{
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ActorArchetypeTag))
        {
            ApplyArchetypePresentationAndTopology(*Def);
        }
    }
}

void ADynamicPhysicsActor::OnRep_IsActiveInWorld()
{
    SetActorHiddenInGame(!bIsActiveInWorld);
    SetActorEnableCollision(bIsActiveInWorld);
}

const FPhysicsLimbConfig* ADynamicPhysicsActor::ResolveActorLimbConfig(uint8 LimbId) const
{
    if (const FPhysicsLimbConfig* const* LocalLimbPtr = LocalLimbLookupMap.Find(LimbId)) return *LocalLimbPtr;
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr) return GS->GetLimbData(LimbId);
    return nullptr;
}

uint8 ADynamicPhysicsActor::ResolveLimbIdForBone(const FName& BoneName) const
{
    if (const uint8* LocalId = LocalBoneToLimbMap.Find(BoneName)) return *LocalId;
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr) return GS->GetLimbIdForBone(BoneName);
    return 1;
}

void ADynamicPhysicsActor::OnServerPrimitiveHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !bIsActiveInWorld || OtherActor == this) return;
    const float ImpulseMagnitude = NormalImpulse.Size();
    if (ImpulseMagnitude <= KINDA_SMALL_NUMBER) return;
    ServerProcessAuthoritativeLimbHit(Hit.MyBoneName != NAME_None ? Hit.MyBoneName : Hit.BoneName, ImpulseMagnitude, 1.0f, Hit.ImpactPoint, Hit.ImpactNormal, OtherActor);
}

void ADynamicPhysicsActor::ServerProcessAuthoritativeLimbHit(const FName& HitBoneName, float RawImpulseMagnitude, float MassRatio, const FVector& HitLocation, const FVector& HitNormal, AActor* InstigatorActor)
{
    if (!HasAuthority()) return;
    ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!DynamicGS) return;

    const FPhysicsCalculationSettings& Settings = DynamicGS->GetCalculationSettings();
    if (RawImpulseMagnitude < Settings.MinImpulseThreshold) return;

    const uint8 ResolvedLimbId = ResolveLimbIdForBone(HitBoneName);
    const float ClampedImpulse = FMath::Min(RawImpulseMagnitude, Settings.MaxImpulseCap);
    if (ForceComponent) ForceComponent->ServerApplyLimbForce(ResolvedLimbId, HitBoneName, ClampedImpulse, HitLocation, HitNormal);
}

void ADynamicPhysicsActor::RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren)
{
    // Native hook for physics motor recalculation
}

void ADynamicPhysicsActor::ApplyJointMotorMultipliers_Implementation(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier)
{
    // Native hook for joint motor multipliers
}