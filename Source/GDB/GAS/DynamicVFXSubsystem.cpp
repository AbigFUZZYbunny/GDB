// DynamicVFXSubsystem.cpp
#include "DynamicVFXSubsystem.h"
#include "DynamicGameState.h"
#include "AbilitySystemComponent.h"
#include "DynamicSocialComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "NiagaraFunctionLibrary.h"

void UDynamicVFXSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); }
void UDynamicVFXSubsystem::Deinitialize() { Super::Deinitialize(); }

void UDynamicVFXSubsystem::PlayVFXLayer(
    FGameplayTag EventTag,
    const FDynamicTagToVFXLayerMapWrapper& VfxEventMap,
    const FVector& Location, 
    const FVector& Normal, 
    float Magnitude, 
    AActor* InstigatorActor,
    AActor* HitTargetActor)
{
    if (!EventTag.IsValid()) return;

    if (const FDynamicVFXLayer* Layer = VfxEventMap.EventMap.Find(EventTag))
    {
        for (const FGameplayTag& VfxTag : Layer->VfxTags)
        {
            PlayDynamicVFX(VfxTag, Location, Normal, Magnitude, InstigatorActor, HitTargetActor);
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
    const FVector& Location, 
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

    // 1. GAMEPLAY & LAW REACTIVITY (Server Authority Only)
    if (InstigatorActor && InstigatorActor->HasAuthority())
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

                if (World->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_Pawn, Sphere, Params))
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

    // 2. VISUAL PRESENTATION (Client / Multicast)
    if (World->GetNetMode() != NM_DedicatedServer)
    {
        UNiagaraSystem* LoadedFallback = VfxDef->FallbackSystem.LoadSynchronous();
        float VfxDuration = (VfxDef->DurationPolicy == EEffectDurationPolicy::Instant) ? 0.0f : 5.0f; // Could pull from active effect duration if needed
        
        PushToNiagaraDataChannel(
            VfxDef->DataChannelName,
            VfxDef->PayloadTypeID,
            Location,
            Normal,
            ScaledRadius,
            VfxDuration,
            ResolvedColor,
            LoadedFallback
        );
    }
}

void UDynamicVFXSubsystem::PushToNiagaraDataChannel_Implementation(
    FName ChannelName, int32 PayloadTypeID, const FVector& Location, const FVector& Normal, float ScaledRadius, float Duration, FLinearColor Color, UNiagaraSystem* FallbackSystem)
{
    if (ChannelName.IsNone() || !FallbackSystem)
    {
        if (FallbackSystem)
        {
            UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FallbackSystem, Location, Normal.Rotation(), FVector(ScaledRadius / 100.0f));
            if (NC)
            {
                NC->SetColorParameter(FName("PrimaryColor"), Color);
            }
        }
        return;
    }
    // Blueprint Native Event implemented in editor using UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel
}