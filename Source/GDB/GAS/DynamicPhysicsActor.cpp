// DynamicPhysicsActor.cpp
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "DynamicInteractableComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicForceComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"

ADynamicPhysicsActor::ADynamicPhysicsActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
    RootComponent = StaticMeshComponent;

    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMeshComponent"));
    SkeletalMeshComponent->SetupAttachment(RootComponent);

    PhysicsControlComponent = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControlComponent"));
    ForceComponent = CreateDefaultSubobject<UDynamicForceComponent>(TEXT("ForceComponent"));
    ForceComponent->SetIsReplicated(true);

    AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

    AttributeSet = CreateDefaultSubobject<UDynamicAttributeSet>(TEXT("DynamicAttributeSet"));
    InteractableComponent = CreateDefaultSubobject<UDynamicInteractableComponent>(TEXT("InteractableComponent"));
    ContainerComponent = CreateDefaultSubobject<UDynamicContainerComponent>(TEXT("ContainerComponent"));
    ContainerComponent->SetIsReplicated(true);
}

void ADynamicPhysicsActor::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority() && StaticMeshComponent)
    {
        StaticMeshComponent->OnComponentHit.AddUniqueDynamic(this, &ADynamicPhysicsActor::OnServerPrimitiveHit);
    }

    if (HasAuthority() && ActorArchetypeTag.IsValid())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            FGameplayTagContainer RoleTags;
            RoleTags.AddTag(ActorArchetypeTag);
            GS->RegisterSpatialRoleActor(this, RoleTags);
        }
    }
}

void ADynamicPhysicsActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            GS->UnregisterSpatialRoleActor(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void ADynamicPhysicsActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ADynamicPhysicsActor, ActorArchetypeTag);
    DOREPLIFETIME(ADynamicPhysicsActor, bIsActiveInWorld);
}

UAbilitySystemComponent* ADynamicPhysicsActor::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

UPrimitiveComponent* ADynamicPhysicsActor::GetActivePhysicsPrimitive() const
{
    if (SkeletalMeshComponent && !SkeletalMeshComponent->bHiddenInGame)
    {
        return SkeletalMeshComponent;
    }
    return StaticMeshComponent;
}

UDynamicContainerComponent* ADynamicPhysicsActor::GetContainerComponent(FGameplayTag ContainerIdentifierTag) const
{
    return ContainerComponent;
}

void ADynamicPhysicsActor::InitializeFromArchetype(FGameplayTag InArchetypeTag)
{
    if (!InArchetypeTag.IsValid()) return;
    ActorArchetypeTag = InArchetypeTag;

    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            FGameplayTagContainer RoleTags;
            RoleTags.AddTag(ActorArchetypeTag);
            GS->RegisterSpatialRoleActor(this, RoleTags);
        }
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicPhysicsActorDef* ActorDef = GS->MasterPhysicsActorRegistry.Find(ActorArchetypeTag);
    if (!ActorDef) return;

    ApplyArchetypePresentationAndTopology(*ActorDef);

    if (AttributeSet)
    {
        for (const FTagFloatPair& AttrPair : ActorDef->DefaultGlobalAttributes.ReplicatedEntries)
        {
            if (AttrPair.KeyTag.IsValid())
            {
                AttributeSet->SetDynamicAttributeValue(AttrPair.KeyTag, AttrPair.Value, 0);
            }
        }
    }

    if (ContainerComponent && ActorDef->Containers.Num() > 0)
    {
        ContainerComponent->InitializeContainerFromDef(ActorDef->Containers[0]);
    }

    ForceNetUpdate();
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
            if (!Joint.BoneName.IsNone())
            {
                LocalBoneToLimbMap.Add(Joint.BoneName, Limb.LimbId);
            }
        }
    }

    if (!Def.SkeletalMeshAsset.IsNull())
    {
        if (USkeletalMesh* SkelMesh = Def.SkeletalMeshAsset.LoadSynchronous())
        {
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
            if (Def.MassOverride > 0.0f)
            {
                SkeletalMeshComponent->SetMassOverrideInKg(NAME_None, Def.MassOverride, true);
            }
        }
        StaticMeshComponent->SetSimulatePhysics(false);
        StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        StaticMeshComponent->SetHiddenInGame(true);
    }
    else if (!Def.StaticMeshAsset.IsNull())
    {
        if (UStaticMesh* StatMesh = Def.StaticMeshAsset.LoadSynchronous())
        {
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
            if (Def.MassOverride > 0.0f)
            {
                StaticMeshComponent->SetMassOverrideInKg(NAME_None, Def.MassOverride, true);
            }
        }
        SkeletalMeshComponent->SetSimulatePhysics(false);
        SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkeletalMeshComponent->SetHiddenInGame(true);
    }
}

const FPhysicsLimbConfig* ADynamicPhysicsActor::ResolveActorLimbConfig(uint8 LimbId) const
{
    if (const FPhysicsLimbConfig* const* LocalLimbPtr = LocalLimbLookupMap.Find(LimbId))
    {
        return *LocalLimbPtr;
    }
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        return GS->GetLimbData(LimbId);
    }
    return nullptr;
}

uint8 ADynamicPhysicsActor::ResolveLimbIdForBone(const FName& BoneName) const
{
    if (const uint8* LocalId = LocalBoneToLimbMap.Find(BoneName))
    {
        return *LocalId;
    }
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        return GS->GetLimbIdForBone(BoneName);
    }
    return 1;
}

void ADynamicPhysicsActor::OnServerPrimitiveHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !bIsActiveInWorld || OtherActor == this) return;

    const float ImpulseMagnitude = NormalImpulse.Size();
    if (ImpulseMagnitude <= KINDA_SMALL_NUMBER) return;

    const FName HitBoneName = (Hit.MyBoneName != NAME_None) ? Hit.MyBoneName : Hit.BoneName;
    ServerProcessAuthoritativeLimbHit(HitBoneName, ImpulseMagnitude, 1.0f, Hit.ImpactPoint, Hit.ImpactNormal, OtherActor);
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

    if (ForceComponent)
    {
        ForceComponent->ServerApplyLimbForce(ResolvedLimbId, HitBoneName, ClampedImpulse, HitLocation, HitNormal);
    }
}

void ADynamicPhysicsActor::RecalculateLimbPhysicsMotors(uint8 LimbId, bool bCascadeToChildren)
{
}

void ADynamicPhysicsActor::ApplyJointMotorMultipliers_Implementation(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier)
{
}

void ADynamicPhysicsActor::ResetToDefaultPoolState()
{
    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            GS->UnregisterSpatialRoleActor(this);
        }
    }

    if (UPrimitiveComponent* Prim = GetActivePhysicsPrimitive())
    {
        Prim->SetSimulatePhysics(false);
        Prim->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Prim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }

    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);

    if (ContainerComponent)
    {
        ContainerComponent->ClearContainerForPooling();
    }
}

void ADynamicPhysicsActor::RestoreFromContainerSnapshot(const FDynamicContainerSlotItem& Snapshot)
{
}

void ADynamicPhysicsActor::DeactivateAndReturnToPool()
{
    if (!HasAuthority()) return;

    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        GS->ReleasePhysicsActorToPool(this);
    }
}

void ADynamicPhysicsActor::OnRep_ActorArchetypeTag()
{
}

void ADynamicPhysicsActor::OnRep_IsActiveInWorld()
{
}