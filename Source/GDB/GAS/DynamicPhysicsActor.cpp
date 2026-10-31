// DynamicPhysicsActor.cpp
#include "DynamicPhysicsActor.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicForceComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicContainerComponent.h"
#include "Engine/AssetManager.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StreamableManager.h"

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
    if (SkeletalMeshComponent && !SkeletalMeshComponent->bHiddenInGame && SkeletalMeshComponent->GetSkeletalMeshAsset() != nullptr)
    {
        return SkeletalMeshComponent;
    }
    if (StaticMeshComponent && !StaticMeshComponent->bHiddenInGame && StaticMeshComponent->GetStaticMesh() != nullptr)
    {
        return StaticMeshComponent;
    }
    return SkeletalMeshComponent;
}

void ADynamicPhysicsActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ADynamicPhysicsActor, ActorArchetypeTag);
    DOREPLIFETIME(ADynamicPhysicsActor, bIsActiveInWorld);
    DOREPLIFETIME(ADynamicPhysicsActor, ItemOwnerGuid);
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
        else if (ActorArchetypeTag.IsValid())
        {
            if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
            {
                FGameplayTagContainer RoleTags;
                RoleTags.AddTag(ActorArchetypeTag);
                GS->RegisterSpatialRoleActor(this, RoleTags);
            }
        }
    }
    else if (ActorArchetypeTag.IsValid())
    {
        OnRep_ActorArchetypeTag();
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

    // Fast-register into GameState's spatial index
    if (HasAuthority())
    {
        FGameplayTagContainer RoleTags;
        RoleTags.AddTag(ActorArchetypeTag);
        RoleTags.AppendTags(Def->InitialInteractableStateTags);
        GS->RegisterSpatialRoleActor(this, RoleTags);
    }

    bIsActiveInWorld = true;
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    ApplyArchetypePresentationAndTopology(*Def);

    if (!HasAuthority()) return;

    if (AttributeSet)
    {
        for (const FTagFloatPair& Pair : Def->DefaultGlobalAttributes.ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid()) AttributeSet->SetDynamicAttributeValue(Pair.KeyTag, Pair.Value, 0);
        }
        for (const auto& Pair : LocalLimbLookupMap)
        {
            if (Pair.Value)
            {
                for (const FTagFloatPair& AttrPair : Pair.Value->DefaultLimbAttributes.ReplicatedEntries)
                {
                    if (AttrPair.KeyTag.IsValid())
                    {
                        AttributeSet->SetDynamicAttributeValue(AttrPair.KeyTag, AttrPair.Value, Pair.Key);
                    }
                }
            }
        }
        bAttributesInitializedOnServer = true;
    }

    if (InteractableComponent)
    {
        InteractableComponent->KinematicTargetPoints = Def->KinematicTargetPoints;
        InteractableComponent->InvalidateAllKinematicCaches();
        for (const FGameplayTag& StateTag : Def->InitialInteractableStateTags)
        {
            InteractableComponent->AddStateTag(StateTag);
        }
    }

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
    ActorArchetypeTag = Def.ArchetypeTag;

    if (UPrimitiveComponent* ActivePrim = GetActivePhysicsPrimitive())
    {
        ActivePrim->SetMassOverrideInKg(NAME_None, Def.MassOverride, true);
    }

    if (!Def.PresentationAsset.IsNull())
    {
        const FSoftObjectPath AssetPath = Def.PresentationAsset.ToSoftObjectPath();
        TWeakObjectPtr<ADynamicPhysicsActor> WeakThis(this);

        UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateLambda([WeakThis, AssetPath]() {
            if (ADynamicPhysicsActor* StrongThis = WeakThis.Get())
            {
                UObject* LoadedObj = AssetPath.ResolveObject();
                if (UStaticMesh* SMesh = Cast<UStaticMesh>(LoadedObj))
                {
                    if (StrongThis->GetStaticMeshComponent())
                    {
                        StrongThis->GetStaticMeshComponent()->SetStaticMesh(SMesh);
                        StrongThis->GetStaticMeshComponent()->SetVisibility(true);
                        if (StrongThis->GetMeshComponent()) StrongThis->GetMeshComponent()->SetVisibility(false);
                    }
                }
                else if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(LoadedObj))
                {
                    if (StrongThis->GetMeshComponent())
                    {
                        StrongThis->GetMeshComponent()->SetSkeletalMeshAsset(SkelMesh);
                        StrongThis->GetMeshComponent()->SetVisibility(true);
                        if (StrongThis->GetStaticMeshComponent()) StrongThis->GetStaticMeshComponent()->SetVisibility(false);
                    }
                }
            }
        }));
    }
}

void ADynamicPhysicsActor::RestoreFromContainerSnapshot(const FDynamicContainerSlotItem& Snapshot)
{
    if (!HasAuthority()) return;

    if (AttributeSet)
    {
        for (const FDynamicAttributeItem& SavedAttr : Snapshot.SavedAttributeState)
        {
            if (SavedAttr.AttributeTag.IsValid()) AttributeSet->SetDynamicAttributeValue(SavedAttr.AttributeTag, SavedAttr.Value, SavedAttr.LimbId);
        }
    }

    if (InteractableComponent && Snapshot.SavedInteractableStates.Num() > 0)
    {
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
    
    SetItemOwnerGuid(Snapshot.OwnerGuid);
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

    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAllAbilities();
        FGameplayEffectQuery AllEffectsQuery;
        AbilitySystemComponent->RemoveActiveEffects(AllEffectsQuery);
        AbilitySystemComponent->RemoveActiveGameplayEffectBySourceEffect(nullptr, nullptr);
        FGameplayTagContainer AllTags;
        AbilitySystemComponent->GetOwnedGameplayTags(AllTags);
        AbilitySystemComponent->RemoveLooseGameplayTags(AllTags);
        AbilitySystemComponent->ClearAllAbilities();
        AbilitySystemComponent->RemoveAllGameplayCues();
    }

    if (AttributeSet)
    {
        AttributeSet->FastAttributes.Items.Empty();
        AttributeSet->FastAttributes.MarkArrayDirty();
        AttributeSet->OnRep_FastAttributes();
    }

    if (ForceComponent)
    {
        ForceComponent->ConsumePendingImpulse();
    }

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

    SetActorLocationAndRotation(FVector(0.0f, 0.0f, -50000.0f), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);

    if (ContainerComponent)
    {
        ContainerComponent->ClearContainerForPooling();
    }

    TArray<UActorComponent*> InstanceComps = GetInstanceComponents();
    for (UActorComponent* Comp : InstanceComps)
    {
        if (UDynamicContainerComponent* ExtraContainer = Cast<UDynamicContainerComponent>(Comp))
        {
            RemoveInstanceComponent(ExtraContainer);
            ExtraContainer->DestroyComponent();
        }
    }

    if (InteractableComponent)
    {
        InteractableComponent->ResetForPooling();
    }

    bAttributesInitializedOnServer = false;
    LocalLimbLookupMap.Empty();
    LocalBoneToLimbMap.Empty();
    ServerLastLimbHitTimestamp.Empty();
}

void ADynamicPhysicsActor::DeactivateAndReturnToPool()
{
    bIsActiveInWorld = false;
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
}

void ADynamicPhysicsActor::ApplyJointMotorMultipliers_Implementation(FName BoneName, uint8 JointChainIndex, float AngularSpringMultiplier, float LinearSpringMultiplier, float DampingMultiplier)
{
}