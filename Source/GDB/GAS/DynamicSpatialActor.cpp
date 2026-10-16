// DynamicSpatialActor.cpp
#include "DynamicSpatialActor.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicSpatialActor::ADynamicSpatialActor()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);

    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    CollisionSphere->InitSphereRadius(16.0f);
    RootComponent = CollisionSphere;

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->ProjectileGravityScale = 0.0f;
    ProjectileMovement->bAutoActivate = false;
}

void ADynamicSpatialActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicSpatialActor, bIsActiveInWorld);
    DOREPLIFETIME(ADynamicSpatialActor, MotionType);
    DOREPLIFETIME(ADynamicSpatialActor, ShapeBehavior);
    DOREPLIFETIME(ADynamicSpatialActor, HomingTurnRate);
    DOREPLIFETIME(ADynamicSpatialActor, HomingTarget);
}

void ADynamicSpatialActor::ActivateSpatialDelivery(
    UAbilitySystemComponent* InSourceASC,
    const FDynamicSpatialDeliveryDef& InDeliveryDef,
    const TArray<FSourcePassBackRule>& InPassBackRules,
    float InResolvedSpeed,
    float InPowerMultiplier,
    AActor* InHomingTarget)
{
    SourceASC = InSourceASC;
    DeliveryDef = InDeliveryDef;
    PassBackRules = InPassBackRules;
    PowerMultiplier = InPowerMultiplier;
    MotionType = InDeliveryDef.MotionType;
    ShapeBehavior = InDeliveryDef.ShapeBehavior;
    HomingTurnRate = InDeliveryDef.HomingTurnRate;
    HomingTarget = InHomingTarget;

    ElapsedTime = 0.0f;
    TickAccumulator = 0.0f;
    ActiveTargets.Empty();

    bIsProjectileMode = (MotionType == ESpatialDeliveryMotion::StraightVelocity ||
                         MotionType == ESpatialDeliveryMotion::SourceGuided ||
                         MotionType == ESpatialDeliveryMotion::Homing);

    CurrentRadius = (ShapeBehavior == ESpatialShapeBehavior::StayFullSize || ShapeBehavior == ESpatialShapeBehavior::ContractIn || ShapeBehavior == ESpatialShapeBehavior::PulseAtFullSize)
                    ? DeliveryDef.MaxRadius
                    : DeliveryDef.MinRadius;

    CollisionSphere->SetSphereRadius(CurrentRadius, false);

    if (bIsProjectileMode)
    {
        CollisionSphere->SetCollisionProfileName(TEXT("Projectile"));
        CollisionSphere->SetNotifyRigidBodyCollision(true);
        CollisionSphere->SetGenerateOverlapEvents(false);

        ProjectileMovement->InitialSpeed = InResolvedSpeed;
        ProjectileMovement->MaxSpeed = InResolvedSpeed;
        ProjectileMovement->Velocity = GetActorForwardVector() * InResolvedSpeed;
        ProjectileMovement->Activate(true);

        if (HasAuthority())
        {
            CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &ADynamicSpatialActor::OnSphereHit);
        }
    }
    else
    {
        ProjectileMovement->StopMovementImmediately();
        ProjectileMovement->Deactivate();

        CollisionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
        CollisionSphere->SetNotifyRigidBodyCollision(false);
        CollisionSphere->SetGenerateOverlapEvents(true);

        if (HasAuthority())
        {
            CollisionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ADynamicSpatialActor::OnSphereBeginOverlap);
            CollisionSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ADynamicSpatialActor::OnSphereEndOverlap);
        }
    }

    bIsActiveInWorld = true;
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    SetActorTickEnabled(true);
}

void ADynamicSpatialActor::DeactivateAndReturnToPool()
{
    if (HasAuthority())
    {
        // Clean up PassiveWhileIn attribute buffs if an Aura expires
        if (!bIsProjectileMode && DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && DeliveryDef.DirectTargetAttributeTag.IsValid())
        {
            for (AActor* Target : ActiveTargets)
            {
                if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(Target))
                {
                    if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
                    {
                        Attr->SetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag, Attr->GetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag) - (DeliveryDef.DirectAttributeMagnitude * PowerMultiplier));
                    }
                }
            }
        }
    }

    ActiveTargets.Empty();
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    ProjectileMovement->StopMovementImmediately();
    ProjectileMovement->Deactivate();

    bIsActiveInWorld = false;
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);

    if (HasAuthority())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            GS->ReleaseSpatialActorToPool(this);
        }
    }
}

void ADynamicSpatialActor::OnRep_IsActiveInWorld()
{
    SetActorHiddenInGame(!bIsActiveInWorld);
    SetActorEnableCollision(bIsActiveInWorld);
    SetActorTickEnabled(bIsActiveInWorld);
}

void ADynamicSpatialActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bIsActiveInWorld) return;

    ElapsedTime += DeltaTime;
    if (HasAuthority() && ElapsedTime >= DeliveryDef.Duration)
    {
        DeactivateAndReturnToPool();
        return;
    }

    if (bIsProjectileMode)
    {
        TickGuidance(DeltaTime);
    }
    else
    {
        TickShapeAndAura(DeltaTime);
    }
}

void ADynamicSpatialActor::TickGuidance(float DeltaTime)
{
    if (MotionType == ESpatialDeliveryMotion::StraightVelocity || !ProjectileMovement) return;

    FVector DesiredDir = GetActorForwardVector();
    if (MotionType == ESpatialDeliveryMotion::SourceGuided)
    {
        if (APawn* SourcePawn = GetInstigator())
        {
            DesiredDir = SourcePawn->GetControlRotation().Vector();
        }
    }
    else if (MotionType == ESpatialDeliveryMotion::Homing && HomingTarget.IsValid())
    {
        DesiredDir = (HomingTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
    }

    const FVector CurrentDir = ProjectileMovement->Velocity.GetSafeNormal();
    const FVector SteeredDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, HomingTurnRate);
    ProjectileMovement->Velocity = SteeredDir * ProjectileMovement->MaxSpeed;
}

void ADynamicSpatialActor::TickShapeAndAura(float DeltaTime)
{
    const float Alpha = FMath::Clamp(ElapsedTime / FMath::Max(0.01f, DeliveryDef.Duration), 0.0f, 1.0f);

    switch (ShapeBehavior)
    {
        case ESpatialShapeBehavior::StayFullSize:
            CurrentRadius = DeliveryDef.MaxRadius;
            break;
        case ESpatialShapeBehavior::ExpandOut:
            CurrentRadius = FMath::Lerp(DeliveryDef.MinRadius, DeliveryDef.MaxRadius, Alpha);
            break;
        case ESpatialShapeBehavior::ContractIn:
            CurrentRadius = FMath::Lerp(DeliveryDef.MaxRadius, DeliveryDef.MinRadius, Alpha);
            break;
        case ESpatialShapeBehavior::ExpandAndContract:
            CurrentRadius = FMath::Lerp(DeliveryDef.MinRadius, DeliveryDef.MaxRadius, FMath::Abs(FMath::Sin(ElapsedTime * DeliveryDef.SpeedOrExpansionRate)));
            break;
        case ESpatialShapeBehavior::PulseAtFullSize:
            CurrentRadius = DeliveryDef.MaxRadius * (0.9f + 0.1f * FMath::Sin(ElapsedTime * DeliveryDef.SpeedOrExpansionRate * 3.0f));
            break;
        default:
            break;
    }

    CollisionSphere->SetSphereRadius(CurrentRadius, true);
    if (!HasAuthority()) return;

    if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::ConstantRate && DeliveryDef.DirectTargetAttributeTag.IsValid())
    {
        const float FrameDelta = DeliveryDef.DirectAttributeMagnitude * PowerMultiplier * DeltaTime;
        for (AActor* Target : ActiveTargets)
        {
            if (ADynamicCharacter* Char = Cast<ADynamicCharacter>(Target))
            {
                if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(Char->GetDynamicAttributeSet()))
                {
                    Attr->SetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag, Attr->GetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag) + FrameDelta);
                }
            }
        }
    }
    else if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::TickInterval)
    {
        TickAccumulator += DeltaTime;
        if (TickAccumulator >= DeliveryDef.TickInterval)
        {
            TickAccumulator -= DeliveryDef.TickInterval;
            for (AActor* Target : ActiveTargets)
            {
                ApplyImpactAndEffects(Target, FName("pelvis"), Target->GetActorLocation(), FVector::UpVector);
            }
        }
    }
}

void ADynamicSpatialActor::OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !bIsActiveInWorld || !OtherActor || OtherActor == GetInstigator()) return;

    ApplyPassBackToSource(EPassBackTriggerEvent::OnCollision, 0.0f);
    const FName HitBone = (Hit.BoneName != NAME_None) ? Hit.BoneName : Hit.MyBoneName;
    ApplyImpactAndEffects(OtherActor, HitBone, Hit.ImpactPoint, Hit.ImpactNormal);

    DeactivateAndReturnToPool();
}

void ADynamicSpatialActor::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!HasAuthority() || !bIsActiveInWorld || !OtherActor || OtherActor == GetInstigator()) return;

    ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(OtherActor);
    if (!TargetChar) return;

    if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::SingleHit)
    {
        if (ActiveTargets.Contains(OtherActor)) return;
        ActiveTargets.Add(OtherActor);

        ApplyPassBackToSource(EPassBackTriggerEvent::OnCollision, 0.0f);
        const FVector OutwardNormal = (GetActorLocation() - OtherActor->GetActorLocation()).GetSafeNormal();
        const FName HitBone = (SweepResult.BoneName != NAME_None) ? SweepResult.BoneName : FName("pelvis");
        ApplyImpactAndEffects(OtherActor, HitBone, OtherActor->GetActorLocation(), OutwardNormal);
    }
    else
    {
        ActiveTargets.Add(OtherActor);
        if (DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && DeliveryDef.DirectTargetAttributeTag.IsValid())
        {
            if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
            {
                Attr->SetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag, Attr->GetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag) + (DeliveryDef.DirectAttributeMagnitude * PowerMultiplier));
            }
        }
    }
}

void ADynamicSpatialActor::OnSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!HasAuthority() || !bIsActiveInWorld || !OtherActor) return;

    if (ActiveTargets.Remove(OtherActor) > 0 && DeliveryDef.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && DeliveryDef.DirectTargetAttributeTag.IsValid())
    {
        if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(OtherActor))
        {
            if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
            {
                Attr->SetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag, Attr->GetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag) - (DeliveryDef.DirectAttributeMagnitude * PowerMultiplier));
            }
        }
    }
}

void ADynamicSpatialActor::ApplyImpactAndEffects(AActor* TargetActor, const FName& HitBone, const FVector& HitLocation, const FVector& HitNormal)
{
    ADynamicCharacter* HitCharacter = Cast<ADynamicCharacter>(TargetActor);
    UAbilitySystemComponent* InstigatorASC = SourceASC.Get();
    if (!HitCharacter || !InstigatorASC) return;

    const float ScaledImpulse = DeliveryDef.KineticImpulseOnHit * PowerMultiplier;
    if (ScaledImpulse > 0.0f)
    {
        HitCharacter->ServerProcessAuthoritativeLimbHit(HitCharacter, HitBone, ScaledImpulse, PowerMultiplier, HitLocation, HitNormal, GetInstigator());
    }

    if (DeliveryDef.DirectTargetAttributeTag.IsValid() && !FMath::IsNearlyZero(DeliveryDef.DirectAttributeMagnitude))
    {
        if (UDynamicAttributeSet* TargetAttr = const_cast<UDynamicAttributeSet*>(HitCharacter->GetDynamicAttributeSet()))
        {
            TargetAttr->SetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag, TargetAttr->GetDynamicAttributeValue(DeliveryDef.DirectTargetAttributeTag) + (DeliveryDef.DirectAttributeMagnitude * PowerMultiplier));
        }
    }

    if (UAbilitySystemComponent* TargetASC = HitCharacter->GetAbilitySystemComponent())
    {
        ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
        const uint8 LimbId = GS ? GS->GetLimbIdForBone(HitBone) : 1;

        for (const FGameplayTag& EffectTag : DeliveryDef.EffectsToApply)
        {
            FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(InstigatorASC, EffectTag, 1);
            if (SpecHandle.IsValid())
            {
                FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
                if (DynCtx)
                {
                    DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, PowerMultiplier, HitNormal, HitLocation);
                    DynCtx->SourcePassBackRules = PassBackRules;
                }
                InstigatorASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
            }
        }
    }
}

void ADynamicSpatialActor::ApplyPassBackToSource(EPassBackTriggerEvent EventType, float DamageAmount)
{
    if (!SourceASC.IsValid()) return;
    const UDynamicAttributeSet* SourceAttr = Cast<UDynamicAttributeSet>(SourceASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!SourceAttr) return;

    for (const FSourcePassBackRule& Rule : PassBackRules)
    {
        if (Rule.TriggerEvent == EventType && Rule.SourceAttributeTag.IsValid())
        {
            const float Grant = (Rule.BaseGrantAmount + (Rule.bScaleWithDamageDealt ? (DamageAmount * Rule.DamageRatioScalar) : 0.0f)) * PowerMultiplier;
            const float Cur = SourceAttr->GetDynamicAttributeValue(Rule.SourceAttributeTag);
            const_cast<UDynamicAttributeSet*>(SourceAttr)->SetDynamicAttributeValue(Rule.SourceAttributeTag, Cur + Grant);
        }
    }
}