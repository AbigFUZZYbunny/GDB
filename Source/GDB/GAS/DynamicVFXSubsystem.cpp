// DynamicVFXSubsystem.cpp
#include "DynamicVFXSubsystem.h"
#include "DynamicGameState.h"
#include "AbilitySystemComponent.h"
#include "DynamicSocialComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

void UDynamicVFXSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicVFXSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UDynamicVFXSubsystem::PlayVFXLayer(
    FGameplayTag EventTag,
    const FDynamicTagToVFXLayerMapWrapper& VfxEventMap,
    const FVector& Location, 
    const FVector& Normal, 
    float Magnitude, 
    AActor* InstigatorActor,
    AActor* HitTargetActor)
{
    UpdateChanneledVFXLayer(FGuid(), EventTag, VfxEventMap, Location, Location, Normal, Magnitude, InstigatorActor, HitTargetActor);
}

void UDynamicVFXSubsystem::UpdateChanneledVFXLayer(
    FGuid ChannelID,
    FGameplayTag EventTag,
    const FDynamicTagToVFXLayerMapWrapper& VfxEventMap,
    const FVector& StartLocation,
    const FVector& EndLocation,
    const FVector& Normal,
    float Magnitude,
    AActor* InstigatorActor,
    AActor* HitTargetActor)
{
    if (!EventTag.IsValid()) return;

    if (const FDynamicVFXLayer* Layer = VfxEventMap.LocalLookupMap.Find(EventTag))
    {
        for (const FGameplayTag& VfxTag : Layer->VfxTags)
        {
            PlayDynamicVFX(VfxTag, ChannelID, StartLocation, EndLocation, Normal, Magnitude, InstigatorActor, HitTargetActor);
        }
    }
}

FLinearColor UDynamicVFXSubsystem::ResolveDynamicColor(const FDynamicVFXDef& VfxDef, AActor* InstigatorActor, AActor* TargetActor) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return VfxDef.PrimaryColor;

    if (VfxDef.bUseAttributeColor && VfxDef.ColorAttributeTag.IsValid())
    {
        if (const FDynamicAttributeDef* AttrDef = GS->AttributeMasterRegistry.Find(VfxDef.ColorAttributeTag))
        {
            return AttrDef->BaseColorTint;
        }
    }

    if (VfxDef.bUseRelationshipColor && InstigatorActor && TargetActor)
    {
        if (UDynamicSocialComponent* SourceSocial = InstigatorActor->FindComponentByClass<UDynamicSocialComponent>())
        {
            if (UDynamicSocialComponent* TargetSocial = TargetActor->FindComponentByClass<UDynamicSocialComponent>())
            {
                static const FGameplayTag AffinityTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Affinity"), false);
                const float Affinity = SourceSocial->GetDecayedRelationshipMetric(TargetSocial->CharacterPersistentGuid, AffinityTag);

                if (Affinity > 25.0f) return FLinearColor(0.1f, 0.8f, 0.2f, 1.0f); // Ally Green
                if (Affinity < -25.0f) return FLinearColor(0.9f, 0.1f, 0.1f, 1.0f); // Enemy Red
            }
        }
    }

    return VfxDef.PrimaryColor;
}

void UDynamicVFXSubsystem::PlayDynamicVFX(
    FGameplayTag VfxTag, 
    FGuid ChannelID,
    const FVector& StartLocation, 
    const FVector& EndLocation,
    const FVector& Normal, 
    float Magnitude, 
    AActor* InstigatorActor,
    AActor* HitTargetActor)
{
    if (!VfxTag.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicVFXDef* VfxDef = GS->VfxMasterRegistry.Find(VfxTag);
    if (!VfxDef) return;

    const FLinearColor ResolvedColor = ResolveDynamicColor(*VfxDef, InstigatorActor, HitTargetActor);
    const float ScaledRadius = VfxDef->BaseRadius + (Magnitude * VfxDef->MagnitudeToScaleRatio);

    // Reactivity is only evaluated on discrete impacts (ChannelID is empty) or first frames to prevent tick spam
    if (!ChannelID.IsValid() && InstigatorActor && InstigatorActor->HasAuthority())
    {
        if (VfxDef->LawAndOrderActionTag.IsValid())
        {
            if (UDynamicSocialComponent* SocialComp = InstigatorActor->FindComponentByClass<UDynamicSocialComponent>())
            {
                SocialComp->ServerEvaluateAndBroadcastAction(VfxDef->LawAndOrderActionTag, InstigatorActor, HitTargetActor, FGuid(), TArray<FGuid>());
            }
        }

        if (VfxDef->ReactivityEventTag.IsValid())
        {
            auto ApplyEventToActor = [&](AActor* Target)
            {
                if (!Target || Target == InstigatorActor) return;
                if (UAbilitySystemComponent* TargetASC = Target->FindComponentByClass<UAbilitySystemComponent>())
                {
                    FGameplayEventData Payload;
                    Payload.EventTag = VfxDef->ReactivityEventTag;
                    Payload.Instigator = InstigatorActor;
                    Payload.Target = Target;
                    Payload.EventMagnitude = Magnitude;
                    TargetASC->HandleGameplayEvent(VfxDef->ReactivityEventTag, &Payload);
                }
            };

            if (VfxDef->ReactivityRadius > 0.0f && World)
            {
                TArray<FOverlapResult> Overlaps;
                FCollisionShape Sphere = FCollisionShape::MakeSphere(VfxDef->ReactivityRadius * ScaledRadius);
                FCollisionQueryParams Params(SCENE_QUERY_STAT(VfxReactivitySweep), false, InstigatorActor);

                // Use the EndLocation for AoE blasts at the end of a beam
                if (World->OverlapMultiByChannel(Overlaps, EndLocation, FQuat::Identity, ECC_Pawn, Sphere, Params))
                {
                    for (const FOverlapResult& Overlap : Overlaps) ApplyEventToActor(Overlap.GetActor());
                }
            }
            else if (HitTargetActor)
            {
                ApplyEventToActor(HitTargetActor);
            }
        }
    }

    if (World->GetNetMode() != NM_DedicatedServer)
    {
        UNiagaraSystem* LoadedFallback = VfxDef->FallbackSystem.LoadSynchronous();
        const float VfxDuration = (VfxDef->DurationPolicy == EEffectDurationPolicy::Instant) ? 0.0f : 5.0f;
        
        PushToNiagaraDataChannel(
            VfxDef->DataChannelName,
            VfxDef->PayloadTypeID,
            ChannelID,
            StartLocation,
            EndLocation,
            Normal,
            ScaledRadius,
            VfxDuration,
            ResolvedColor,
            LoadedFallback
        );
    }
}

void UDynamicVFXSubsystem::PushToNiagaraDataChannel_Implementation(
    FName ChannelName, int32 PayloadTypeID, FGuid ChannelID, const FVector& StartLocation, const FVector& EndLocation, const FVector& Normal, float ScaledRadius, float Duration, FLinearColor Color, UNiagaraSystem* FallbackSystem)
{
    if (ChannelName.IsNone() || !FallbackSystem)
    {
        if (FallbackSystem && !ChannelID.IsValid()) // Only spawn fallback if it's a discrete impact, avoid spawning 60 times a second for channels
        {
            UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FallbackSystem, EndLocation, Normal.Rotation(), FVector(ScaledRadius / 100.0f));
            if (NC) NC->SetColorParameter(FName("PrimaryColor"), Color);
        }
        return;
    }
}