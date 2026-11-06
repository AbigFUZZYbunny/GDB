#include "DynamicAudioSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicSocialComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

void UDynamicAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicAudioSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UDynamicAudioSubsystem::PlayDynamicAudioAtLocation(
    FGameplayTag AudioTag, 
    const FVector& Location, 
    AActor* Instigator, 
    float ImpactVelocity,
    float VolumeMultiplier,
    float PitchMultiplier)
{
    if (!AudioTag.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicAudioDef* AudioDef = GS->AudioMasterRegistry.Find(AudioTag);
    if (!AudioDef || AudioDef->SoundAsset.IsNull()) return;

    float FinalVolume = AudioDef->DefaultVolumeMultiplier * VolumeMultiplier;
    float FinalPitch = AudioDef->DefaultPitchMultiplier * PitchMultiplier;

    if (ImpactVelocity > 0.0f && AudioDef->VelocityToVolumeScalar > 0.0f)
    {
        FinalVolume *= FMath::Clamp(ImpactVelocity * AudioDef->VelocityToVolumeScalar, 0.1f, 3.0f);
    }

    if (World->GetNetMode() != NM_DedicatedServer)
    {
        if (USoundBase* LoadedSound = AudioDef->SoundAsset.LoadSynchronous())
        {
            UGameplayStatics::PlaySoundAtLocation(World, LoadedSound, Location, FinalVolume, FinalPitch);
        }
    }

    if (Instigator && Instigator->HasAuthority() && AudioDef->AssociatedStimulusTag.IsValid())
    {
        BroadcastAcousticStimulusToAI(*AudioDef, Location, Instigator, FinalVolume);
    }
}

void UDynamicAudioSubsystem::PlayMaterialImpactAudio(
    FGameplayTag MaterialA, 
    FGameplayTag MaterialB, 
    const FVector& Location, 
    AActor* Instigator, 
    AActor* HitTargetActor, 
    float ImpactVelocity)
{
    if (!MaterialA.IsValid() || !MaterialB.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FAcousticInteractionProfile* FoundProfile = nullptr;

    if (const FAcousticMaterialMatrixRow* Row = GS->AudioMaterialMatrix.Find(MaterialA))
    {
        FoundProfile = Row->TargetMaterialProfiles.Find(MaterialB);
    }

    if (!FoundProfile)
    {
        if (const FAcousticMaterialMatrixRow* Row = GS->AudioMaterialMatrix.Find(MaterialB))
        {
            FoundProfile = Row->TargetMaterialProfiles.Find(MaterialA);
        }
    }

    if (FoundProfile)
    {
        PlayAcousticProfile(*FoundProfile, Location, Instigator, HitTargetActor, ImpactVelocity);
    }
}

void UDynamicAudioSubsystem::PlayMaterialImpactAudio(
    FGameplayTag MaterialA, 
    FGameplayTag MaterialB, 
    const FVector& Location, 
    AActor* Instigator, 
    float ImpactVelocity)
{
    PlayMaterialImpactAudio(MaterialA, MaterialB, Location, Instigator, nullptr, ImpactVelocity);
}

void UDynamicAudioSubsystem::PlayAcousticProfile(
    const FAcousticInteractionProfile& Profile, 
    const FVector& Location, 
    AActor* Instigator, 
    AActor* HitTargetActor, 
    float ImpactVelocity)
{
    if (Profile.PrimaryAudioTag.IsValid())
    {
        PlayDynamicAudioAtLocation(Profile.PrimaryAudioTag, Location, Instigator, ImpactVelocity);
    }

    for (const FGameplayTag& SubLayerTag : Profile.GuaranteedSecondaryAudioTags)
    {
        PlayDynamicAudioAtLocation(SubLayerTag, Location, Instigator, ImpactVelocity);
    }

    for (const FAcousticConditionalLayer& CondLayer : Profile.ConditionalAudioLayers)
    {
        if (!CondLayer.AudioTag.IsValid()) continue;

        float EvaluatedProbability = CondLayer.BaseProbability;

        if (CondLayer.ProbabilityScalingAttribute.IsValid())
        {
            float TargetAttrVal = 0.0f;
            if (HitTargetActor)
            {
                if (UAbilitySystemComponent* TargetASC = HitTargetActor->FindComponentByClass<UAbilitySystemComponent>())
                {
                    if (const UDynamicAttributeSet* AttrSet = Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())))
                    {
                        TargetAttrVal = AttrSet->GetDynamicAttributeValue(CondLayer.ProbabilityScalingAttribute, 0);
                    }
                }
            }

            if (CondLayer.bInvertAttributeScaling)
            {
                EvaluatedProbability += FMath::Max(0.0f, (100.0f - TargetAttrVal) * CondLayer.AttributeScalingWeight * 0.01f);
            }
            else
            {
                EvaluatedProbability += (TargetAttrVal * CondLayer.AttributeScalingWeight * 0.01f);
            }
        }

        if (!CondLayer.ChanceConditionTreeName.IsNone() && HitTargetActor)
        {
            if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(HitTargetActor, CondLayer.ChanceConditionTreeName))
            {
                UAbilitySystemComponent* InstigatorASC = Instigator ? Instigator->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                UAbilitySystemComponent* TargetASC = HitTargetActor->FindComponentByClass<UAbilitySystemComponent>();
                FGameplayTagContainer SourceTags, TargetTags;
                if (InstigatorASC) InstigatorASC->GetOwnedGameplayTags(SourceTags);
                if (TargetASC) TargetASC->GetOwnedGameplayTags(TargetTags);

                if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, InstigatorASC, TargetASC, SourceTags, TargetTags))
                {
                    continue;
                }
            }
        }

        if (FMath::FRand() <= FMath::Clamp(EvaluatedProbability, 0.0f, 1.0f))
        {
            PlayDynamicAudioAtLocation(CondLayer.AudioTag, Location, Instigator, ImpactVelocity);
        }
    }
}

void UDynamicAudioSubsystem::PlayFootstepAcousticLayers(
    const AActor* SteppingCharacter, 
    const FHitResult& FloorHit, 
    uint8 FootLimbId)
{
    if (!SteppingCharacter || !FloorHit.bBlockingHit) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    // 1. Resolve Character Movement Metrics (Pace, Velocity, and Stance)
    float MovementSpeed = SteppingCharacter->GetVelocity().Size();
    float PaceScale = 1.0f;
    float StanceScale = 1.0f;

    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(SteppingCharacter))
    {
        PaceScale = CharOwner->TargetPaceScale;
        StanceScale = CharOwner->TargetStanceScale;

        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            static const FGameplayTag PaceTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Movement.Pace"), false);
            if (PaceTag.IsValid())
            {
                const float AttrPace = AttrSet->GetDynamicAttributeValue(PaceTag, 0);
                if (AttrPace > 0.0f) PaceScale = AttrPace;
            }
        }
    }

    // 2. Resolve Footwear & Equipment Action Audio Layers
    FGameplayTag FootwearMaterial = FGameplayTag::RequestGameplayTag(TEXT("AcousticMaterial.Leather"), false);

    // Calculate layer-based acoustic dampening (e.g. Tabard / Cloak dampening chainmail)
    float TotalAcousticDamping = 1.0f;

    if (const UDynamicEquipmentComponent* EquipComp = SteppingCharacter->FindComponentByClass<UDynamicEquipmentComponent>())
    {
        for (const FDynamicEquipmentSlotProfile& SlotProf : EquipComp->ConfiguredSlots)
        {
            if (EquipComp->GetEquippedItemInSlot(SlotProf.SlotTag).IsValid())
            {
                TotalAcousticDamping *= SlotProf.AcousticDampingMultiplier;
            }
        }

        static const FGameplayTag StepActionTag = FGameplayTag::RequestGameplayTag(TEXT("Action.Movement.Step"), false);

        for (const FDynamicEquipmentSlotItem& SlotItem : EquipComp->ReplicatedEquipmentSlots.Items)
        {
            if (SlotItem.EquippedItemArchetypeTag.IsValid())
            {
                if (const FDynamicPhysicsActorDef* ArmorDef = GS->MasterPhysicsActorRegistry.Find(SlotItem.EquippedItemArchetypeTag))
                {
                    if (SlotItem.LinkedLimbId == FootLimbId && ArmorDef->AcousticMaterialTag.IsValid())
                    {
                        FootwearMaterial = ArmorDef->AcousticMaterialTag;
                    }

                    for (const FAcousticActionLayer& ActionLayer : ArmorDef->ActionAudioLayers)
                    {
                        if (ActionLayer.ActionOrMovementTag == StepActionTag)
                        {
                            if (ActionLayer.BaseProbability < 1.0f && FMath::FRand() > ActionLayer.BaseProbability)
                            {
                                continue;
                            }

                            const FAcousticPaceVolumeModifier& Mod = ActionLayer.PaceModifier;
                            const float SpeedAlpha = FMath::Clamp((MovementSpeed - Mod.MinSpeedThreshold) / FMath::Max(1.0f, Mod.MaxSpeedThreshold - Mod.MinSpeedThreshold), 0.0f, 1.0f);
                            
                            const float CalcVolume = FMath::Lerp(Mod.VolumeMultiplierAtMin, Mod.VolumeMultiplierAtMax, SpeedAlpha) * PaceScale * TotalAcousticDamping;
                            const float CalcPitch = FMath::Lerp(Mod.PitchMultiplierAtMin, Mod.PitchMultiplierAtMax, SpeedAlpha);

                            if (ActionLayer.PrimaryAudioTag.IsValid())
                            {
                                PlayDynamicAudioAtLocation(ActionLayer.PrimaryAudioTag, SteppingCharacter->GetActorLocation(), const_cast<AActor*>(SteppingCharacter), MovementSpeed, CalcVolume, CalcPitch);
                            }

                            for (const FGameplayTag& SecTag : ActionLayer.GuaranteedSecondaryAudioTags)
                            {
                                PlayDynamicAudioAtLocation(SecTag, SteppingCharacter->GetActorLocation(), const_cast<AActor*>(SteppingCharacter), MovementSpeed, CalcVolume, CalcPitch);
                            }

                            for (const FAcousticConditionalLayer& CondLayer : ActionLayer.ConditionalAudioLayers)
                            {
                                if (FMath::FRand() <= CondLayer.BaseProbability)
                                {
                                    PlayDynamicAudioAtLocation(CondLayer.AudioTag, SteppingCharacter->GetActorLocation(), const_cast<AActor*>(SteppingCharacter), MovementSpeed, CalcVolume, CalcPitch);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Resolve Floor Surface Material and Play Layered Impact
    FGameplayTag FloorMaterial = FGameplayTag::RequestGameplayTag(TEXT("AcousticMaterial.Stone"), false);
    AActor* FloorActor = FloorHit.GetActor();
    if (FloorActor)
    {
        FloorMaterial = ResolveActorAcousticMaterial(FloorActor);
    }

    // Footwear on Floor impact
    PlayMaterialImpactAudio(FootwearMaterial, FloorMaterial, FloorHit.ImpactPoint, const_cast<AActor*>(SteppingCharacter), FloorActor, MovementSpeed * PaceScale);
}

void UDynamicAudioSubsystem::PlayActionAcousticLayers(
    const AActor* SourceActor, 
    FGameplayTag ActionTag, 
    const FVector& Location, 
    float MovementVelocity)
{
    if (!SourceActor || !ActionTag.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    if (const UDynamicEquipmentComponent* EquipComp = SourceActor->FindComponentByClass<UDynamicEquipmentComponent>())
    {
        for (const FDynamicEquipmentSlotItem& SlotItem : EquipComp->ReplicatedEquipmentSlots.Items)
        {
            if (SlotItem.EquippedItemArchetypeTag.IsValid())
            {
                if (const FDynamicPhysicsActorDef* ItemDef = GS->MasterPhysicsActorRegistry.Find(SlotItem.EquippedItemArchetypeTag))
                {
                    for (const FAcousticActionLayer& Layer : ItemDef->ActionAudioLayers)
                    {
                        if (Layer.ActionOrMovementTag == ActionTag)
                        {
                            const FAcousticPaceVolumeModifier& Mod = Layer.PaceModifier;
                            const float SpeedAlpha = FMath::Clamp((MovementVelocity - Mod.MinSpeedThreshold) / FMath::Max(1.0f, Mod.MaxSpeedThreshold - Mod.MinSpeedThreshold), 0.0f, 1.0f);
                            const float CalcVolume = FMath::Lerp(Mod.VolumeMultiplierAtMin, Mod.VolumeMultiplierAtMax, SpeedAlpha);
                            const float CalcPitch = FMath::Lerp(Mod.PitchMultiplierAtMin, Mod.PitchMultiplierAtMax, SpeedAlpha);

                            if (Layer.PrimaryAudioTag.IsValid())
                            {
                                PlayDynamicAudioAtLocation(Layer.PrimaryAudioTag, Location, const_cast<AActor*>(SourceActor), MovementVelocity, CalcVolume, CalcPitch);
                            }
                        }
                    }
                }
            }
        }
    }
}

FGameplayTag UDynamicAudioSubsystem::ResolveActorAcousticMaterial(const AActor* InActor) const
{
    if (!InActor) return FGameplayTag::EmptyTag;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayTag::EmptyTag;

    if (const ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(InActor))
    {
        if (const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag()))
        {
            return Def->AcousticMaterialTag;
        }
    }
    else if (const ADynamicCharacter* Char = Cast<ADynamicCharacter>(InActor))
    {
        if (const UDynamicCustomizationComponent* Cust = Char->GetCustomizationComponent())
        {
            FCharacterTemplateDef Def;
            if (GS->GetCharacterTemplate(Cust->ActiveCharacterTemplateTag, Def))
            {
                return Def.AcousticMaterialTag;
            }
        }
    }

    return FGameplayTag::EmptyTag;
}

void UDynamicAudioSubsystem::BroadcastAcousticStimulusToAI(
    const FDynamicAudioDef& AudioDef, 
    const FVector& Location, 
    AActor* Instigator,
    float EffectiveVolume)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const float AdjustedMagnitude = AudioDef.BaseStimulusMagnitude * FMath::Clamp(EffectiveVolume, 0.1f, 3.0f);
    TMap<FGameplayTag, float> Signatures;
    Signatures.Add(AudioDef.AssociatedStimulusTag, AdjustedMagnitude);

    const float QueryRadius = FMath::Clamp(AdjustedMagnitude * 4.5f, 500.0f, 15000.0f);
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(QueryRadius);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AudioSubsystemAcousticSweep), false, Instigator);

    if (World->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_Pawn, Sphere, Params))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            if (ADynamicCharacter* Listener = Cast<ADynamicCharacter>(Overlap.GetActor()))
            {
                if (Listener != Instigator)
                {
                    if (UDynamicSocialComponent* Social = Listener->FindComponentByClass<UDynamicSocialComponent>())
                    {
                        Social->ServerProcessCrimeSensoryEvent(
                            FGameplayTag::EmptyTag, 
                            Instigator, 
                            nullptr, 
                            FGuid(), 
                            TArray<FGuid>(), 
                            Signatures
                        );
                    }
                }
            }
        }
    }
}