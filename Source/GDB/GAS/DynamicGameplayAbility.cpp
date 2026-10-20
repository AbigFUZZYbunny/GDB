// DynamicGameplayAbility.cpp
#include "DynamicGameplayAbility.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "AT_WaitChargeTime.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicCharacter.h"
#include "DynamicForceComponent.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "DynamicSpatialActor.h"
#include "DynamicInteractableComponent.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"
#include "TimerManager.h"
#include "DynamicContainerComponent.h"
#include "DynamicPhysicsActor.h"
#include "Engine/OverlapResult.h"

UDynamicGameplayAbility::UDynamicGameplayAbility()
{
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UDynamicGameplayAbility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // OPTIMIZATION: Only replicate the lightweight tag! Hydrate AbilityConfig locally on client.
    DOREPLIFETIME(UDynamicGameplayAbility, ActiveAbilityRegistryTag);
}

void UDynamicGameplayAbility::OnRep_ActiveAbilityRegistryTag()
{
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicAbilityDef* Def = GS->AbilityMasterRegistry.Find(ActiveAbilityRegistryTag))
            {
                AbilityConfig = *Def;
                OnAbilityConfigChanged.Broadcast(AbilityConfig);
            }
        }
    }
}

bool UDynamicGameplayAbility::CanBypassComboGate(const FDynamicAbilityDef& DefToCheck, const UAbilitySystemComponent* CasterASC)
{
    if (!DefToCheck.ComboGate.bRequiresComboTransitionToCast)
    {
        return true;
    }
    if (!CasterASC || !DefToCheck.ComboGate.BypassGateMasteryAttributeTag.IsValid())
    {
        return false;
    }
    const UDynamicAttributeSet* AttrSet = Cast<UDynamicAttributeSet>(CasterASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!AttrSet)
    {
        return false;
    }
    const float CurrentMastery = AttrSet->GetDynamicAttributeValue(DefToCheck.ComboGate.BypassGateMasteryAttributeTag, 0);
    return CurrentMastery >= DefToCheck.ComboGate.BypassGateMasteryThreshold;
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

    if (!bActivatedViaComboTransition && AbilityConfig.ComboGate.bRequiresComboTransitionToCast)
    {
        if (!CanBypassComboGate(AbilityConfig, ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr))
        {
            return false;
        }
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
        case EValidationPhase::Release:
        case EValidationPhase::Recovery: TargetTag = AbilityConfig.LifeCycleRequirements.ReleaseCostConditionTag; break;
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

void UDynamicGameplayAbility::RefreshOpenComboWindows(float CurrentPhaseElapsed, float CurrentPhaseAlpha)
{
    FGameplayTagContainer NewOpenTags;
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    AActor* OwnerActor = GetOwningActorFromActorInfo();

    for (const FComboWindowRule& Rule : AbilityConfig.ComboWindows)
    {
        if (Rule.ActivePhase != CurrentPhase || !Rule.AllowedComboAbilityTag.IsValid()) continue;

        const float Metric = Rule.bUseNormalizedAlpha ? CurrentPhaseAlpha : CurrentPhaseElapsed;
        if (Metric >= Rule.WindowStartTimeOrAlpha && Metric <= Rule.WindowEndTimeOrAlpha)
        {
            if (!Rule.OptionalConditionTreeName.IsNone() && OwnerActor)
            {
                if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(OwnerActor, Rule.OptionalConditionTreeName))
                {
                    FGameplayTagContainer STags, TTags;
                    if (ASC) ASC->GetOwnedGameplayTags(TTags);
                    if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, STags, TTags))
                    {
                        continue;
                    }
                }
            }
            NewOpenTags.AddTag(Rule.AllowedComboAbilityTag);
        }
    }

    for (const FGameplayTag& NewlyOpened : NewOpenTags)
    {
        if (!CurrentlyOpenComboTags.HasTagExact(NewlyOpened))
        {
            OnComboWindowChanged.Broadcast(NewlyOpened, true);
        }
    }
    for (const FGameplayTag& PreviouslyOpen : CurrentlyOpenComboTags)
    {
        if (!NewOpenTags.HasTagExact(PreviouslyOpen))
        {
            OnComboWindowChanged.Broadcast(PreviouslyOpen, false);
        }
    }

    CurrentlyOpenComboTags = NewOpenTags;
}

bool UDynamicGameplayAbility::IsComboWindowOpenForAbility(FGameplayTag RequestedComboTag) const
{
    if (!IsActive() || !RequestedComboTag.IsValid()) return false;

    if (CurrentlyOpenComboTags.HasTagExact(RequestedComboTag))
    {
        return true;
    }

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const float PhaseElapsed = static_cast<float>(FMath::Max(0.0, Now - PhaseStartWorldTime));
    float PhaseDuration = 1.0f;
    if (CurrentPhase == EValidationPhase::Charging) PhaseDuration = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
    else if (CurrentPhase == EValidationPhase::Recovery) PhaseDuration = FMath::Max(0.01f, AbilityConfig.LifeCycleRequirements.RecoveryWindowDuration);

    const float PhaseAlpha = FMath::Clamp(PhaseElapsed / PhaseDuration, 0.0f, 1.0f);

    for (const FComboWindowRule& Rule : AbilityConfig.ComboWindows)
    {
        if (Rule.ActivePhase == CurrentPhase && Rule.AllowedComboAbilityTag == RequestedComboTag)
        {
            const float Metric = Rule.bUseNormalizedAlpha ? PhaseAlpha : PhaseElapsed;
            if (Metric >= Rule.WindowStartTimeOrAlpha && Metric <= Rule.WindowEndTimeOrAlpha)
            {
                return true;
            }
        }
    }
    return false;
}

bool UDynamicGameplayAbility::TryTriggerComboTransition(FGameplayTag NextComboAbilityTag)
{
    if (!IsComboWindowOpenForAbility(NextComboAbilityTag))
    {
        return false;
    }

    if (!HasAuthority(&CurrentActivationInfo))
    {
        Server_TryTriggerComboTransition(NextComboAbilityTag);
    }

    return TransitionIntoComboAbilityInternal(NextComboAbilityTag);
}

bool UDynamicGameplayAbility::Server_TryTriggerComboTransition_Validate(FGameplayTag NextComboAbilityTag)
{
    return NextComboAbilityTag.IsValid();
}

void UDynamicGameplayAbility::Server_TryTriggerComboTransition_Implementation(FGameplayTag NextComboAbilityTag)
{
    if (IsComboWindowOpenForAbility(NextComboAbilityTag))
    {
        TransitionIntoComboAbilityInternal(NextComboAbilityTag);
    }
}

bool UDynamicGameplayAbility::TransitionIntoComboAbilityInternal(FGameplayTag NextComboAbilityTag)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicAbilityDef* NextDef = GS->AbilityMasterRegistry.Find(NextComboAbilityTag);
    if (!NextDef) return false;

    if (NextDef->ComboGate.ValidPrerequisiteAbilityTags.Num() > 0 && ActiveAbilityRegistryTag.IsValid())
    {
        if (!NextDef->ComboGate.ValidPrerequisiteAbilityTags.HasTag(ActiveAbilityRegistryTag))
        {
            return false;
        }
    }

    // Clean up active charge task and timers before transitioning
    if (ActiveChargeTask)
    {
        ActiveChargeTask->EndTask();
        ActiveChargeTask = nullptr;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_RecoveryWindow);
    }

    bActivatedViaComboTransition = true;
    ActiveAbilityRegistryTag = NextComboAbilityTag;
    AbilityConfig = *NextDef;
    CurrentlyOpenComboTags.Reset();
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
    ActivateAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, nullptr);
    return true;
}

void UDynamicGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    CurrentPhase = EValidationPhase::Attempt;
    RuntimeTrackedChargeTime = 0.0f;
    PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    CurrentlyOpenComboTags.Reset();

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        bActivatedViaComboTransition = false;
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    bServerApprovedActivation = true;
    PlayStagePresentation(AbilityConfig.StageVisuals.AttemptStage);
    GrantSourcePassBack(EPassBackTriggerEvent::OnCast, 1.0f);
    RefreshOpenComboWindows(0.0f, 0.0f);

    switch (AbilityConfig.ExecutionType)
    {
        case EAbilityExecutionType::Charging:
        {
            CurrentPhase = EValidationPhase::Charging;
            PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            PlayStagePresentation(AbilityConfig.StageVisuals.ChargingStage);
            RefreshOpenComboWindows(0.0f, 0.0f);

            ActiveChargeTask = UAT_WaitChargeTime::WaitChargeTime(this, AbilityConfig.ChargeProfile.MaxChargeTime);
            if (ActiveChargeTask)
            {
                ActiveChargeTask->OnChargeTick.AddDynamic(this, &UDynamicGameplayAbility::HandleTickDrain);
                ActiveChargeTask->OnMaxChargeReached.AddDynamic(this, &UDynamicGameplayAbility::HandleMaxChargeReached);
                ActiveChargeTask->ReadyForActivation();
            }
            break;
        }
        case EAbilityExecutionType::SpatialDelivery:
            ExecuteSpatialDelivery(1.0f);
            EnterRecoveryPhase();
            break;
        case EAbilityExecutionType::Interaction:
            ExecuteInteractionBehavior(1.0f);
            EnterRecoveryPhase();
            break;
        case EAbilityExecutionType::Chain:
            ExecuteChainBehavior(TriggerEventData);
            EnterRecoveryPhase();
            break;
        case EAbilityExecutionType::Movement:
            ExecuteMovementBehavior();
            EnterRecoveryPhase();
            break;
        default:
            EnterRecoveryPhase();
            break;
    }
}

void UDynamicGameplayAbility::HandleTickDrain(float TotalElapsedTime, float DeltaTime)
{
    RuntimeTrackedChargeTime = TotalElapsedTime;
    const float MaxCharge = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
    const float ChargeAlpha = FMath::Clamp(RuntimeTrackedChargeTime / MaxCharge, 0.0f, 1.0f);

    RefreshOpenComboWindows(RuntimeTrackedChargeTime, ChargeAlpha);

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
    RuntimeTrackedChargeTime = AbilityConfig.ChargeProfile.MaxChargeTime;
    RefreshOpenComboWindows(RuntimeTrackedChargeTime, 1.0f);
    if (!AbilityConfig.ChargeProfile.bHoldAtMaxCharge)
    {
        FinalizeRelease(1.0f);
    }
}

void UDynamicGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
    if (bServerApprovedActivation && CurrentPhase == EValidationPhase::Charging)
    {
        bServerApprovedActivation = false;
        const float MaxTime = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
        FinalizeRelease(FMath::Clamp(RuntimeTrackedChargeTime / MaxTime, 0.1f, 1.0f));
    }
}

void UDynamicGameplayAbility::FinalizeRelease(float FinalChargeAlpha)
{
    if (ActiveChargeTask)
    {
        ActiveChargeTask->EndTask();
        ActiveChargeTask = nullptr;
    }

    CurrentPhase = EValidationPhase::Release;
    PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    RefreshOpenComboWindows(0.0f, FinalChargeAlpha);

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

    EnterRecoveryPhase();
}

void UDynamicGameplayAbility::EnterRecoveryPhase()
{
    const float RecoveryDuration = AbilityConfig.LifeCycleRequirements.RecoveryWindowDuration;
    if (RecoveryDuration <= 0.0f)
    {
        ConcludeRecoveryPhase();
        return;
    }

    CurrentPhase = EValidationPhase::Recovery;
    PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    PlayStagePresentation(AbilityConfig.StageVisuals.RecoveryStage);
    RefreshOpenComboWindows(0.0f, 1.0f);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(TimerHandle_RecoveryWindow, this, &UDynamicGameplayAbility::ConcludeRecoveryPhase, RecoveryDuration, false);
    }
}

void UDynamicGameplayAbility::ConcludeRecoveryPhase()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_RecoveryWindow);
    }
    bActivatedViaComboTransition = false;
    CurrentlyOpenComboTags.Reset();
    
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UDynamicGameplayAbility::ExecuteInteractionBehavior(float ChargeAlpha)
{
    UDynamicInteractableComponent* TargetInteractable = FindBestNearbyInteractable(CurrentActorInfo);
    if (!TargetInteractable) return;

    ADynamicCharacter* AvatarChar = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());
    AActor* TargetOwnerActor = TargetInteractable->GetOwner();

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

    const FGameplayTag ActionTag = AbilityConfig.InteractionConfig.InteractionActionTag;

    // 1. STORE ACTION: Stores a world ADynamicPhysicsActor into a container (e.g. putting an item into a chest or backpack)
    if (ActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Store"))))
    {
        if (ADynamicPhysicsActor* TargetPhysicsActor = Cast<ADynamicPhysicsActor>(TargetOwnerActor))
        {
            if (UDynamicContainerComponent* CasterContainer = AvatarChar->FindComponentByClass<UDynamicContainerComponent>())
            {
                if (CasterContainer->ServerStorePhysicalActor(TargetPhysicsActor, AvatarChar))
                {
                    PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                    GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
                }
            }
        }
    }
    // 2. LOOT / EXTRACT ACTION: Extracts an item from a world container into the character's hands or inventory
    else if (ActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Loot"))))
    {
        if (UDynamicContainerComponent* TargetContainer = TargetOwnerActor->FindComponentByClass<UDynamicContainerComponent>())
        {
            // Example: Extract item located at grid coordinate [0,0] into the character's hand socket
            int32 ExtractX = 0;
            int32 ExtractY = 0;
            if (TargetContainer->ServerExtractActorToHands(ExtractX, ExtractY, AvatarChar, AbilityConfig.InteractionConfig.CasterEffectorBone))
            {
                PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
            }
        }
    }
    // 3. STANDARD WORLD INTERACTION (Open, Close, Unlock, Lock, Push, Pull, Throw)
    else if (TargetInteractable->ServerExecuteInteraction(AvatarChar, AbilityConfig.InteractionConfig, ChargeAlpha))
    {
        PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
        GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);

        if (AbilityConfig.InteractionConfig.EffectsToApplyToTarget.Num() > 0 && TargetOwnerActor)
        {
            UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
            UAbilitySystemComponent* TargetASC = TargetOwnerActor->FindComponentByClass<UAbilitySystemComponent>();
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
    if (!CurrentTarget) return;

    const int32 MaxBounces = AbilityConfig.ChainConfig.MaxBounces;
    const float Falloff = AbilityConfig.ChainConfig.DamageFalloffPerBounce;
    const float Radius = AbilityConfig.ChainConfig.BounceSearchRadius;
    const float BaseDamage = AbilityConfig.ChainConfig.DamageParamTag.IsValid()
        ? AbilityConfig.NumericParams.LocalLookupMap.FindRef(AbilityConfig.ChainConfig.DamageParamTag)
        : 50.0f;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChainAbility), false);
    QueryParams.AddIgnoredActor(GetAvatarActorFromActorInfo());

    for (int32 CurrentBounce = 0; CurrentBounce < MaxBounces && CurrentTarget; ++CurrentBounce)
    {
        const float ModdedDamage = BaseDamage * FMath::Pow(Falloff, CurrentBounce);

        if (ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(CurrentTarget))
        {
            HitChar->ServerProcessAuthoritativeLimbHit(HitChar, FName("pelvis"), ModdedDamage, 1.0f, HitChar->GetActorLocation(), FVector::UpVector, GetAvatarActorFromActorInfo());
            GrantSourcePassBack(EPassBackTriggerEvent::OnDamageDealt, ModdedDamage);
        }

        QueryParams.AddIgnoredActor(CurrentTarget);
        TArray<FOverlapResult> Overlaps;
        const bool bFound = GetWorld()->OverlapMultiByChannel(
            Overlaps,
            CurrentTarget->GetActorLocation(),
            FQuat::Identity,
            ECC_Pawn,
            FCollisionShape::MakeSphere(Radius),
            QueryParams
        );

        CurrentTarget = (bFound && Overlaps.Num() > 0) ? Overlaps[0].GetActor() : nullptr;
    }
}

void UDynamicGameplayAbility::ExecuteMovementBehavior()
{
    if (ADynamicCharacter* CustomPawn = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo()))
    {
        if (UDynamicForceComponent* ForceComp = CustomPawn->FindComponentByClass<UDynamicForceComponent>())
        {
            float VelocityIntensity = AbilityConfig.MovementConfig.VelocityMetricTag.IsValid()
                ? AbilityConfig.MovementConfig.MovementMetrics.LocalLookupMap.FindRef(AbilityConfig.MovementConfig.VelocityMetricTag)
                : 1000.0f;
            if (FMath::IsNearlyZero(VelocityIntensity)) VelocityIntensity = 1000.0f;

            ForceComp->QueueInstantMovementImpulse(CustomPawn->GetActorForwardVector(), VelocityIntensity);
        }
    }
}

void UDynamicGameplayAbility::UpdateConfigAtRuntime_Implementation(FDynamicAbilityDef NewConfigLayout)
{
    AbilityConfig = NewConfigLayout;
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
}