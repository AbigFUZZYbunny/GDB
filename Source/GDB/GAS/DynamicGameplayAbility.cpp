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

UDynamicGameplayAbility::UDynamicGameplayAbility()
{
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
    
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UDynamicGameplayAbility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicGameplayAbility, AbilityConfig);
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
    FGameplayTagContainer SourceTags; FGameplayTagContainer TargetTags; if (ASC) ASC->GetOwnedGameplayTags(TargetTags);
    return UDynamicLogicEvaluator::EvaluateConditionTree(*ValidationTree, ASC, ASC, SourceTags, TargetTags);
}

void UDynamicGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    CurrentPhase = EValidationPhase::Attempt; RuntimeTrackedChargeTime = 0.0f;
    
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo)) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
    
    bServerApprovedActivation = true;

    // Redirect to specialized mechanics routines based strictly on JSON metadata assignments
    switch (AbilityConfig.ExecutionType)
    {
    case EAbilityExecutionType::Charging:
        { 
            UAT_WaitChargeTime* ChargeTask = UAT_WaitChargeTime::WaitChargeTime(this, AbilityConfig.ChargeProfile.MaxChargeTime);
            if (ChargeTask)
            {
                ChargeTask->OnChargeTick.AddDynamic(this, &UDynamicGameplayAbility::HandleTickDrain);
                ChargeTask->OnMaxChargeReached.AddDynamic(this, &UDynamicGameplayAbility::HandleMaxChargeReached);
                ChargeTask->ReadyForActivation();
            }
            break;
        }
    case EAbilityExecutionType::Aura:
        ExecuteAuraBehavior();
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
        
        // FIX: Added 'nullptr' as the 3rd argument to match the overridden CheckCost signature
        if (!CheckCost(CurrentSpecHandle, CurrentActorInfo, nullptr)) 
        {
            CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
        }
    }
}

void UDynamicGameplayAbility::HandleMaxChargeReached()
{
    if (AbilityConfig.ChargeProfile.MaxChargeResolution == EMaxChargeResolution::MutateToOtherAbility)
    {
        if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
        {
            if (HasAuthority(&CurrentActivationInfo))
            {
                FGameplayEventData Payload; ASC->HandleGameplayEvent(AbilityConfig.ChargeProfile.MutationAbilityTag, &Payload);
            }
        }
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false); return;
    }
    FinalizeRelease(1.0f);
}

void UDynamicGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
    if (bServerApprovedActivation && AbilityConfig.ExecutionType == EAbilityExecutionType::Charging && RuntimeTrackedChargeTime < AbilityConfig.ChargeProfile.MaxChargeTime)
    {
        bServerApprovedActivation = false;
        FinalizeRelease(FMath::Clamp(RuntimeTrackedChargeTime / AbilityConfig.ChargeProfile.MaxChargeTime, 0.0f, 1.0f));
    }
}

void UDynamicGameplayAbility::FinalizeRelease(float FinalChargeAlpha)
{
    CurrentPhase = EValidationPhase::Release;
    if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo)) { CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true); return; }

    AActor* Avatar = GetAvatarActorFromActorInfo();
    UDynamicGASManagerComponent* Registry = Avatar ? Avatar->FindComponentByClass<UDynamicGASManagerComponent>() : nullptr;
    if (Registry)
    {
        FGameplayTag ProjLink = AbilityConfig.ProjectileConfig.ArchetypeLinkTag;
        if (ProjLink.IsValid())
        {
            float FinalSpeed = Registry->ResolveMetric(ProjLink, FGameplayTag::RequestGameplayTag("Metric.Movement.Speed"), AbilityConfig.ProjectileConfig.RuntimeMetricModifiers);
            FinalSpeed *= (1.0f + (FinalChargeAlpha * AbilityConfig.ChargeProfile.CostScalingMultiplier));
        }
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// --- Aura Implementation ---
void UDynamicGameplayAbility::ExecuteAuraBehavior()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar) return;

    // Auras automatically anchor directly to the Instigator location without a spawn point calculation
    float Radius = AbilityConfig.AuraConfig.AuraMetrics.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Aura.Param.Radius"));
    float TickDuration = AbilityConfig.AuraConfig.AuraMetrics.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Aura.Param.TickRate"));

    // Server spawns a local attached collision trigger actor or applies dynamic cyclical loop attributes directly
    if (HasAuthority(&CurrentActivationInfo))
    {
        // Pseudocode: Spawn transient aura actor and attach it directly to the caster character mesh root
        // MyWorld->SpawnActorDeferred(...); AttachToComponent(Avatar->GetRootComponent());
    }

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// --- Chain Implementation ---
void UDynamicGameplayAbility::ExecuteChainBehavior(const FGameplayEventData* TriggerEventData)
{
    if (!HasAuthority(&CurrentActivationInfo)) return;
    
    AActor* CurrentTarget = (TriggerEventData && TriggerEventData->Target.Get()) ? 
        const_cast<AActor*>(TriggerEventData->Target.Get()) : nullptr;

    int32 MaxBounces = AbilityConfig.ChainConfig.MaxBounces;
    float Falloff = AbilityConfig.ChainConfig.DamageFalloffPerBounce;
    float Radius = AbilityConfig.ChainConfig.BounceSearchRadius;

    float BaseDamage = AbilityConfig.NumericParams.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Ability.Param.BaseDamage"));
    TArray<AActor*> IgnoredTargets;
    IgnoredTargets.Add(GetAvatarActorFromActorInfo());

    // Execute iterative tracking sweeps across consecutive entity link rows
    for (int32 CurrentBounce = 0; CurrentBounce < MaxBounces; ++CurrentBounce)
    {
        // 1. Calculate drop-off scalar calculations natively over sequential increments
        float ModdedDamage = BaseDamage * FMath::Pow(Falloff, CurrentBounce);
        IgnoredTargets.Add(CurrentTarget);

        // Apply hit effects or damage metrics using calculated 'ModdedDamage' values here...

        // 2. Perform a spatial radius query to search for the next closest branch target object
        TArray<AActor*> Hits;
        TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
        ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

        UKismetSystemLibrary::SphereOverlapActors(GetWorld(), CurrentTarget->GetActorLocation(), Radius, ObjectTypes, nullptr, IgnoredTargets, Hits);

        if (Hits.Num() > 0)
        {
            // Lock target assignment row over onto the newly discovered target node location
            CurrentTarget = Hits[0]; 
        }
        else
        {
            break; // Target range cleared out: Break string link execution loop
        }
    }

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// --- Movement Override Mechanics (Dash / Roll / Wall Run) ---
void UDynamicGameplayAbility::ExecuteMovementBehavior()
{
    // 1. Recover our exact custom character type safely
    ADynamicCharacter* CustomPawn = Cast<ADynamicCharacter>(GetAvatarActorFromActorInfo());
    if (!CustomPawn) return;

    UDynamicForceComponent* ForceComp = CustomPawn->FindComponentByClass<UDynamicForceComponent>();
    if (!ForceComp) return;

    // 2. Fetch the dynamic velocity variable configured straight out of your data registry profiles
    float VelocityIntensity = AbilityConfig.MovementConfig.MovementMetrics.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Movement.Param.Velocity"));
    if (FMath::IsNearlyZero(VelocityIntensity))
    {
        VelocityIntensity = 1000.0f; // Stable runtime default fallback
    }

    // 3. Extrapolate trajectory direction safely via standard actor rotations
    FVector ForwardDirection = CustomPawn->GetActorForwardVector();

    // 4. Queue the impulse onto our independent custom worker module
    ForceComp->QueueInstantMovementImpulse(ForwardDirection, VelocityIntensity);

    // 5. Safely conclude the ability lifecycle
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDynamicGameplayAbility::UpdateConfigAtRuntime_Implementation(FDynamicAbilityDef NewConfigLayout)
{
    // Allows safe leveling system modification on server, which automatically updates client representations
    AbilityConfig = NewConfigLayout;
    
    // Notify your broadcast delegates
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
}

void UDynamicGameplayAbility::OnRep_AbilityConfig()
{
    // Notify local systems or UI elements that the ability configuration has changed
    OnAbilityConfigChanged.Broadcast(AbilityConfig);
}