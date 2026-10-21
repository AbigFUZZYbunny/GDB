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

void ADynamicSpatialActor::ResetToDefaultPoolState()
{
    bIsActive = false;
    SetActorTickEnabled(false);
    CurrentSpeed = 0.0f;
    PowerMultiplier = 1.0f;
    ElapsedLifetime = 0.0f;

    SourceASC.Reset();
    HomingTargetActor.Reset();
    ImpactedActors.Empty();
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
    AActor* InHomingTarget)
{
    ResetToDefaultPoolState();

    SourceASC = InSourceASC;
    DeliveryDef = InDeliveryDef;
    PassBackRules = InPassBackRules;
    CurrentSpeed = InSpeed;
    PowerMultiplier = InPowerMultiplier;
    HomingTargetActor = InHomingTarget;

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

    // 1. Handle Motion
    if (DeliveryDef.MotionType == ESpatialDeliveryMotion::StraightVelocity)
    {
        const FVector MoveDelta = GetActorForwardVector() * CurrentSpeed * DeltaSeconds;
        AddActorWorldOffset(MoveDelta, true);
    }

    // 2. Handle Overlap and Liveness Validation Checks for Area of Effect / Projectiles
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<FOverlapResult> Overlaps;
    FCollisionShape SphereShape = FCollisionShape::MakeSphere(DeliveryDef.MinRadius);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpatialActorOverlap), false, this);
    if (GetInstigator())
    {
        QueryParams.AddIgnoredActor(GetInstigator());
    }

    if (World->OverlapMultiByChannel(Overlaps, GetActorLocation(), FQuat::Identity, ECC_Pawn, SphereShape, QueryParams))
    {
        ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
        for (const FOverlapResult& Result : Overlaps)
        {
            AActor* HitActor = Result.GetActor();
            if (!HitActor || ImpactedActors.Contains(HitActor)) continue;

            // Enforce Target Liveness & Faction Tag Filters
            if (SourceASC.IsValid())
            {
                // Fixed: Non-const UAbilitySystemComponent pointer for target application
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

                ImpactedActors.Add(HitActor);

                ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(HitActor);
                const FName BoneName = FName("pelvis"); // Fixed: FOverlapResult does not expose MyBoneName
                const FVector OutwardNormal = (GetActorLocation() - HitActor->GetActorLocation()).GetSafeNormal();
                const float ScaledImpulse = DeliveryDef.KineticImpulseOnHit * PowerMultiplier;

                if (HitChar && GS)
                {
                    const uint8 LimbId = GS->GetLimbIdForBone(BoneName);
                    HitChar->ServerProcessAuthoritativeLimbHit(HitChar, BoneName, ScaledImpulse, PowerMultiplier, HitActor->GetActorLocation(), OutwardNormal, GetInstigator());
                }

                for (const FGameplayTag& EffTag : DeliveryDef.EffectsToApply)
                {
                    FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC.Get(), EffTag, 1);
                    if (SpecHandle.IsValid())
                    {
                        FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
                        if (DynCtx && GS)
                        {
                            const uint8 LimbId = GS->GetLimbIdForBone(BoneName);
                            DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, PowerMultiplier, OutwardNormal, HitActor->GetActorLocation());
                            DynCtx->SourcePassBackRules = PassBackRules;
                        }
                        SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
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