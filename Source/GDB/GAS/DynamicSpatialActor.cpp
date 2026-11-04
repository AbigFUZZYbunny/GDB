// DynamicSpatialActor.cpp
#include "DynamicSpatialActor.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicEffectFactory.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"

ADynamicSpatialActor::ADynamicSpatialActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    bReplicates = true;
    SetReplicateMovement(true);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpatialMesh"));
    RootComponent = MeshComponent;
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetSimulatePhysics(false);
    MeshComponent->SetHiddenInGame(true);
}

void ADynamicSpatialActor::BeginPlay()
{
    Super::BeginPlay();
}

void ADynamicSpatialActor::ClearAllPassiveEffects()
{
    for (auto& Pair : ActivePassiveEffects)
    {
        if (UAbilitySystemComponent* TargetASC = Pair.Key.Get())
        {
            for (const FActiveGameplayEffectHandle& Handle : Pair.Value)
            {
                if (Handle.IsValid())
                {
                    TargetASC->RemoveActiveGameplayEffect(Handle);
                }
            }
        }
    }
    ActivePassiveEffects.Empty();
}

void ADynamicSpatialActor::ResetToDefaultPoolState()
{
    bIsActive = false;
    SetActorTickEnabled(false);
    CurrentSpeed = 0.0f;
    PowerMultiplier = 1.0f;
    ElapsedLifetime = 0.0f;
    CurrentVelocity = FVector::ZeroVector;

    ClearAllPassiveEffects();

    SourceASC.Reset();
    HomingTargetActor.Reset();
    ImpactedActors.Empty();
    ActorTickTimers.Empty();
    PassBackRules.Empty();
    DeliveryDef = FDynamicSpatialDeliveryDef();

    if (MeshComponent)
    {
        MeshComponent->SetHiddenInGame(true);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent->SetSimulatePhysics(false);
        MeshComponent->SetRelativeScale3D(FVector(1.0f));
        MeshComponent->EmptyOverrideMaterials();
    }

    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}

void ADynamicSpatialActor::ActivateSpatialDelivery(
    UAbilitySystemComponent* InSourceASC,
    const FDynamicSpatialDeliveryDef& InDeliveryDef,
    const TArray<FSourcePassBackRule>& InPassBackRules,
    float InSpeed,
    float InPowerMultiplier,
    FVector InDirection,
    AActor* InHomingTarget)
{
    ResetToDefaultPoolState();

    SourceASC = InSourceASC;
    DeliveryDef = InDeliveryDef;
    PassBackRules = InPassBackRules;
    CurrentSpeed = InSpeed;
    PowerMultiplier = InPowerMultiplier;
    HomingTargetActor = InHomingTarget;
    CurrentVelocity = InDirection.GetSafeNormal() * CurrentSpeed;

    bIsActive = true;
    ElapsedLifetime = 0.0f;
    SetActorHiddenInGame(false);
    SetActorTickEnabled(true);

    if (MeshComponent)
    {
        MeshComponent->SetHiddenInGame(false);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
}

void ADynamicSpatialActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bIsActive || !HasAuthority()) return;

    ElapsedLifetime += DeltaSeconds;
    if (ElapsedLifetime >= DeliveryDef.Duration && DeliveryDef.Duration > 0.0f)
    {
        DeactivateAndReturnToPool();
        return;
    }

    // 1. Evaluate Trajectory Physics
    if (DeliveryDef.MotionType == ESpatialDeliveryMotion::StraightVelocity)
    {
        AddActorWorldOffset(CurrentVelocity * DeltaSeconds, true);
    }
    else if (DeliveryDef.MotionType == ESpatialDeliveryMotion::Ballistic)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            CurrentVelocity.Z += (World->GetGravityZ() * DeliveryDef.GravityScale * DeltaSeconds);
            FHitResult MoveHit;
            AddActorWorldOffset(CurrentVelocity * DeltaSeconds, true, &MoveHit);
            if (MoveHit.bBlockingHit)
            {
                if (DeliveryDef.bDetonateOnBounce) ElapsedLifetime = DeliveryDef.Duration; 
                else CurrentVelocity = FMath::GetReflectionVector(CurrentVelocity, MoveHit.ImpactNormal) * DeliveryDef.Bounciness;
            }
        }
    }
    else if (DeliveryDef.MotionType == ESpatialDeliveryMotion::SourceGuided && HomingTargetActor.IsValid())
    {
        const FVector ToTarget = (HomingTargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
        CurrentVelocity = FMath::VInterpTo(CurrentVelocity, ToTarget * CurrentSpeed, DeltaSeconds, DeliveryDef.HomingTurnRate);
        SetActorRotation(CurrentVelocity.Rotation());
        AddActorWorldOffset(CurrentVelocity * DeltaSeconds, true);
    }
    else if (DeliveryDef.MotionType == ESpatialDeliveryMotion::Homing && HomingTargetActor.IsValid())
    {
        const FVector ToTarget = (HomingTargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
        CurrentVelocity = FMath::VInterpTo(CurrentVelocity, ToTarget * CurrentSpeed, DeltaSeconds, DeliveryDef.HomingTurnRate);
        SetActorRotation(CurrentVelocity.Rotation());
        AddActorWorldOffset(CurrentVelocity * DeltaSeconds, true);
    }

    // 2. Evaluate Dynamic Shape Expansion
    float ActiveRadius = DeliveryDef.MinRadius;
    if (DeliveryDef.ShapeBehavior == ESpatialShapeBehavior::ExpandOut)
    {
        const float Alpha = FMath::Clamp(ElapsedLifetime / FMath::Max(0.01f, DeliveryDef.Duration), 0.0f, 1.0f);
        ActiveRadius = FMath::Lerp(DeliveryDef.MinRadius, DeliveryDef.MaxRadius, Alpha);
    }
    else if (DeliveryDef.ShapeBehavior == ESpatialShapeBehavior::PulseAtFullSize)
    {
        ActiveRadius = DeliveryDef.MaxRadius;
    }

    if (MeshComponent)
    {
        MeshComponent->SetWorldScale3D(FVector(ActiveRadius / 50.0f)); // Assuming base sphere is 100 units
    }

    // 3. Handle Overlaps & Validations
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<FOverlapResult> Overlaps;
    FCollisionShape SphereShape = FCollisionShape::MakeSphere(ActiveRadius);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpatialActorOverlap), false, this);
    if (GetInstigator()) QueryParams.AddIgnoredActor(GetInstigator());

    TSet<TWeakObjectPtr<AActor>> CurrentFrameOverlaps;

    if (World->OverlapMultiByChannel(Overlaps, GetActorLocation(), FQuat::Identity, ECC_Pawn, SphereShape, QueryParams))
    {
        ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
        for (const FOverlapResult& Result : Overlaps)
        {
            AActor* HitActor = Result.GetActor();
            if (!HitActor) continue;

            UAbilitySystemComponent* TargetASC = HitActor->FindComponentByClass<UAbilitySystemComponent>();
            if (!TargetASC) continue;

            FGameplayTagContainer TargetTags;
            TargetASC->GetOwnedGameplayTags(TargetTags);

            static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), false);
            static const FGameplayTag InvulnerableTag = FGameplayTag::RequestGameplayTag(TEXT("State.Invulnerable"), false);

            if (DeadTag.IsValid() && TargetTags.HasTag(DeadTag)) continue;
            if (InvulnerableTag.IsValid() && TargetTags.HasTag(InvulnerableTag)) continue;

            if (DeliveryDef.BlockedTargetTags.Num() > 0 && TargetTags.HasAny(DeliveryDef.BlockedTargetTags)) continue;
            if (DeliveryDef.RequiredTargetTags.Num() > 0 && !TargetTags.HasAll(DeliveryDef.RequiredTargetTags)) continue;

            CurrentFrameOverlaps.Add(HitActor);

            bool bShouldApplyEffects = false;

            // Application Logic evaluation
            if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::SingleHit)
            {
                if (ImpactedActors.Contains(HitActor)) continue;
                ImpactedActors.Add(HitActor);
                bShouldApplyEffects = true;
            }
            else if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::TickInterval)
            {
                float& Timer = ActorTickTimers.FindOrAdd(HitActor);
                Timer += DeltaSeconds;
                if (Timer >= DeliveryDef.TickInterval)
                {
                    Timer = 0.0f;
                    bShouldApplyEffects = true;
                }
            }
            else if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn)
            {
                if (!ActivePassiveEffects.Contains(TargetASC))
                {
                    bShouldApplyEffects = true;
                }
            }

            if (bShouldApplyEffects)
            {
                ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(HitActor);
                const FName BoneName = FName("pelvis"); 
                const FVector OutwardNormal = (GetActorLocation() - HitActor->GetActorLocation()).GetSafeNormal();
                const float ScaledImpulse = DeliveryDef.KineticImpulseOnHit * PowerMultiplier;

                FGameplayTagContainer ProjectileImpactTags = DeliveryDef.ImpactTypeTags;
                if (ProjectileImpactTags.IsEmpty())
                {
                    static const FGameplayTag ImpactRootTag = FGameplayTag::RequestGameplayTag(TEXT("Impact.Type"), false);
                    for (const FGameplayTag& Tag : DeliveryDef.PropagatedTriggerTags)
                    {
                        if (ImpactRootTag.IsValid() && Tag.MatchesTag(ImpactRootTag))
                        {
                            ProjectileImpactTags.AddTag(Tag);
                        }
                    }
                }
                if (ProjectileImpactTags.IsEmpty())
                {
                    ProjectileImpactTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Impact.Type.Physical.Blunt"), false));
                }

                // Apply physics impulse (TickInterval and SingleHit only; pure Passives shouldn't shove constantly)
                if (HitChar && GS && DeliveryDef.ApplicationType != EAuraEffectApplicationType::PassiveWhileIn)
                {
                    HitChar->ServerProcessAuthoritativeLimbHit(
                        HitChar, 
                        BoneName, 
                        ScaledImpulse, 
                        PowerMultiplier, 
                        HitActor->GetActorLocation(), 
                        OutwardNormal, 
                        GetInstigator(), 
                        ProjectileImpactTags
                    );
                }

                if (SourceASC.IsValid())
                {
                    TArray<FActiveGameplayEffectHandle> AppliedHandles;
                    const uint8 LimbId = GS ? GS->GetLimbIdForBone(BoneName) : 0;

                    for (const FGameplayTag& EffTag : DeliveryDef.EffectsToApply)
                    {
                        FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC.Get(), EffTag, 1);
                        if (SpecHandle.IsValid())
                        {
                            FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
                            if (DynCtx)
                            {
                                DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, PowerMultiplier, OutwardNormal, HitActor->GetActorLocation());
                                DynCtx->SourcePassBackRules = PassBackRules;
                                DynCtx->ContextPayloadTags = ProjectileImpactTags;
                            }
                            
                            FActiveGameplayEffectHandle ActiveHandle = SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                            
                            if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn)
                            {
                                AppliedHandles.Add(ActiveHandle);
                            }
                        }
                    }

                    if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn)
                    {
                        ActivePassiveEffects.Add(TargetASC, AppliedHandles);
                    }
                }

                if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::SingleHit)
                {
                    DeactivateAndReturnToPool();
                    return;
                }
            }
        }
    }

    // 4. Cleanup Passives and Timers for actors that exited the Aura
    if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::TickInterval)
    {
        for (auto It = ActorTickTimers.CreateIterator(); It; ++It)
        {
            if (!CurrentFrameOverlaps.Contains(It.Key()))
            {
                It.RemoveCurrent();
            }
        }
    }
    else if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn)
    {
        for (auto It = ActivePassiveEffects.CreateIterator(); It; ++It)
        {
            UAbilitySystemComponent* TargetASC = It.Key().Get();
            if (!TargetASC || !CurrentFrameOverlaps.Contains(TargetASC->GetOwner()))
            {
                if (TargetASC)
                {
                    for (const FActiveGameplayEffectHandle& Handle : It.Value())
                    {
                        if (Handle.IsValid()) TargetASC->RemoveActiveGameplayEffect(Handle);
                    }
                }
                It.RemoveCurrent();
            }
        }
    }
}

void ADynamicSpatialActor::DeactivateAndReturnToPool()
{
    ResetToDefaultPoolState();
    SetActorHiddenInGame(true);

    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            GS->ReleaseSpatialActorToPool(this);
        }
    }
}