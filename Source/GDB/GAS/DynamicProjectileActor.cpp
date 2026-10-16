#include "DynamicProjectileActor.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicCharacter.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"
#include "DynamicAttributeSet.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicProjectileActor::ADynamicProjectileActor()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);

    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    CollisionSphere->InitSphereRadius(16.0f);
    CollisionSphere->SetCollisionProfileName(TEXT("Projectile"));
    CollisionSphere->SetNotifyRigidBodyCollision(true);
    RootComponent = CollisionSphere;

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->ProjectileGravityScale = 0.0f;
}

void ADynamicProjectileActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicProjectileActor, GuidanceType);
    DOREPLIFETIME(ADynamicProjectileActor, GuidanceTurnRate);
    DOREPLIFETIME(ADynamicProjectileActor, HomingTarget);
}

void ADynamicProjectileActor::InitializeProjectile(
    UAbilitySystemComponent* InSourceASC,
    const FDynamicPayloadProfile& InProfile,
    const TArray<FSourcePassBackRule>& InPassBackRules,
    float InInitialSpeed,
    float InPowerMultiplier,
    AActor* InHomingTarget)
{
    SourceASC = InSourceASC;
    PayloadProfile = InProfile;
    PassBackRules = InPassBackRules;
    PowerMultiplier = InPowerMultiplier;
    GuidanceType = InProfile.GuidanceType;
    GuidanceTurnRate = InProfile.HomingTurnRate;
    HomingTarget = InHomingTarget;

    ProjectileMovement->InitialSpeed = InInitialSpeed;
    ProjectileMovement->MaxSpeed = InInitialSpeed;
    ProjectileMovement->Velocity = GetActorForwardVector() * InInitialSpeed;

    if (HasAuthority())
    {
        CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &ADynamicProjectileActor::OnServerProjectileHit);
    }
}

void ADynamicProjectileActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (GuidanceType == EProjectileGuidanceType::Straight || !ProjectileMovement) return;

    FVector DesiredDir = GetActorForwardVector();
    if (GuidanceType == EProjectileGuidanceType::SourceGuided)
    {
        // Steer along the source avatar's current control rotation
        if (APawn* SourcePawn = Cast<APawn>(GetInstigator()))
        {
            DesiredDir = SourcePawn->GetControlRotation().Vector();
        }
    }
    else if (GuidanceType == EProjectileGuidanceType::Homing && HomingTarget.IsValid())
    {
        DesiredDir = (HomingTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
    }

    const FVector CurrentDir = ProjectileMovement->Velocity.GetSafeNormal();
    const float MaxRadians = FMath::DegreesToRadians(GuidanceTurnRate) * DeltaTime;
    const FVector SteeredDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, GuidanceTurnRate);
    ProjectileMovement->Velocity = SteeredDir * ProjectileMovement->MaxSpeed;
}

void ADynamicProjectileActor::OnServerProjectileHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || !OtherActor || OtherActor == GetInstigator()) return;

    UAbilitySystemComponent* InstigatorASC = SourceASC.Get();
    ADynamicCharacter* HitCharacter = Cast<ADynamicCharacter>(OtherActor);

    // 1. Trigger Pass-Back Rules for OnCollision (e.g., Skill XP for landing a hit)
    if (InstigatorASC)
    {
        if (const UDynamicAttributeSet* SourceAttr = Cast<UDynamicAttributeSet>(InstigatorASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())))
        {
            for (const FSourcePassBackRule& Rule : PassBackRules)
            {
                if (Rule.TriggerEvent == EPassBackTriggerEvent::OnCollision && Rule.SourceAttributeTag.IsValid())
                {
                    float CurrentVal = SourceAttr->GetDynamicAttributeValue(Rule.SourceAttributeTag);
                    const_cast<UDynamicAttributeSet*>(SourceAttr)->SetDynamicAttributeValue(Rule.SourceAttributeTag, CurrentVal + (Rule.BaseGrantAmount * PowerMultiplier));
                }
            }
        }
    }

    // 2. Process Authoritative Bone Collision & Dynamic Effects on Target
    if (HitCharacter && InstigatorASC)
    {
        const FName HitBone = (Hit.BoneName != NAME_None) ? Hit.BoneName : Hit.MyBoneName;
        const float ScaledImpulse = PayloadProfile.KineticImpulseOnHit * PowerMultiplier;

        HitCharacter->ServerProcessAuthoritativeLimbHit(
            HitCharacter,
            HitBone,
            ScaledImpulse,
            PowerMultiplier,
            Hit.ImpactPoint,
            Hit.ImpactNormal,
            GetInstigator());

        if (UAbilitySystemComponent* TargetASC = HitCharacter->GetAbilitySystemComponent())
        {
            ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
            const uint8 LimbId = GS ? GS->GetLimbIdForBone(HitBone) : 1;

            for (const FGameplayTag& EffectTag : PayloadProfile.EffectsToApplyOnHit)
            {
                FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(InstigatorASC, EffectTag, 1);
                if (SpecHandle.IsValid())
                {
                    FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
                    if (DynCtx)
                    {
                        DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, PowerMultiplier, Hit.ImpactNormal, Hit.ImpactPoint);
                        DynCtx->SourcePassBackRules = PassBackRules;
                    }
                    InstigatorASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                }
            }
        }
    }

    Destroy();
}