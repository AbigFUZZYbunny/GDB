// DynamicGameplayAbility.cpp
#include "DynamicGameplayAbility.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "AT_ChannelSpatialDelivery.h"
#include "AT_WaitChargeTime.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "DynamicCharacter.h"
#include "DynamicAudioSubsystem.h"
#include "DynamicVFXSubsystem.h"
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
#include "DynamicVoxelSubsystem.h"
#include "DynamicBuildingChunkActor.h"
#include "Engine/AssetManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/StreamableManager.h"

UDynamicGameplayAbility::UDynamicGameplayAbility()
{
    // MMO SCALING: InstancedPerExecution eliminates 75,000 idle UObject instances in server memory
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UDynamicGameplayAbility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // OPTIMIZATION: Only replicate the lightweight tag! Hydrate AbilityConfig locally on client.
    DOREPLIFETIME(UDynamicGameplayAbility, ActiveAbilityRegistryTag);
}

void UDynamicGameplayAbility::HydrateConfigFromRegistry(FGameplayTag AbilityTag)
{
    if (!AbilityTag.IsValid()) return;
    if (UWorld* World = GetWorld())
    {
        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            if (const FDynamicAbilityDef* Def = GS->AbilityMasterRegistry.Find(AbilityTag))
            {
                AbilityConfig = *Def;
                OnAbilityConfigChanged.Broadcast(AbilityConfig);
            }
        }
    }
}

void UDynamicGameplayAbility::OnRep_ActiveAbilityRegistryTag()
{
    HydrateConfigFromRegistry(ActiveAbilityRegistryTag);
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

    // 1. Evaluate Data-Driven Condition Tree (e.g. AttemptCooldownConditionTag)
    const FGameplayTag TargetTag = (CurrentPhase == EValidationPhase::Attempt)
        ? AbilityConfig.LifeCycleRequirements.AttemptCooldownConditionTag
        : AbilityConfig.LifeCycleRequirements.ReleaseCooldownConditionTag;

    static const FGameplayTag NoneTag = FGameplayTag::RequestGameplayTag(TEXT("None"), false);
    if (TargetTag.IsValid() && TargetTag != NoneTag)
    {
        return EvaluateDynamicRule(TargetTag, ActorInfo);
    }

    // 2. Default Cooldown Check: Evaluates Attribute.Cooldown.<AbilityTag> <= CurrentWorldTime
    if (ActorInfo && ActiveAbilityRegistryTag.IsValid())
    {
        const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
        const UDynamicAttributeSet* AttrSet = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
        if (AttrSet)
        {
            const FString CooldownTagStr = FString::Printf(TEXT("Attribute.Cooldown.%s"), *ActiveAbilityRegistryTag.ToString());
            const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(FName(*CooldownTagStr), false);
            if (CooldownTag.IsValid())
            {
                const float CooldownExpiry = AttrSet->GetDynamicAttributeValue(CooldownTag, 0);
                const UWorld* World = GetWorld();
                const float CurrentWorldTime = World ? World->GetTimeSeconds() : 0.0f;
                if (CooldownExpiry > CurrentWorldTime)
                {
                    return false;
                }
            }
        }
    }

    return true;
}

void UDynamicGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
    Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);

    if (!HasAuthority(&ActivationInfo) || !ActorInfo) return;

    const float Duration = AbilityConfig.LifeCycleRequirements.BaseCooldownDuration * AbilityConfig.ChargeProfile.CooldownScalingMultiplier;
    if (Duration <= 0.0f) return;

    UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    if (!ASC) return;

    UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())));
    if (!AttrSet || !ActiveAbilityRegistryTag.IsValid()) return;

    const UWorld* World = GetWorld();
    const float CurrentWorldTime = World ? World->GetTimeSeconds() : 0.0f;
    const float ExpirationTimestamp = CurrentWorldTime + Duration;

    const FString CooldownTagStr = FString::Printf(TEXT("Attribute.Cooldown.%s"), *ActiveAbilityRegistryTag.ToString());
    const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(FName(*CooldownTagStr), false);

    if (CooldownTag.IsValid())
    {
        AttrSet->SetDynamicAttributeValue(CooldownTag, ExpirationTimestamp, 0);
    }
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

void UDynamicGameplayAbility::PlayStagePresentation(const FAbilityStagePresentation& StageConfig)
{
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (StageConfig.StageAudioTag.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            if (UDynamicAudioSubsystem* AudioSub = World->GetSubsystem<UDynamicAudioSubsystem>())
            {
                AudioSub->PlayDynamicAudioAtLocation(StageConfig.StageAudioTag, Avatar ? Avatar->GetActorLocation() : FVector::ZeroVector, Avatar);
            }
        }
    }

    // --- VFX: Trigger Stage Entry Event ---
    if (Avatar && StageConfig.VfxEventMap.LocalLookupMap.Num() > 0)
    {
        if (UDynamicVFXSubsystem* VfxSub = Avatar->GetWorld()->GetSubsystem<UDynamicVFXSubsystem>())
        {
            static const FGameplayTag StageEntryTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Stage.Entry"), false);
            VfxSub->PlayVFXLayer(StageEntryTag, StageConfig.VfxEventMap, Avatar->GetActorLocation(), Avatar->GetActorForwardVector(), 1.0f, Avatar, nullptr);
        }
    }

    if (StageConfig.StageMontage.IsNull()) return;

    const FSoftObjectPath MontagePath = StageConfig.StageMontage.ToSoftObjectPath();
    TWeakObjectPtr<UDynamicGameplayAbility> WeakThis(this);
    const float PlayRate = StageConfig.PlayRate;

    UAssetManager::GetStreamableManager().RequestAsyncLoad(MontagePath, FStreamableDelegate::CreateLambda([WeakThis, MontagePath, PlayRate]() {
        if (UDynamicGameplayAbility* StrongThis = WeakThis.Get())
        {
            if (UAnimMontage* Montage = Cast<UAnimMontage>(MontagePath.ResolveObject()))
            {
                if (ADynamicCharacter* StrongAvatar = Cast<ADynamicCharacter>(StrongThis->GetAvatarActorFromActorInfo()))
                {
                    if (USkeletalMeshComponent* MeshComp = StrongAvatar->GetMeshComponent())
                    {
                        if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
                        {
                            AnimInst->Montage_Play(Montage, PlayRate);
                        }
                    }
                }
            }
        }
    }));
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
    if (ActiveAbilityRegistryTag.IsValid()) HydrateConfigFromRegistry(ActiveAbilityRegistryTag);

    RuntimeTrackedChargeTime = 0.0f;
    PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    CurrentlyOpenComboTags.Reset();
    QTEParameterAdjustments.LocalLookupMap.Empty();
    bIsAwaitingQTE = false;

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        bActivatedViaComboTransition = false;
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    bServerApprovedActivation = true;
    EvaluateStageEntry(EValidationPhase::Attempt);
}

void UDynamicGameplayAbility::EvaluateStageEntry(EValidationPhase Phase)
{
    CurrentPhase = Phase;
    bIsAwaitingQTE = false;

    // Use the helper method instead of TMap::Find
    if (const FAbilityStageInputSequence* Seq = AbilityConfig.FindStageSequence(Phase))
    {
        if (Seq->Steps.Num() > 0)
        {
            bool bBypass = false;
            if (!Seq->BypassConditionTreeName.IsNone())
            {
                if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(GetOwningActorFromActorInfo(), Seq->BypassConditionTreeName))
                {
                    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
                    FGameplayTagContainer SourceTags;
                    if (ASC) ASC->GetOwnedGameplayTags(SourceTags);
                    if (UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, SourceTags, SourceTags))
                    {
                        bBypass = true;
                    }
                }
            }

            if (!bBypass)
            {
                bIsAwaitingQTE = true;
                CurrentInputStepIndex = 0;

                if (UWorld* World = GetWorld())
                {
                    World->GetTimerManager().SetTimer(TimerHandle_QTEWindow, this, &UDynamicGameplayAbility::OnQTEWindowFailed, Seq->SequenceTimeoutSeconds, false);
                }
                return;
            }
        }
    }

    ProceedWithStage(Phase);
}

void UDynamicGameplayAbility::ProceedWithStage(EValidationPhase Phase)
{
    switch (Phase)
    {
        case EValidationPhase::Attempt:
        {
            PlayStagePresentation(AbilityConfig.StageVisuals.AttemptStage);
            GrantSourcePassBack(EPassBackTriggerEvent::OnCast, 1.0f);
            RefreshOpenComboWindows(0.0f, 0.0f);
            
            // If the ability has any charge time configured, go to charging.
            if (AbilityConfig.ChargeProfile.MaxChargeTime > 0.0f || 
                AbilityConfig.ExecutionType == EAbilityExecutionType::Charging)
            {
                EvaluateStageEntry(EValidationPhase::Charging);
            }
            else
            {
                // Immediate release for instant abilities
                FinalizeRelease(1.0f);
            }
            break;
        }
        case EValidationPhase::Charging:
        {
            PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            PlayStagePresentation(AbilityConfig.StageVisuals.ChargingStage);
            RefreshOpenComboWindows(0.0f, 0.0f);

            // Charging is strictly pre-release power accumulation
            UAT_WaitChargeTime* ChargeTask = UAT_WaitChargeTime::WaitChargeTime(this, AbilityConfig.ChargeProfile.MaxChargeTime);
            if (ChargeTask)
            {
                ActiveChargeTask = ChargeTask;
                ChargeTask->OnChargeTick.AddDynamic(this, &UDynamicGameplayAbility::HandleTickDrain);
                ChargeTask->OnMaxChargeReached.AddDynamic(this, &UDynamicGameplayAbility::HandleMaxChargeReached);
                ChargeTask->ReadyForActivation();
            }
            break;
        }
        case EValidationPhase::Release:
        {
            PhaseStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            RefreshOpenComboWindows(0.0f, 1.0f); 
            PlayStagePresentation(AbilityConfig.StageVisuals.ReleaseStage);

            // Channeling occurs during the Release phase
            if (AbilityConfig.ExecutionType == EAbilityExecutionType::Channeled)
            {
                FName EffectorBone = AbilityConfig.InteractionConfig.CasterEffectorBone;

                // Dynamic Dominance Fallback: If no bone is specified, use the template's primary weapon hand
                if (EffectorBone.IsNone())
                {
                    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
                    {
                        if (ADynamicCharacter* Char = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo()))
                        {
                            if (UDynamicCustomizationComponent* Cust = Char->GetCustomizationComponent())
                            {
                                FCharacterTemplateDef TempDef;
                                if (GS->GetCharacterTemplate(Cust->ActiveCharacterTemplateTag, TempDef))
                                {
                                    EffectorBone = TempDef.DominanceProfile.MainHandBone;
                                }
                            }
                        }
                    }
                }
                // Final fallback
                if (EffectorBone.IsNone()) EffectorBone = FName("hand_r");

                UAT_ChannelSpatialDelivery* ChannelTask = UAT_ChannelSpatialDelivery::ChannelSpatialDelivery(this, EffectorBone);
                if (ChannelTask)
                {
                    ActiveChannelTask = ChannelTask;
                    ChannelTask->OnChannelFinished.AddDynamic(this, &UDynamicGameplayAbility::HandleChannelFinished);
                    ChannelTask->ReadyForActivation();
                }
            }
            else
            {
                // Standard instantaneous releases
                if (AbilityConfig.ExecutionType == EAbilityExecutionType::Interaction || AbilityConfig.InteractionConfig.InteractionActionTag.IsValid())
                {
                    ExecuteInteractionBehavior(1.0f);
                }
                else if (AbilityConfig.ExecutionType == EAbilityExecutionType::SpatialDelivery)
                {
                    ExecuteSpatialDelivery(1.0f);
                }
                else if (AbilityConfig.ExecutionType == EAbilityExecutionType::Chain)
                {
                    ExecuteChainBehavior(nullptr);
                }
                else if (AbilityConfig.ExecutionType == EAbilityExecutionType::Movement)
                {
                    ExecuteMovementBehavior();
                }
                
                EnterRecoveryPhase();
            }
            break;
        }
        case EValidationPhase::Recovery:
        {
            break;
        }
    }
}

void UDynamicGameplayAbility::HandleInputStep(FGameplayTag ActionTag, bool bIsPressed)
{
    if (!IsActive() || !bIsAwaitingQTE) return;

    const FAbilityStageInputSequence* Seq = AbilityConfig.FindStageSequence(CurrentPhase);
    if (!Seq || !Seq->Steps.IsValidIndex(CurrentInputStepIndex)) return;

    const FAbilityInputStep& ExpectedStep = Seq->Steps[CurrentInputStepIndex];

    if (ActionTag != ExpectedStep.RequiredInputAction)
    {
        if (bIsPressed) OnQTEWindowFailed();
        return;
    }

    if (ExpectedStep.bIsHold && !bIsPressed)
    {
        OnQTEWindowFailed();
        return;
    }
    else if (!ExpectedStep.bIsHold && !bIsPressed)
    {
        return; 
    }

    // --- VFX: QTE Step Success ---
    if (AActor* Avatar = GetAvatarActorFromActorInfo())
    {
        const FAbilityStagePresentation* Pres = nullptr;
        switch(CurrentPhase)
        {
            case EValidationPhase::Attempt: Pres = &AbilityConfig.StageVisuals.AttemptStage; break;
            case EValidationPhase::Charging: Pres = &AbilityConfig.StageVisuals.ChargingStage; break;
            case EValidationPhase::Release: Pres = &AbilityConfig.StageVisuals.ReleaseStage; break;
            case EValidationPhase::Recovery: Pres = &AbilityConfig.StageVisuals.RecoveryStage; break;
        }

        if (Pres && Pres->VfxEventMap.LocalLookupMap.Num() > 0)
        {
            if (UDynamicVFXSubsystem* VfxSub = Avatar->GetWorld()->GetSubsystem<UDynamicVFXSubsystem>())
            {
                static const FGameplayTag SuccessTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Stage.QTE.Success"), false);
                VfxSub->PlayVFXLayer(SuccessTag, Pres->VfxEventMap, Avatar->GetActorLocation(), Avatar->GetActorForwardVector(), 1.0f, Avatar, nullptr);
            }
        }
    }

    CurrentInputStepIndex++;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TimerHandle_QTEWindow);

    if (CurrentInputStepIndex >= Seq->Steps.Num())
    {
        bIsAwaitingQTE = false;
        ProceedWithStage(CurrentPhase);
    }
    else
    {
        const FAbilityInputStep& NextStep = Seq->Steps[CurrentInputStepIndex];
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(TimerHandle_QTEWindow, this, &UDynamicGameplayAbility::OnQTEWindowFailed, NextStep.TimeWindowSeconds, false);
        }
    }
}

void UDynamicGameplayAbility::OnQTEWindowFailed()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TimerHandle_QTEWindow);
    bIsAwaitingQTE = false;

    if (const FAbilityStageInputSequence* Seq = AbilityConfig.FindStageSequence(CurrentPhase))
    {
        UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
        
        // --- VFX: QTE Stage Failure ---
        if (AActor* Avatar = GetAvatarActorFromActorInfo())
        {
            const FAbilityStagePresentation* Pres = nullptr;
            switch(CurrentPhase)
            {
                case EValidationPhase::Attempt: Pres = &AbilityConfig.StageVisuals.AttemptStage; break;
                case EValidationPhase::Charging: Pres = &AbilityConfig.StageVisuals.ChargingStage; break;
                case EValidationPhase::Release: Pres = &AbilityConfig.StageVisuals.ReleaseStage; break;
                case EValidationPhase::Recovery: Pres = &AbilityConfig.StageVisuals.RecoveryStage; break;
            }

            if (Pres && Pres->VfxEventMap.LocalLookupMap.Num() > 0)
            {
                if (UDynamicVFXSubsystem* VfxSub = Avatar->GetWorld()->GetSubsystem<UDynamicVFXSubsystem>())
                {
                    static const FGameplayTag FailTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Stage.QTE.Failure"), false);
                    VfxSub->PlayVFXLayer(FailTag, Pres->VfxEventMap, Avatar->GetActorLocation(), Avatar->GetActorForwardVector(), 1.0f, Avatar, nullptr);
                }
            }
        }

        if (ASC)
        {
            if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
            {
                for (const FDynamicModifierDef& Mod : Seq->PenaltyModifiers)
                {
                    if (!Mod.TargetAttributeTag.IsValid()) continue;
                    const float CurVal = AttrSet->GetDynamicAttributeValue(Mod.TargetAttributeTag, 0);
                    AttrSet->SetDynamicAttributeValue(Mod.TargetAttributeTag, CurVal + Mod.BaseMagnitude, 0);
                }
            }
            for (const FGameplayTag& EffTag : Seq->PenaltyEffects)
            {
                FGameplayEffectSpecHandle SpecHandle = UDynamicEffectFactory::CreateDynamicEffectSpec(ASC, EffTag, 1);
                if (SpecHandle.IsValid())
                {
                    ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
                }
            }
        }

        for (const auto& ParamPair : Seq->ParameterAdjustmentsOnFailure.LocalLookupMap)
        {
            QTEParameterAdjustments.LocalLookupMap.Add(ParamPair.Key, ParamPair.Value);
        }

        if (Seq->bProceedOnFailure) ProceedWithStage(CurrentPhase);
        else if (Seq->bCancelAbilityOnFailure) CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
    }
}

void UDynamicGameplayAbility::FinalizeRelease(float FinalChargeAlpha)
{
    if (ActiveChargeTask)
    {
        ActiveChargeTask->EndTask();
        ActiveChargeTask = nullptr;
    }
    EvaluateStageEntry(EValidationPhase::Release); 
}

void UDynamicGameplayAbility::HandleTickDrain(float TotalElapsedTime, float DeltaTime)
{
    RuntimeTrackedChargeTime = TotalElapsedTime;
    const float MaxCharge = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
    const float ChargeAlpha = FMath::Clamp(RuntimeTrackedChargeTime / MaxCharge, 0.0f, 1.0f);

    // OPTIMIZATION: Throttle logic condition tree evaluation to 10 Hz to conserve GameThread budget
    if (TotalElapsedTime - LastComboEvalTime > 0.1f || LastComboEvalTime < 0.0f)
    {
        RefreshOpenComboWindows(RuntimeTrackedChargeTime, ChargeAlpha);
        LastComboEvalTime = TotalElapsedTime;
    }

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
    if (bServerApprovedActivation)
    {
        if (CurrentPhase == EValidationPhase::Charging)
        {
            bServerApprovedActivation = false;
            const float MaxTime = FMath::Max(0.01f, AbilityConfig.ChargeProfile.MaxChargeTime);
            FinalizeRelease(FMath::Clamp(RuntimeTrackedChargeTime / MaxTime, 0.1f, 1.0f));
        }
        else if (CurrentPhase == EValidationPhase::Release && AbilityConfig.ExecutionType == EAbilityExecutionType::Channeled)
        {
            // If the player releases the button while channeling, cut the channel short and enter recovery
            if (ActiveChannelTask)
            {
                ActiveChannelTask->EndTask();
                ActiveChannelTask = nullptr;
            }
            EnterRecoveryPhase();
        }
    }
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
        World->GetTimerManager().ClearTimer(TimerHandle_QTEWindow);
    }
    
    if (ActiveChargeTask) { ActiveChargeTask->EndTask(); ActiveChargeTask = nullptr; }
    if (ActiveChannelTask) { ActiveChannelTask->EndTask(); ActiveChannelTask = nullptr; }

    bActivatedViaComboTransition = false;
    CurrentlyOpenComboTags.Reset();
    
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UDynamicGameplayAbility::ExecuteInteractionBehavior(float ChargeAlpha)
{
    ADynamicCharacter* AvatarChar = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());
    if (!AvatarChar) return;

    UDynamicInteractableComponent* TargetInteractable = FindBestNearbyInteractable(CurrentActorInfo);
    
    // 1. Fallback: Physical Construction / Nailing (Requires Hammer & Materials)
    if (!TargetInteractable) 
    {
        static const FGameplayTag ConstructActionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Construct"), false);
        if (ConstructActionTag.IsValid() && AbilityConfig.InteractionConfig.InteractionActionTag.MatchesTag(ConstructActionTag))
        {
            if (UWorld* World = AvatarChar->GetWorld())
            {
                const FVector Start = AvatarChar->GetActorLocation();
                const FVector End = Start + (AvatarChar->GetActorForwardVector() * AbilityConfig.InteractionConfig.MaxInteractionDistance);
                
                FHitResult BuildHit;
                FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildingConstructionSweep), false, AvatarChar);
                
                if (World->SweepSingleByChannel(BuildHit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(AbilityConfig.InteractionConfig.TargetSweepRadius), Params))
                {
                    if (UHierarchicalInstancedStaticMeshComponent* HitHISM = Cast<UHierarchicalInstancedStaticMeshComponent>(BuildHit.GetComponent()))
                    {
                        if (ADynamicBuildingChunkActor* Chunk = Cast<ADynamicBuildingChunkActor>(BuildHit.GetActor()))
                        {
                            if (const TMap<int32, FGuid>* InstMap = Chunk->InstanceHitMap.Find(HitHISM))
                            {
                                if (const FGuid* TargetGuid = InstMap->Find(BuildHit.Item))
                                {
                                    Chunk->ServerProvideMaterialToGhost(*TargetGuid, AvatarChar);
                                    Chunk->ServerApplyConstructionLabor(*TargetGuid, AvatarChar, 1.0f * ChargeAlpha);
                                    
                                    PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                                    GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Physical Terrain & Foliage Mechanical Interaction (Mining, Logging, Shoveling, Cleaving)
    if (!TargetInteractable) 
    {
        UWorld* World = AvatarChar->GetWorld();
        ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

        if (World && GS)
        {
            const FVector Start = AvatarChar->GetActorLocation();
            const FVector End = Start + (AvatarChar->GetActorForwardVector() * AbilityConfig.InteractionConfig.MaxInteractionDistance);

            // Assemble mechanical stress profile from ability and equipped main-hand tool
            FGameplayTagContainer MechanicalImpactTags = AbilityConfig.InteractionConfig.ImpactTypeTags;
            float TotalToolForce = 500.0f * ChargeAlpha;

            if (UDynamicEquipmentComponent* EquipComp = AvatarChar->FindComponentByClass<UDynamicEquipmentComponent>())
            {
                static const FGameplayTag RightHandSlotTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Equipment.Hand.Right"), false);
                const FGameplayTag EquippedTag = EquipComp->GetEquippedItemInSlot(RightHandSlotTag);
                if (EquippedTag.IsValid())
                {
                    if (const FDynamicPhysicsActorDef* ToolDef = GS->MasterPhysicsActorRegistry.Find(EquippedTag))
                    {
                        MechanicalImpactTags.AppendTags(ToolDef->ImpactTypeTags);
                        TotalToolForce += (ToolDef->MassOverride * 200.0f);
                    }
                }
            }

            // NO MORE HARDCODING: Rely on the external ability configuration for default stress typing
            if (MechanicalImpactTags.IsEmpty() && AbilityConfig.InteractionConfig.DefaultImpactTypeTag.IsValid())
            {
                MechanicalImpactTags.AddTag(AbilityConfig.InteractionConfig.DefaultImpactTypeTag);
            }

            if (const UDynamicAttributeSet* AttrSet = AvatarChar->GetDynamicAttributeSet())
            {
                static const FGameplayTag StrengthStatTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stat.Strength"), false);
                if (StrengthStatTag.IsValid())
                {
                    TotalToolForce += AttrSet->GetDynamicAttributeValue(StrengthStatTag, 0) * 15.0f;
                }
            }

            // A. Interact with Voxel Terrain via Material Science Evaluation
            FHitResult VoxelHit;
            FCollisionQueryParams VoxelParams(SCENE_QUERY_STAT(VoxelMechanicalSweep), false, AvatarChar);

            if (World->SweepSingleByChannel(VoxelHit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(AbilityConfig.InteractionConfig.TargetSweepRadius), VoxelParams))
            {
                if (ADynamicVoxelChunkActor* HitChunk = Cast<ADynamicVoxelChunkActor>(VoxelHit.GetActor()))
                {
                    if (UDynamicVoxelSubsystem* VoxelSub = World->GetSubsystem<UDynamicVoxelSubsystem>())
                    {
                        const FVector DigLocation = VoxelHit.ImpactPoint - (VoxelHit.ImpactNormal * 25.0f);
                        FGameplayTag YieldArchetype;
                        bool bDestroyed = false;

                        if (VoxelSub->ServerApplyMechanicalStressAtLocation(DigLocation, MechanicalImpactTags, TotalToolForce, AvatarChar, YieldArchetype, bDestroyed))
                        {
                            PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                            GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);

                            if (bDestroyed && YieldArchetype.IsValid())
                            {
                                UDynamicContainerComponent* Inventory = AvatarChar->FindComponentByClass<UDynamicContainerComponent>();
                                const FGuid OwnerGuid = AvatarChar->FindComponentByClass<UDynamicSocialComponent>() ? AvatarChar->FindComponentByClass<UDynamicSocialComponent>()->CharacterPersistentGuid : FGuid();

                                if (!Inventory || !Inventory->ServerAddActorByTag(YieldArchetype, 1, TArray<FDynamicAttributeItem>(), OwnerGuid))
                                {
                                    FTransform DropTransform(FRotator::ZeroRotator, VoxelHit.ImpactPoint + (VoxelHit.ImpactNormal * 40.0f));
                                    if (ADynamicPhysicsActor* DroppedLoot = GS->AcquirePhysicsActor(YieldArchetype, DropTransform, AvatarChar, AvatarChar))
                                    {
                                        if (UPrimitiveComponent* Prim = DroppedLoot->GetActivePhysicsPrimitive())
                                        {
                                            Prim->SetSimulatePhysics(true);
                                        }
                                    }
                                }
                            }
                            return;
                        }
                    }
                }
            }

            // B. Interact with Dynamic Foliage, Trees & Harvestable Physics Actors in Range
            TArray<FOverlapResult> Overlaps;
            FCollisionShape QuerySphere = FCollisionShape::MakeSphere(AbilityConfig.InteractionConfig.MaxInteractionDistance);
            FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(FoliageMechanicalSweep), false, AvatarChar);

            if (World->OverlapMultiByChannel(Overlaps, AvatarChar->GetActorLocation(), FQuat::Identity, ECC_PhysicsBody, QuerySphere, OverlapParams))
            {
                for (const FOverlapResult& Overlap : Overlaps)
                {
                    AActor* OverlapActor = Overlap.GetActor();
                    // Resolves C2446: Compares AActor* with AvatarChar (which implicitly upcasts to AActor*)
                    if (OverlapActor && OverlapActor != AvatarChar && !OverlapActor->IsPendingKillPending())
                    {
                        if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(OverlapActor))
                        {
                            const FVector HitDir = (PhysActor->GetActorLocation() - AvatarChar->GetActorLocation()).GetSafeNormal();
                            PhysActor->ServerProcessAuthoritativeLimbHit(
                                FName("root"), 
                                TotalToolForce, 
                                1.0f, 
                                PhysActor->GetActorLocation(), 
                                HitDir, 
                                AvatarChar
                            );

                            PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                            GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
                            return;
                        }
                    }
                }
            }
        }
    }

    if (!TargetInteractable) return;
    
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
    
    // 3. STORE ACTION (Physical Item Ingestion)
    static const FGameplayTag StoreActionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Store"), false);
    if (StoreActionTag.IsValid() && ActionTag.MatchesTag(StoreActionTag))
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
    // 4. LOOT / EXTRACT ACTION
    else if (static const FGameplayTag LootActionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Loot"), false);
             LootActionTag.IsValid() && ActionTag.MatchesTag(LootActionTag))
    {
        if (UDynamicContainerComponent* TargetContainer = TargetOwnerActor->FindComponentByClass<UDynamicContainerComponent>())
        {
            int32 ExtractX = 0;
            int32 ExtractY = 0;
            if (TargetContainer->ServerExtractActorToHands(ExtractX, ExtractY, AvatarChar, AbilityConfig.InteractionConfig.CasterEffectorBone))
            {
                PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
            }
        }
    }
    // 5. READ LORE / SIGNPOST ACTION
    else if (static const FGameplayTag ReadActionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Action.Read"), false);
             ReadActionTag.IsValid() && ActionTag.MatchesTag(ReadActionTag))
    {
        if (UDynamicLanguageComponent* LangComp = AvatarChar->FindComponentByClass<UDynamicLanguageComponent>())
        {
            if (AbilityConfig.InteractionConfig.ReadableLanguageTag.IsValid() && !AbilityConfig.InteractionConfig.ReadableTextPayload.IsEmpty())
            {
                LangComp->ServerReadWrittenText(
                    AbilityConfig.InteractionConfig.ReadableLanguageTag, 
                    AbilityConfig.InteractionConfig.ReadableTextPayload, 
                    TargetOwnerActor
                );
                PlayStagePresentation(AbilityConfig.StageVisuals.ImpactStage);
                GrantSourcePassBack(EPassBackTriggerEvent::OnCollision, ChargeAlpha);
            }
        }
    }
    // 6. STANDARD WORLD INTERACTION (Doors, Levers, Mounts, Tethers, Physics Pushes)
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
                        FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(Spec.Data->GetContext().Get());
                        if (DynCtx)
                        {
                            DynCtx->ContextPayloadTags = AbilityConfig.InteractionConfig.ImpactTypeTags;
                        }
                        SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                    }
                }
            }
        }
    }
}

bool UDynamicGameplayAbility::IsValidTargetForSpatialDelivery(const FDynamicSpatialDeliveryDef& Delivery, const AActor* TargetActor) const
{
    if (!IsValid(TargetActor) || TargetActor->IsActorBeingDestroyed())
    {
        return false;
    }

    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (TargetActor == Avatar)
    {
        return false; // Prevent self-hit
    }

    const UAbilitySystemComponent* TargetASC = TargetActor->FindComponentByClass<UAbilitySystemComponent>();
    if (!TargetASC)
    {
        return false; // Target cannot receive GameplayEffects or GAS state tracking
    }

    FGameplayTagContainer TargetTags;
    TargetASC->GetOwnedGameplayTags(TargetTags);

    // 1. Mandatory Liveness & Invulnerability Check
    static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), false);
    static const FGameplayTag InvulnerableTag = FGameplayTag::RequestGameplayTag(TEXT("State.Invulnerable"), false);

    if (DeadTag.IsValid() && TargetTags.HasTag(DeadTag))
    {
        return false; // Disallow damaging or applying AoE to dead entities
    }
    if (InvulnerableTag.IsValid() && TargetTags.HasTag(InvulnerableTag))
    {
        return false;
    }

    // 2. Data-Driven Blocked Tags Check
    if (Delivery.BlockedTargetTags.Num() > 0 && TargetTags.HasAny(Delivery.BlockedTargetTags))
    {
        return false;
    }

    // 3. Data-Driven Required Target Tags Check
    if (Delivery.RequiredTargetTags.Num() > 0 && !TargetTags.HasAll(Delivery.RequiredTargetTags))
    {
        return false;
    }

    // 4. Dedicated Target Validation Condition Tree (Opposing Faction, Line of Sight, etc.)
    if (!Delivery.TargetValidationConditionTree.IsNone() && Avatar)
    {
        if (const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(Avatar, Delivery.TargetValidationConditionTree))
        {
            const UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
            FGameplayTagContainer SourceTags;
            if (SourceASC) SourceASC->GetOwnedGameplayTags(SourceTags);

            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, SourceASC, TargetASC, SourceTags, TargetTags))
            {
                return false;
            }
        }
    }

    return true;
}

void UDynamicGameplayAbility::ExecuteSpatialDelivery(float ChargeAlpha)
{
    if (!HasAuthority(&CurrentActivationInfo)) return;
    AActor* Avatar = GetAvatarActorFromActorInfo();
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    UDynamicVFXSubsystem* VfxSub = World ? World->GetSubsystem<UDynamicVFXSubsystem>() : nullptr;
    if (!Avatar || !GS) return;

    const FDynamicSpatialDeliveryDef& Delivery = AbilityConfig.SpatialDelivery;
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    // --- AURA / IMMEDIATE AoE (StationaryGround or AttachedToCaster) ---
    if ((Delivery.MotionType == ESpatialDeliveryMotion::AttachedToCaster || Delivery.MotionType == ESpatialDeliveryMotion::StationaryGround) &&
        (Delivery.ApplicationType == EAuraEffectApplicationType::SingleHit || Delivery.ApplicationType == EAuraEffectApplicationType::TickInterval) &&
        Delivery.Duration <= 0.25f) // Immediate burst
    {
        // VFX: Spawn Burst Aura
        if (VfxSub && AbilityConfig.StageVisuals.ReleaseStage.VfxEventMap.LocalLookupMap.Num() > 0)
        {
            static const FGameplayTag DeliverySpawnEventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Delivery.Spawn"), false);
            VfxSub->PlayVFXLayer(DeliverySpawnEventTag, AbilityConfig.StageVisuals.ReleaseStage.VfxEventMap, Avatar->GetActorLocation(), FVector::UpVector, Delivery.MaxRadius * ChargeAlpha, Avatar, nullptr);
        }

        TArray<FHitResult> HitResults;
        FCollisionShape SphereShape = FCollisionShape::MakeSphere(Delivery.MaxRadius * ChargeAlpha);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InstantSpatialNovaSweep), false, Avatar);

        if (World->SweepMultiByChannel(HitResults, Avatar->GetActorLocation(), Avatar->GetActorLocation() + FVector(0, 0, 1.0f), FQuat::Identity, ECC_Pawn, SphereShape, QueryParams))
        {
            TSet<AActor*> ProcessedActors;
            for (const FHitResult& Hit : HitResults)
            {
                AActor* HitActor = Hit.GetActor();
                if (!HitActor || ProcessedActors.Contains(HitActor)) continue;
                ProcessedActors.Add(HitActor);

                if (!IsValidTargetForSpatialDelivery(Delivery, HitActor)) continue;

                ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(HitActor);
                const FName BoneName = (Hit.BoneName != NAME_None) ? Hit.BoneName : FName("pelvis");
                const FVector OutwardNormal = (Avatar->GetActorLocation() - HitActor->GetActorLocation()).GetSafeNormal();
                const float ScaledImpulse = Delivery.KineticImpulseOnHit * ChargeAlpha;

                // VFX: Aura Impact on Target
                if (VfxSub && AbilityConfig.StageVisuals.ImpactStage.VfxEventMap.LocalLookupMap.Num() > 0)
                {
                    static const FGameplayTag DeliveryImpactEventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Delivery.Impact"), false);
                    VfxSub->PlayVFXLayer(DeliveryImpactEventTag, AbilityConfig.StageVisuals.ImpactStage.VfxEventMap, HitActor->GetActorLocation(), OutwardNormal, ScaledImpulse, Avatar, HitActor);
                }

                if (HitChar)
                {
                    HitChar->ServerProcessAuthoritativeLimbHit(
                        HitChar, BoneName, ScaledImpulse, ChargeAlpha, HitActor->GetActorLocation(), OutwardNormal, Avatar, Delivery.ImpactTypeTags
                    );
                }

                if (UAbilitySystemComponent* TargetASC = HitActor->FindComponentByClass<UAbilitySystemComponent>())
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
                                DynCtx->SetAuthoritativePhysicsPayload(LimbId, ScaledImpulse, ChargeAlpha, OutwardNormal, HitActor->GetActorLocation());
                                DynCtx->SourcePassBackRules = AbilityConfig.SourcePassBackRules;
                                DynCtx->ContextPayloadTags = Delivery.ImpactTypeTags;
                            }
                            SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                        }
                    }
                }
            }
        }
        return;
    }

    // --- CONTINUOUS PROJECTILE / PERSISTENT SPATIAL SPAWN ---
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

    AActor* HomingTarget = nullptr;
    if (Delivery.MotionType == ESpatialDeliveryMotion::Homing)
    {
        if (UDynamicSocialComponent* SocialComp = Avatar->FindComponentByClass<UDynamicSocialComponent>())
        {
            HomingTarget = SocialComp->ActiveInterlocutor.Get();
        }
    }

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
            
            // VFX: Projectile or Aura Deployed
            if (VfxSub && AbilityConfig.StageVisuals.ReleaseStage.VfxEventMap.LocalLookupMap.Num() > 0)
            {
                static const FGameplayTag DeliverySpawnEventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Delivery.Spawn"), false);
                VfxSub->PlayVFXLayer(DeliverySpawnEventTag, AbilityConfig.StageVisuals.ReleaseStage.VfxEventMap, SpawnTM.GetLocation(), SpawnTM.GetRotation().GetForwardVector(), Delivery.MaxRadius * ChargeAlpha, Avatar, HomingTarget);
            }

            SpatialActor->ActivateSpatialDelivery(
                SourceASC,
                Delivery,
                AbilityConfig.SourcePassBackRules,
                FinalSpeed,
                PowerMultiplierPerInstance,
                SpawnTM.GetRotation().GetForwardVector(),
                HomingTarget
            );
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

    FGameplayTagContainer ChainImpactTags = AbilityConfig.ChainConfig.ImpactTypeTags;
    if (ChainImpactTags.IsEmpty())
    {
        ChainImpactTags = AbilityConfig.SpatialDelivery.ImpactTypeTags;
    }

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChainAbility), false);
    QueryParams.AddIgnoredActor(GetAvatarActorFromActorInfo());

    for (int32 CurrentBounce = 0; CurrentBounce < MaxBounces && CurrentTarget; ++CurrentBounce)
    {
        const float ModdedDamage = BaseDamage * FMath::Pow(Falloff, CurrentBounce);

        if (ADynamicCharacter* HitChar = Cast<ADynamicCharacter>(CurrentTarget))
        {
            HitChar->ServerProcessAuthoritativeLimbHit(
                HitChar, 
                FName("pelvis"), 
                ModdedDamage, 
                1.0f, 
                HitChar->GetActorLocation(), 
                FVector::UpVector, 
                GetAvatarActorFromActorInfo(), 
                ChainImpactTags
            );
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

void UDynamicGameplayAbility::HandleChannelFinished(bool bCompletedSuccessfully)
{
    if (ActiveChannelTask)
    {
        ActiveChannelTask = nullptr; // Task automatically ends itself
    }
    // Once the channel is complete, move to recovery
    EnterRecoveryPhase();
}