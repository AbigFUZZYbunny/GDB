// DynamicGameplayAbility.cpp
#include "DynamicGameplayAbility.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "AT_WaitChargeTime.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DynamicCharacter.h"
#include "DynamicForceComponent.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "DynamicSpatialActor.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"

UDynamicGameplayAbility::UDynamicGameplayAbility()
{
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UDynamicGameplayAbility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicGameplayAbility, AbilityConfig);
    DOREPLIFETIME(UDynamicGameplayAbility, CustomSpecChainTags);
}

void UDynamicGameplayAbility::SetCustomSpecChain(const TArray<FGameplayTag>& InChainTags)
{
    CustomSpecChainTags = InChainTags;
    CurrentChainStageIndex = 0;
    if (CustomSpecChainTags.Num() > 0 && GetWorld())
    {
        if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicAbilityDef* Def = GS->AbilityMasterRegistry.Find(CustomSpecChainTags[0]))
            {
                AbilityConfig = *Def;
            }
        }
    }
}

bool UDynamicGameplayAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags)) return false;
    FGameplayTag TargetTag = (CurrentPhase == EValidationPhase::Attempt) ? AbilityConfig.LifeCycleRequirements.AttemptCooldownConditionTag : AbilityConfig.LifeCycleRequirements.ReleaseCooldownConditionTag;
    return EvaluateDynamicRule(TargetTag, ActorInfo);
}

bool UDynamicGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags)) return false;
    FGameplayTag TargetTag;
    switch (CurrentPhase)
    {
        case EValidationPhase::Attempt:  TargetTag = AbilityConfig.LifeCycleRequirements.AttemptCostConditionTag; break;
        case EValidationPhase::Charging: TargetTag = AbilityConfig.LifeCycleRequirements.ChargingCostConditionTag; break;
        case EValidationPhase::Release:  TargetTag = AbilityConfig.LifeCycleRequirements.ReleaseCostConditionTag; break;
    }
    return EvaluateDynamicRule(TargetTag, ActorInfo);
}

bool UDynamicGameplayAbility::EvaluateDynamicRule(const FGameplayTag& RuleTag, const FGameplayAbilityActorInfo* ActorInfo) const
{
    if (!RuleTag.IsValid()) return true;
    const FDynamicConditionTreeContainer* ValidationTree = UDynamicGASManagerComponent::GetConditionTreeByName(ActorInfo->OwnerActor.Get(), RuleTag.GetTagName());
    if (!ValidationTree) return true;
    UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    FGameplayTagContainer SourceTags; FGameplayTagContainer TargetTags;
    if (ASC) ASC->GetOwnedGameplayTags(TargetTags);
    return UDynamicLogicEvaluator::EvaluateConditionTree(*ValidationTree, ASC, ASC, SourceTags, TargetTags);
}

void UDynamicGameplayAbility::PlayStagePresentation(const FAbilityStagePresentation& StageVisual) const
{
    ADynamicCharacter* AvatarChar = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());
    if (!AvatarChar) return;

    if (USkeletalMeshComponent* MeshComp = AvatarChar->GetMeshComponent())
    {
        if (!StageVisual.StageMontage.IsNull())
        {
            if (UAnimMontage* Montage = StageVisual.StageMontage.LoadSynchronous())
            {
                if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                {
                    AnimInst->Montage_Play(Montage, StageVisual.PlayRate);
                    if (StageVisual.MontageSectionName != NAME_None)
                    {
                        AnimInst->Montage_JumpToSection(StageVisual.MontageSectionName, Montage);
                    }
                }
            }
        }
    }

    if (StageVisual.StageVfxCueTag.IsValid())
    {
        if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
        {
            ASC->ExecuteGameplayCue(StageVisual.StageVfxCueTag);
        }
    }
}

void UDynamicGameplayAbility::GrantSourcePassBackOnCast() const
{
    if (!HasAuthority(&CurrentActivationInfo)) return;
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    const UDynamicAttributeSet* SourceAttr = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
    if (!SourceAttr) return;

    for (const FSourcePassBackRule& Rule : AbilityConfig.SourcePassBackRules)
    {
        if (Rule.TriggerEvent == EPassBackTriggerEvent::OnCast && Rule.SourceAttributeTag.IsValid())
        {
            float Cur = SourceAttr->GetDynamicAttributeValue(Rule.SourceAttributeTag);
            const_cast<UDynamicAttributeSet*>(SourceAttr)->SetDynamicAttributeValue(Rule.SourceAttributeTag, Cur + Rule.BaseGrantAmount);
        }
    }
}

void UDynamicGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    CurrentPhase = EValidationPhase::Attempt;
    RuntimeTrackedChargeTime = 0.0f;
    CurrentChainStageIndex = 0;

    if (CustomSpecChainTags.Num() > 0 && GetWorld())
    {
        if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicAbilityDef* BaseDef = GS->AbilityMasterRegistry.Find(CustomSpecChainTags[0]))
            {
                AbilityConfig = *BaseDef;
            }
        }
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    bServerApprovedActivation = true;
    PlayStagePresentation(AbilityConfig.StageVisuals.AttemptStage);
    GrantSourcePassBackOnCast();

    switch (AbilityConfig.ExecutionType)
    {
        case EAbilityExecutionType::Charging:
        {
            PlayStagePresentation(AbilityConfig.StageVisuals.ChargingStage);
            UAT_WaitChargeTime* ChargeTask = UAT_WaitChargeTime::WaitChargeTime(this, AbilityConfig.ChargeProfile.MaxChargeTime);
            if (ChargeTask)
            {
                ChargeTask->OnChargeTick.AddDynamic(this, &UDynamicGameplayAbility::HandleTickDrain);
                ChargeTask->OnMaxChargeReached.AddDynamic(this, &UDynamicGameplayAbility::HandleMaxChargeReached);
                ChargeTask->ReadyForActivation();
            }
            break;
        }
        case EAbilityExecutionType::SpatialDelivery:
            ExecuteSpatialDelivery(1.0f);
            EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
            break;
        case EAbilityExecutionType::Chain:
            ExecuteChainBehavior(TriggerEventData);
            break;
        case EAbilityExecutionType::Movement:
            ExecuteMovementBehavior();
            break;
        default:
            EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
            break;
    }
}

void UDynamicGameplayAbility::HandleTickDrain(float TotalElapsedTime, float DeltaTime)
{
    RuntimeTrackedChargeTime = TotalElapsedTime;
    if (AbilityConfig.LifeCycleRequirements.bApplyChargeCostSmoothly)
    {
        CurrentPhase = EValidationPhase::Charging;
        if (!CheckCost(CurrentSpecHandle, CurrentActorInfo, nullptr))
        {
            CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
        }
    }
}

void UDynamicGameplayAbility::HandleMaxChargeReached()
{
    if (CustomSpecChainTags.IsValidIndex(CurrentChainStageIndex + 1))
    {
        CurrentChainStageIndex++;
        FGameplayTag NextAbilityTag = CustomSpecChainTags[CurrentChainStageIndex];
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            if (const FDynamicAbilityDef* NextDef = GS->AbilityMasterRegistry.Find(NextAbilityTag))
            {
                AbilityConfig = *NextDef;
                OnAbilityConfigChanged.Broadcast(AbilityConfig);

                if (AbilityConfig.ExecutionType == EAbilityExecutionType::Charging && AbilityConfig.ChargeProfile.MaxChargeTime > 0.0f)
                {
                    RuntimeTrackedChargeTime = 0.0f;
                    PlayStagePresentation(AbilityConfig.StageVisuals.ChargingStage);
                    UAT_WaitChargeTime* NextChargeTask = UAT_WaitChargeTime::WaitChargeTime(this, AbilityConfig.ChargeProfile.MaxChargeTime);
                    if (NextChargeTask)
                    {
                        NextChargeTask->OnChargeTick.AddDynamic(this, &UDynamicGameplayAbility::HandleTickDrain);
                        NextChargeTask->OnMaxChargeReached.AddDynamic(this, &UDynamicGameplayAbility::HandleMaxChargeReached);
                        NextChargeTask->ReadyForActivation();
                        return;
                    }
                }
            }
        }
    }
    else if (AbilityConfig.ChargeProfile.MaxChargeResolution == EMaxChargeResolution::MutateToOtherAbility && AbilityConfig.ChargeProfile.MutationAbilityTag.IsValid())
    {
        if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
        {
            if (const FDynamicAbilityDef* MutatedDef = GS->AbilityMasterRegistry.Find(AbilityConfig.ChargeProfile.MutationAbilityTag))
            {
                AbilityConfig = *MutatedDef;
                OnAbilityConfigChanged.Broadcast(AbilityConfig);
            }
        }
    }

    FinalizeRelease(1.0f);
}

void UDynamicGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
    if (bServerApprovedActivation)
    {
        bServerApprovedActivation = false;
        const float MaxTime = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
        FinalizeRelease(FMath::Clamp(RuntimeTrackedChargeTime / MaxTime, 0.1f, 1.0f));
    }
}

void UDynamicGameplayAbility::FinalizeRelease(float FinalChargeAlpha)
{
    CurrentPhase = EValidationPhase::Release;
    if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    {
        CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
        return;
    }

    PlayStagePresentation(AbilityConfig.StageVisuals.ReleaseStage);
    ExecuteSpatialDelivery(FinalChargeAlpha);
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::ExecuteSpatialDelivery(float ChargeAlpha)
{
    if (!HasAuthority(&CurrentActivationInfo)) return;

    AActor* Avatar = GetAvatarActorFromActorInfo();
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!Avatar || !GS) return;

    const FDynamicSpatialDeliveryDef& Delivery = AbilityConfig.SpatialDelivery;
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    // 1. FAST PATH: Instant / Sub-0.25s Shockwave Nova -> Server Multi-Sweep (Zero Actor Spawn!)
    if (Delivery.MotionType == ESpatialDeliveryMotion::AttachedToCaster &&
        Delivery.ApplicationType == EAuraEffectApplicationType::SingleHit &&
        Delivery.Duration <= 0.25f)
    {
        TArray<FHitResult> HitResults;
        FCollisionShape SphereShape = FCollisionShape::MakeSphere(Delivery.MaxRadius);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InstantSpatialNovaSweep), false, Avatar);

        if (World->SweepMultiByChannel(HitResults, Avatar->GetActorLocation(), Avatar->GetActorLocation() + FVector(0, 0, 1.0f), FQuat::Identity, ECC_Pawn, SphereShape, QueryParams))
        {
            TSet<AActor*> ProcessedActors;
            for (const FHitResult& Hit : HitResults)
            {
                ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(Hit.GetActor());
                if (!HitChar || HitChar == Avatar || ProcessedActors.Contains(HitChar)) continue;
                ProcessedActors.Add(HitChar);

                const FName BoneName = (Hit.BoneName != NAME_None) ? Hit.BoneName : FName("pelvis");
                const FVector OutwardNormal = (Avatar->GetActorLocation() - HitChar->GetActorLocation()).GetSafeNormal();
                const float ScaledImpulse = Delivery.KineticImpulseOnHit * ChargeAlpha;

                HitChar->ServerProcessAuthoritativeLimbHit(HitChar, BoneName, ScaledImpulse, ChargeAlpha, HitChar->GetActorLocation(), OutwardNormal, Avatar);

                if (UAbilitySystemComponent* TargetASC = HitChar->GetAbilitySystemComponent())
                {
                    const uint8 LimbId = GS->GetLimbIdForBone(BoneName);
                    for (const FGameplayTag& EffTag : Delivery.EffectsToApply)
                    {
                        FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC, EffTag, 1);
                        if (SpecHandle.IsValid())
                        {
                            FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(SpecHandle.Data->GetContext().Get());
                            if (DynCtx)
                            {
                                DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, ChargeAlpha, OutwardNormal, HitChar->GetActorLocation());
                                DynCtx->SourcePassBackRules = AbilityConfig.SourcePassBackRules;
                            }
                            SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                        }
                    }
                }
            }
        }
        return;
    }

    // 2. POOLED SPATIAL ACTOR PATH: Projectiles, Multi-Shot, Persistent Auras, and Expanding Ring Waves
    UDynamicGASManagerComponent* Registry = Avatar->FindComponentByClass<UDynamicGASManagerComponent>();
    float BaseSpeed = Delivery.SpeedOrExpansionRate;
    if (Registry && Delivery.ArchetypeLinkTag.IsValid())
    {
        const float ArchSpeed = Registry->ResolveMetric(Delivery.ArchetypeLinkTag, FGameplayTag::RequestGameplayTag("Metric.Movement.Speed", false), Delivery.RuntimeMetricModifiers);
        if (ArchSpeed > 0.0f) BaseSpeed = ArchSpeed;
    }
    const float FinalSpeed = BaseSpeed * (1.0f + (ChargeAlpha * AbilityConfig.ChargeProfile.CostScalingMultiplier));

    const int32 Count = FMath::Max(1, Delivery.SpawnCount);
    const float PowerMultiplierPerInstance = FMath::Pow(Delivery.PowerDecayPerAdditional, static_cast<float>(Count - 1)) * ChargeAlpha;
    const float TotalSpread = Delivery.SpreadAngleDegrees;

    const bool bIsAttached = (Delivery.MotionType == ESpatialDeliveryMotion::AttachedToCaster);
    const FVector BaseLoc = bIsAttached ? Avatar->GetActorLocation() : (Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * 80.0f);
    const FRotator BaseRot = Avatar->GetActorRotation();

    for (int32 i = 0; i < Count; ++i)
    {
        float YawOffset = 0.0f;
        if (Count > 1)
        {
            const float Step = TotalSpread / static_cast<float>(Count - 1);
            YawOffset = (-TotalSpread * 0.5f) + (Step * i);
        }

        const FTransform SpawnTM(BaseRot + FRotator(0.0f, YawOffset, 0.0f), BaseLoc);
        if (ADynamicSpatialActor* SpatialActor = GS->AcquireSpatialActor(SpawnTM, Avatar, Cast<APawn>(Avatar)))
        {
            if (bIsAttached)
            {
                SpatialActor->AttachToComponent(Avatar->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            }
            SpatialActor->ActivateSpatialDelivery(
                SourceASC,
                Delivery,
                AbilityConfig.SourcePassBackRules,
                FinalSpeed,
                PowerMultiplierPerInstance,
                nullptr);
        }
    }
}

void UDynamicGameplayAbility::ExecuteChainBehavior(const FGameplayEventData* TriggerEventData)
{
    if (!HasAuthority(&CurrentActivationInfo)) return;
    AActor* CurrentTarget = (TriggerEventData && TriggerEventData->Target.Get()) ? const_cast<AActor*>(TriggerEventData->Target.Get()) : nullptr;
    if (!CurrentTarget) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false); return; }

    int32 MaxBounces = AbilityConfig.ChainConfig.MaxBounces;
    float Falloff = AbilityConfig.ChainConfig.DamageFalloffPerBounce;
    float Radius = AbilityConfig.ChainConfig.BounceSearchRadius;
    float BaseDamage = AbilityConfig.NumericParams.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Ability.Param.BaseDamage", false));

    TArray<AActor*> IgnoredTargets;
    IgnoredTargets.Add(GetAvatarActorFromActorInfo());

    for (int32 CurrentBounce = 0; CurrentBounce < MaxBounces && CurrentTarget; ++CurrentBounce)
    {
        float ModdedDamage = BaseDamage * FMath::Pow(Falloff, CurrentBounce);
        IgnoredTargets.Add(CurrentTarget);

        TArray<AActor*> Hits;
        TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
        ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
        UKismetSystemLibrary::SphereOverlapActors(GetWorld(), CurrentTarget->GetActorLocation(), Radius, ObjectTypes, nullptr, IgnoredTargets, Hits);
        CurrentTarget = (Hits.Num() > 0) ? Hits[0] : nullptr;
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::ExecuteMovementBehavior()
{
    ADynamicCharacter* CustomPawn = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());
    if (!CustomPawn) return;

    UDynamicForceComponent* ForceComp = CustomPawn->FindComponentByClass<UDynamicForceComponent>();
    if (!ForceComp) return;

    float VelocityIntensity = AbilityConfig.MovementConfig.MovementMetrics.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Movement.Param.Velocity", false));
    if (FMath::IsNearlyZero(VelocityIntensity)) VelocityIntensity = 1000.0f;

    ForceComp->QueueInstantMovementImpulse(CustomPawn->GetActorForwardVector(), VelocityIntensity);
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::UpdateConfigAtRuntime_Implementation(FDynamicAbilityDef NewConfigLayout)
{
    AbilityConfig = NewConfigLayout;
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
}

void UDynamicGameplayAbility::OnRep_AbilityConfig()
{
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
}