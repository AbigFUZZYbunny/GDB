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
#include "DynamicInteractableComponent.h"
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

bool UDynamicGameplayAbility::IsValidNextTierTransition(const FDynamicAbilityDef& CurrentStageDef, const FDynamicAbilityDef& NextStageDef)
{
    // Strictly require sequential tier advancement (e.g., Tier 1 -> Tier 2 -> Tier 3)
    return NextStageDef.BaseSpellTier == (CurrentStageDef.BaseSpellTier + 1);
}

bool UDynamicGameplayAbility::SetCustomSpecChain(const TArray<FGameplayTag>& InChainTags)
{
    CustomSpecChainTags.Empty();
    CurrentChainStageIndex = 0;
    if (InChainTags.Num() == 0) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS)
    {
        CustomSpecChainTags = InChainTags;
        return true;
    }

    const FDynamicAbilityDef* FirstDef = GS->AbilityMasterRegistry.Find(InChainTags[0]);
    if (!FirstDef || FirstDef->BaseSpellTier != 1)
    {
        // Chain must start with a BaseSpellTier 1 ability
        return false;
    }

    CustomSpecChainTags.Add(InChainTags[0]);
    const FDynamicAbilityDef* PrevDef = FirstDef;

    for (int32 i = 1; i < InChainTags.Num(); ++i)
    {
        const FDynamicAbilityDef* CandidateDef = GS->AbilityMasterRegistry.Find(InChainTags[i]);
        if (!CandidateDef || !IsValidNextTierTransition(*PrevDef, *CandidateDef))
        {
            // Rejects invalid tier jumps such as Tier 1 -> Tier 3
            break;
        }
        CustomSpecChainTags.Add(InChainTags[i]);
        PrevDef = CandidateDef;
    }

    AbilityConfig = *FirstDef;
    return true;
}

UDynamicInteractableComponent* UDynamicGameplayAbility::FindBestNearbyInteractable(const FGameplayAbilityActorInfo* ActorInfo) const
{
    if (!ActorInfo || !ActorInfo->AvatarActor.IsValid()) return nullptr;
    AActor* Avatar = ActorInfo->AvatarActor.Get();
    UWorld* World = Avatar->GetWorld();
    if (!World) return nullptr;

    const FDynamicInteractionProfile& Profile = AbilityConfig.InteractionConfig;
    const FVector Start = Avatar->GetActorLocation();
    const FVector Forward = Avatar->GetActorForwardVector();
    const FVector End = Start + (Forward * Profile.MaxInteractionDistance);

    TArray<FHitResult> Hits;
    FCollisionShape SweepShape = FCollisionShape::MakeSphere(Profile.TargetSweepRadius);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DynamicInteractionSweep), false, Avatar);

    World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility, SweepShape, Params);

    for (const FHitResult& Hit : Hits)
    {
        if (AActor* HitActor = Hit.GetActor())
        {
            if (UDynamicInteractableComponent* Interactable = HitActor->FindComponentByClass<UDynamicInteractableComponent>())
            {
                if (Interactable->CanAcceptInteraction(Profile, Avatar))
                {
                    return Interactable;
                }
            }
        }
    }
    return nullptr;
}

bool UDynamicGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
    {
        return false;
    }

    if (AbilityConfig.ExecutionType == EAbilityExecutionType::Interaction)
    {
        if (!FindBestNearbyInteractable(ActorInfo))
        {
            return false;
        }
    }

    return true;
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

void UDynamicGameplayAbility::GrantSourcePassBack(EPassBackTriggerEvent EventType, float Scalar) const
{
    if (!HasAuthority(&CurrentActivationInfo)) return;
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    const UDynamicAttributeSet* SourceAttr = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
    if (!SourceAttr) return;

    for (const FSourcePassBackRule& Rule : AbilityConfig.SourcePassBackRules)
    {
        if (Rule.TriggerEvent == EventType && Rule.SourceAttributeTag.IsValid())
        {
            float Cur = SourceAttr->GetDynamicAttributeValue(Rule.SourceAttributeTag);
            const_cast<UDynamicAttributeSet*>(SourceAttr)->SetDynamicAttributeValue(Rule.SourceAttributeTag, Cur + (Rule.BaseGrantAmount * Scalar));
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
    GrantSourcePassBack(EPassBackTriggerEvent::OnCast, 1.0f);

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
        case EAbilityExecutionType::Interaction:
            ExecuteInteractionBehavior(1.0f);
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
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

    if (GS && CustomSpecChainTags.IsValidIndex(CurrentChainStageIndex + 1))
    {
        const FGameplayTag NextAbilityTag = CustomSpecChainTags[CurrentChainStageIndex + 1];
        if (const FDynamicAbilityDef* NextDef = GS->AbilityMasterRegistry.Find(NextAbilityTag))
        {
            // Enforce strict tier step: Tier N -> Tier N + 1
            if (IsValidNextTierTransition(AbilityConfig, *NextDef))
            {
                CurrentChainStageIndex++;
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
    else if (GS && AbilityConfig.ChargeProfile.MaxChargeResolution == EMaxChargeResolution::MutateToOtherAbility && AbilityConfig.ChargeProfile.MutationAbilityTag.IsValid())
    {
        if (const FDynamicAbilityDef* MutatedDef = GS->AbilityMasterRegistry.Find(AbilityConfig.ChargeProfile.MutationAbilityTag))
        {
            if (IsValidNextTierTransition(AbilityConfig, *MutatedDef))
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

    if (AbilityConfig.ExecutionType == EAbilityExecutionType::Interaction || AbilityConfig.InteractionConfig.InteractionActionTag.IsValid())
    {
        ExecuteInteractionBehavior(FinalChargeAlpha);
    }
    else
    {
        ExecuteSpatialDelivery(FinalChargeAlpha);
    }

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::ExecuteInteractionBehavior(float ChargeAlpha)
{
    UDynamicInteractableComponent* TargetInteractable = FindBestNearbyInteractable(CurrentActorInfo);
    if (!TargetInteractable) return;

    ADynamicCharacter* AvatarChar = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());

    // 1. Align GASP kinematic effector (e.g. "hand_r") to the target point (e.g. "Interaction.Target.DoorHandle") on Client & Server
    if (AvatarChar && AbilityConfig.InteractionConfig.KinematicTargetTag.IsValid())
    {
        FTransform HandleWorldTransform;
        USceneComponent* HandleComp = nullptr;
        if (TargetInteractable->ResolveKinematicTargetTransform(AbilityConfig.InteractionConfig.KinematicTargetTag, HandleWorldTransform, HandleComp))
        {
            AvatarChar->AlignLimbToInteractionTarget(AbilityConfig.InteractionConfig.CasterEffectorBone, HandleWorldTransform, HandleComp);
        }
    }

    PlayStagePresentation(AbilityConfig.StageVisuals.ReleaseStage);

    if (!HasAuthority(&CurrentActivationInfo)) return;

    // 2. Server executes authoritative state transitions and physical impulses
    if (TargetInteractable->ServerExecuteInteraction(AvatarChar, AbilityConfig.InteractionConfig, ChargeAlpha))
    {
        PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
        GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);

        if (AbilityConfig.InteractionConfig.EffectsToApplyToTarget.Num() > 0 && TargetInteractable->GetOwner())
        {
            UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
            UAbilitySystemComponent* TargetASC = TargetInteractable->GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
            if (SourceASC && TargetASC)
            {
                for (const FGameplayTag& EffTag : AbilityConfig.InteractionConfig.EffectsToApplyToTarget)
                {
                    FGameplayEffectSpecHandle Spec = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC, EffTag, 1);
                    if (Spec.IsValid())
                    {
                        SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                    }
                }
            }
        }
    }
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

    UDynamicGASManagerComponent* Registry = Avatar->FindComponentByClass<UDynamicGASManagerComponent>();
    float BaseSpeed = Delivery.SpeedOrExpansionRate;
    if (Registry && Delivery.ArchetypeLinkTag.IsValid() && Delivery.SpeedMetricTag.IsValid())
    {
        const float ArchSpeed = Registry->ResolveMetric(Delivery.ArchetypeLinkTag, Delivery.SpeedMetricTag, Delivery.RuntimeMetricModifiers);
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
    float BaseDamage = AbilityConfig.ChainConfig.DamageParamTag.IsValid()
        ? AbilityConfig.NumericParams.LocalLookupMap.FindRef(AbilityConfig.ChainConfig.DamageParamTag)
        : 0.0f;

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

    float VelocityIntensity = AbilityConfig.MovementConfig.VelocityMetricTag.IsValid()
        ? AbilityConfig.MovementConfig.MovementMetrics.LocalLookupMap.FindRef(AbilityConfig.MovementConfig.VelocityMetricTag)
        : 1000.0f;
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