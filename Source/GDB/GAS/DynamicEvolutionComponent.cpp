#include "DynamicEvolutionComponent.h"
#include "DynamicGameState.h"
#include "DynamicInteractableComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"

UDynamicEvolutionComponent::UDynamicEvolutionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 2.0f; // Optimized check interval for world objects
    SetIsReplicatedByDefault(true);
}

void UDynamicEvolutionComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UDynamicEvolutionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    const float EnvMultiplier = CalculateEnvironmentalProgressMultiplier();
    if (EnvMultiplier > 0.0f)
    {
        ServerAddProgress(DeltaTime * EnvMultiplier);
    }
}

void UDynamicEvolutionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicEvolutionComponent, CurrentProgressValue);
    DOREPLIFETIME(UDynamicEvolutionComponent, CurrentStageTag);
}

void UDynamicEvolutionComponent::OnRep_EvolutionData()
{
    // Client-side synchronization update
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

        // 1. Apply physical mesh scale
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
        {
            RootPrim->SetWorldScale3D(FVector(ActiveFrame.ScaleMultiplier));
        }

        // 2. Apply Stage Tags to Interactable Component if present
        if (UDynamicInteractableComponent* Interactable = GetOwner()->FindComponentByClass<UDynamicInteractableComponent>())
        {
            for (const FGameplayTag& Tag : ActiveFrame.TagsToApply)
            {
                Interactable->AddStateTag(Tag);
            }
        }

        // 3. Apply Stage Attribute Modifiers
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
        // Query world cell value + any local container/appliance offset (e.g., -25C inside an Icebox)
        float MetricVal = GS->GetEnvironmentMetricAtLocation(WorldLoc, Mod.EnvironmentMetricTag);
        if (const float* LocalOffset = LocalEnvironmentOffsets.LocalLookupMap.Find(Mod.EnvironmentMetricTag))
        {
            MetricVal += *LocalOffset;
        }

        if (Mod.bPauseOutsideRange && (MetricVal < Mod.MinThreshold || MetricVal > Mod.MaxThreshold))
        {
            return 0.0f; // Completely halted (e.g. plant frozen dormant)
        }

        const float Alpha = FMath::GetRangePct(Mod.MinThreshold, Mod.MaxThreshold, MetricVal);
        const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
        const float ModFactor = FMath::Lerp(Mod.MinRateMultiplier, Mod.MaxRateMultiplier, ClampedAlpha);

        CombinedMultiplier *= ModFactor;
    }

    return FMath::Max(0.0f, CombinedMultiplier);
}