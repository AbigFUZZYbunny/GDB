#include "DynamicEvolutionComponent.h"
#include "DynamicGameState.h"
#include "DynamicInteractableComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicAttributeSet.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UDynamicEvolutionComponent::UDynamicEvolutionComponent()
{
    // ZERO TICK OVERHEAD: Fully decoupled from per-frame or polling tick loops
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UDynamicEvolutionComponent::BeginPlay()
{
    Super::BeginPlay();

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        LastScheduledCalculationWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        RecalculateEvolutionSchedule();
    }
}

void UDynamicEvolutionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_StageAdvancement);
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicEvolutionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicEvolutionComponent, CurrentProgressValue);
    DOREPLIFETIME(UDynamicEvolutionComponent, CurrentStageTag);
    DOREPLIFETIME(UDynamicEvolutionComponent, TargetStageCompletionWorldTime);
}

void UDynamicEvolutionComponent::OnRep_EvolutionData()
{
    // Visual and attribute sync for late-joining and proxy clients
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FEvolutionProfileDef* Profile = GS->EvolutionProfilesRegistry.Find(EvolutionProfileTag);
    if (!Profile || Profile->ProgressionPhases.Num() == 0) return;

    const float NormProgress = (Profile->TotalLifespanOrMaxXP > 0.0f) ? (CurrentProgressValue / Profile->TotalLifespanOrMaxXP) : 0.0f;
    for (const FEvolutionStageKeyframe& Frame : Profile->ProgressionPhases)
    {
        if (Frame.StageTag == CurrentStageTag)
        {
            if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
            {
                RootPrim->SetWorldScale3D(FVector(Frame.ScaleMultiplier));
            }
            OnEvolutionStageAdvanced.Broadcast(CurrentStageTag, NormProgress, Frame.ScaleMultiplier);
            break;
        }
    }
}

void UDynamicEvolutionComponent::OnRep_TargetProgressTimestamp()
{
    // Clients can use this to smoothly interpolate growth visuals locally without server RPC spam
}

void UDynamicEvolutionComponent::ServerAddProgress(float DeltaAmount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FEvolutionProfileDef* Profile = GS->EvolutionProfilesRegistry.Find(EvolutionProfileTag);
    if (!Profile) return;

    CurrentProgressValue = FMath::Clamp(CurrentProgressValue + DeltaAmount, 0.0f, Profile->TotalLifespanOrMaxXP);
    EvaluateEvolution(CurrentProgressValue / Profile->TotalLifespanOrMaxXP);
    RecalculateEvolutionSchedule();
}

void UDynamicEvolutionComponent::RecalculateEvolutionSchedule()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    UWorld* World = GetWorld();
    if (!World) return;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return;

    const FEvolutionProfileDef* Profile = GS->EvolutionProfilesRegistry.Find(EvolutionProfileTag);
    if (!Profile || Profile->ProgressionPhases.Num() == 0) return;

    const double Now = World->GetTimeSeconds();

    // 1. Account for progress accrued during previous interval
    if (LastScheduledCalculationWorldTime > 0.0 && LastScheduledCalculationWorldTime < Now)
    {
        const float ElapsedSecs = static_cast<float>(Now - LastScheduledCalculationWorldTime);
        const float RateMultiplier = CalculateEnvironmentalProgressMultiplier();
        if (RateMultiplier > 0.0f)
        {
            CurrentProgressValue = FMath::Clamp(CurrentProgressValue + (ElapsedSecs * RateMultiplier), 0.0f, Profile->TotalLifespanOrMaxXP);
            EvaluateEvolution(CurrentProgressValue / Profile->TotalLifespanOrMaxXP);
        }
    }
    LastScheduledCalculationWorldTime = Now;

    // 2. Identify the next milestone keyframe
    const float CurrentNorm = (Profile->TotalLifespanOrMaxXP > 0.0f) ? (CurrentProgressValue / Profile->TotalLifespanOrMaxXP) : 0.0f;
    const FEvolutionStageKeyframe* NextKeyframe = nullptr;

    for (const FEvolutionStageKeyframe& Frame : Profile->ProgressionPhases)
    {
        if (Frame.NormalizedProgress > CurrentNorm)
        {
            NextKeyframe = &Frame;
            break;
        }
    }

    World->GetTimerManager().ClearTimer(TimerHandle_StageAdvancement);

    // If fully evolved, retire the timer
    if (!NextKeyframe)
    {
        TargetStageCompletionWorldTime = 0.0;
        return;
    }

    // 3. Compute duration to next milestone
    const float EffectiveRate = CalculateEnvironmentalProgressMultiplier();
    if (EffectiveRate <= 0.0f)
    {
        // Stagnant / Dormant: Do not schedule a timer; will re-awaken when biome/environment changes
        TargetStageCompletionWorldTime = -1.0;
        return;
    }

    const float ProgressNeeded = (NextKeyframe->NormalizedProgress * Profile->TotalLifespanOrMaxXP) - CurrentProgressValue;
    const float SecondsUntilNextStage = FMath::Max(0.1f, ProgressNeeded / EffectiveRate);

    TargetStageCompletionWorldTime = Now + SecondsUntilNextStage;

    // 4. Schedule single low-cost wake-up timer
    World->GetTimerManager().SetTimer(TimerHandle_StageAdvancement, this, &UDynamicEvolutionComponent::HandleScheduledStageReached, SecondsUntilNextStage, false);
}

void UDynamicEvolutionComponent::HandleScheduledStageReached()
{
    RecalculateEvolutionSchedule();
}

void UDynamicEvolutionComponent::EvaluateEvolution(float NormalizedProgress)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FEvolutionProfileDef* Profile = GS->EvolutionProfilesRegistry.Find(EvolutionProfileTag);
    if (!Profile || Profile->ProgressionPhases.Num() == 0) return;

    FEvolutionStageKeyframe ActiveFrame = Profile->ProgressionPhases[0];
    for (const FEvolutionStageKeyframe& Frame : Profile->ProgressionPhases)
    {
        if (NormalizedProgress >= Frame.NormalizedProgress)
        {
            ActiveFrame = Frame;
        }
    }

    if (ActiveFrame.StageTag != CurrentStageTag)
    {
        CurrentStageTag = ActiveFrame.StageTag;

        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
        {
            RootPrim->SetWorldScale3D(FVector(ActiveFrame.ScaleMultiplier));
        }

        if (UDynamicInteractableComponent* Interactable = GetOwner()->FindComponentByClass<UDynamicInteractableComponent>())
        {
            for (const FGameplayTag& Tag : ActiveFrame.TagsToApply)
            {
                Interactable->AddStateTag(Tag);
            }
        }

        if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(GetOwner()))
        {
            if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(PhysActor->GetDynamicAttributeSet()))
            {
                for (const FTagFloatPair& Pair : ActiveFrame.StageAttributeModifiers)
                {
                    if (Pair.KeyTag.IsValid())
                    {
                        AttrSet->SetDynamicAttributeValue(Pair.KeyTag, Pair.Value, 0);
                    }
                }
            }
        }

        OnEvolutionStageAdvanced.Broadcast(CurrentStageTag, NormalizedProgress, ActiveFrame.ScaleMultiplier);
    }
}

float UDynamicEvolutionComponent::CalculateEnvironmentalProgressMultiplier() const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !GetOwner()) return 1.0f;

    const FEvolutionProfileDef* Profile = GS->EvolutionProfilesRegistry.Find(EvolutionProfileTag);
    if (!Profile || Profile->EnvironmentalModifiers.Num() == 0) return 1.0f;

    float CombinedMultiplier = 1.0f;
    const FVector WorldLoc = GetOwner()->GetActorLocation();

    for (const FEnvironmentalRateModifier& Mod : Profile->EnvironmentalModifiers)
    {
        float MetricVal = GS->GetEnvironmentMetricAtLocation(WorldLoc, Mod.EnvironmentMetricTag);
        if (const float* LocalOffset = LocalEnvironmentOffsets.LocalLookupMap.Find(Mod.EnvironmentMetricTag))
        {
            MetricVal += *LocalOffset;
        }

        if (Mod.bPauseOutsideRange && (MetricVal < Mod.MinThreshold || MetricVal > Mod.MaxThreshold))
        {
            return 0.0f;
        }

        const float Alpha = FMath::GetRangePct(Mod.MinThreshold, Mod.MaxThreshold, MetricVal);
        const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
        const float ModFactor = FMath::Lerp(Mod.MinRateMultiplier, Mod.MaxRateMultiplier, ClampedAlpha);
        CombinedMultiplier *= ModFactor;
    }

    return FMath::Max(0.0f, CombinedMultiplier);
}