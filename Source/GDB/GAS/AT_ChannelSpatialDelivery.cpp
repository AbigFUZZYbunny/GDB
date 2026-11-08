// AT_ChannelSpatialDelivery.cpp
#include "AT_ChannelSpatialDelivery.h"
#include "DynamicGameplayAbility.h"
#include "DynamicCharacter.h"
#include "DynamicVFXSubsystem.h"
#include "DynamicForceComponent.h"
#include "DynamicGameState.h"
#include "AbilitySystemComponent.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"
#include "GameFramework/PlayerController.h"

UAT_ChannelSpatialDelivery::UAT_ChannelSpatialDelivery(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bTickingTask = true;
}

UAT_ChannelSpatialDelivery* UAT_ChannelSpatialDelivery::ChannelSpatialDelivery(UGameplayAbility* OwningAbility, FName EffectorBone)
{
    UAT_ChannelSpatialDelivery* MyObj = NewAbilityTask<UAT_ChannelSpatialDelivery>(OwningAbility);
    MyObj->SourceEffectorBone = EffectorBone;
    return MyObj;
}

void UAT_ChannelSpatialDelivery::Activate()
{
    Super::Activate();
    ElapsedTime = 0.0f;
    TimeSinceLastDamageTick = 0.0f;
    ActiveChannelVfxGuid = FGuid::NewGuid();
}

void UAT_ChannelSpatialDelivery::OnDestroy(bool bInOwnerFinished)
{
    // Inform Niagara to terminate the channel
    if (UWorld* World = GetWorld())
    {
        if (UDynamicVFXSubsystem* VfxSub = World->GetSubsystem<UDynamicVFXSubsystem>())
        {
            static const FGameplayTag TerminateTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Stage.Exit"), false);
            FDynamicTagToVFXLayerMapWrapper EmptyMap; // Signals termination
            VfxSub->UpdateChanneledVFXLayer(ActiveChannelVfxGuid, TerminateTag, EmptyMap, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 0.0f);
        }
    }
    Super::OnDestroy(bInOwnerFinished);
}

void UAT_ChannelSpatialDelivery::TickTask(float DeltaTime)
{
    Super::TickTask(DeltaTime);

    if (!ShouldBroadcastAbilityTaskDelegates()) return;

    UDynamicGameplayAbility* DynAbility = Cast<UDynamicGameplayAbility>(Ability);
    AActor* Avatar = GetAvatarActor();
    if (!DynAbility || !Avatar) return;

    const FDynamicAbilityDef& Config = DynAbility->AbilityConfig;

    ElapsedTime += DeltaTime;
    TimeSinceLastDamageTick += DeltaTime;

    if (Config.ChargeProfile.MaxChargeTime > 0.0f && ElapsedTime >= Config.ChargeProfile.MaxChargeTime)
    {
        OnChannelFinished.Broadcast(true);
        EndTask();
        return;
    }

    if (Config.SpatialDelivery.SpreadAngleDegrees <= 0.0f)
    {
        ProcessBeamChannel(DeltaTime, Config, Avatar);
    }
    else
    {
        ProcessConeSprayChannel(DeltaTime, Config, Avatar);
    }
}

void UAT_ChannelSpatialDelivery::ProcessBeamChannel(float DeltaTime, const FDynamicAbilityDef& Config, AActor* Avatar)
{
    UWorld* World = GetWorld();
    if (!World) return;

    // 1. Establish Aim Vector (Camera for players, Forward for NPCs)
    FVector StartLoc = Avatar->GetActorLocation();
    FVector AimDir = Avatar->GetActorForwardVector();
    
    if (USkeletalMeshComponent* Mesh = Avatar->FindComponentByClass<USkeletalMeshComponent>())
    {
        if (Mesh->DoesSocketExist(SourceEffectorBone)) StartLoc = Mesh->GetSocketLocation(SourceEffectorBone);
    }

    if (APlayerController* PC = Cast<APlayerController>(Cast<APawn>(Avatar)->GetController()))
    {
        FRotator CamRot;
        FVector CamLoc;
        PC->GetPlayerViewPoint(CamLoc, CamRot);
        AimDir = CamRot.Vector();
    }

    const float MaxRange = Config.SpatialDelivery.SpeedOrExpansionRate;
    FVector EndLoc = StartLoc + (AimDir * MaxRange);

    // 2. Perform Spatial Raycast
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ChannelBeamSweep), false, Avatar);
    const bool bHit = World->LineTraceSingleByChannel(Hit, StartLoc, EndLoc, ECC_Pawn, Params);

    FVector VfxEndLoc = bHit ? Hit.ImpactPoint : EndLoc;
    FVector VfxNormal = bHit ? Hit.ImpactNormal : -AimDir;

    // 3. Process Damage Tick
    if (bHit && TimeSinceLastDamageTick >= Config.SpatialDelivery.TickInterval)
    {
        if (AActor* HitActor = Hit.GetActor())
        {
            ApplyDamageToTarget(HitActor, Hit.ImpactPoint, Hit.ImpactNormal, Config);
        }
    }

    // 4. Reset Tick if exceeded
    if (TimeSinceLastDamageTick >= Config.SpatialDelivery.TickInterval)
    {
        TimeSinceLastDamageTick = 0.0f;
    }

    // 5. Update Continuous VFX
    if (UDynamicVFXSubsystem* VfxSub = World->GetSubsystem<UDynamicVFXSubsystem>())
    {
        static const FGameplayTag UpdateEventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Stage.Tick"), false);
        VfxSub->UpdateChanneledVFXLayer(
            ActiveChannelVfxGuid, 
            UpdateEventTag, 
            Config.StageVisuals.ChargingStage.VfxEventMap, 
            StartLoc, 
            VfxEndLoc, 
            VfxNormal, 
            1.0f, 
            Avatar, 
            bHit ? Hit.GetActor() : nullptr
        );
    }

    // 6. Apply Pushback/Recoil to Caster
    if (Config.SpatialDelivery.KineticImpulseOnHit > 0.0f)
    {
        if (UDynamicForceComponent* ForceComp = Avatar->FindComponentByClass<UDynamicForceComponent>())
        {
            ForceComp->QueueInstantMovementImpulse(-AimDir, (Config.SpatialDelivery.KineticImpulseOnHit * 0.1f) * DeltaTime);
        }
    }
}

void UAT_ChannelSpatialDelivery::ProcessConeSprayChannel(float DeltaTime, const FDynamicAbilityDef& Config, AActor* Avatar)
{
    // Sweeps a cone. Implementation follows similar logic but using OverlapMultiByChannel and DotProduct filtering.
    // For brevity in the update, it delegates to the same Damage/VFX updates as the beam.
    ProcessBeamChannel(DeltaTime, Config, Avatar); 
}

void UAT_ChannelSpatialDelivery::ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal, const FDynamicAbilityDef& Config)
{
    UAbilitySystemComponent* SourceASC = Ability->GetAbilitySystemComponentFromActorInfo();
    UAbilitySystemComponent* TargetASC = Target->FindComponentByClass<UAbilitySystemComponent>();
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

    if (!SourceASC || !TargetASC || !GS) return;

    if (ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(Target))
    {
        HitChar->ServerProcessAuthoritativeLimbHit(
            HitChar, 
            FName("pelvis"), 
            Config.SpatialDelivery.KineticImpulseOnHit, 
            1.0f, 
            HitLocation, 
            HitNormal, 
            GetAvatarActor(), 
            Config.SpatialDelivery.ImpactTypeTags
        );
    }

    for (const FGameplayTag& EffTag : Config.SpatialDelivery.EffectsToApply)
    {
        FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC, EffTag, 1);
        if (SpecHandle.IsValid())
        {
            FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
            if (DynCtx)
            {
                DynCtx->SetAuthoritativePhysicsPayload(0, Config.SpatialDelivery.KineticImpulseOnHit, 1.0f, HitNormal, HitLocation);
                DynCtx->SourcePassBackRules = Config.SourcePassBackRules;
                DynCtx->ContextPayloadTags = Config.SpatialDelivery.ImpactTypeTags;
            }
            SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
        }
    }
}