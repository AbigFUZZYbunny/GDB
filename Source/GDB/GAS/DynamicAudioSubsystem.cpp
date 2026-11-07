// ============================================================================
// DynamicAudioSubsystem.cpp
// ============================================================================
#include "DynamicAudioSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicBuildingSubsystem.h"
#include "DynamicInteractableComponent.h"
#include "Engine/OverlapResult.h"
#include "DynamicAttributeSet.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Sound/ReverbEffect.h"

void UDynamicAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    AmbientSampleAccumulator = 0.0f;
}

void UDynamicAudioSubsystem::Deinitialize()
{
    if (ContinuousAmbientAudioComponent)
    {
        ContinuousAmbientAudioComponent->Stop();
        ContinuousAmbientAudioComponent = nullptr;
    }

    for (UAudioComponent* Comp : ActiveAudioComponents)
    {
        if (IsValid(Comp))
        {
            Comp->Stop();
        }
    }

    AudioComponentPool.Empty();
    ActiveAudioComponents.Empty();
    Super::Deinitialize();
}

bool UDynamicAudioSubsystem::IsTickable() const
{
    const UWorld* World = GetWorld();
    return World && World->GetNetMode() != NM_DedicatedServer;
}

void UDynamicAudioSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateContinuousEnvironmentAudio(DeltaTime);
    UpdateContinuousScrapes(DeltaTime); // Evaluate active sliding/dragging
}

UAudioComponent* UDynamicAudioSubsystem::AcquirePooledAudioComponent(const FVector& Location)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    const int32 MaxPoolSize = GS ? GS->GameSettingsConfig.AudioVirtualizationConfig.MaxActiveProceduralComponents : 32;

    while (AudioComponentPool.Num() > 0)
    {
        UAudioComponent* Candidate = AudioComponentPool.Pop(EAllowShrinking::No);
        if (IsValid(Candidate))
        {
            Candidate->SetWorldLocation(Location);
            ActiveAudioComponents.Add(Candidate);
            return Candidate;
        }
    }

    if (ActiveAudioComponents.Num() >= MaxPoolSize)
    {
        UAudioComponent* Oldest = ActiveAudioComponents[0];
        ActiveAudioComponents.RemoveAt(0, 1, EAllowShrinking::No);
        if (IsValid(Oldest))
        {
            Oldest->Stop();
            Oldest->SetWorldLocation(Location);
            ActiveAudioComponents.Add(Oldest);
            return Oldest;
        }
    }

    UAudioComponent* NewComp = NewObject<UAudioComponent>(this);
    NewComp->RegisterComponentWithWorld(World);
    NewComp->SetWorldLocation(Location);
    NewComp->bAutoActivate = false;
    NewComp->bAllowSpatialization = true;
    NewComp->bOverrideAttenuation = true;
    NewComp->bEnableLowPassFilter = true;

    FSoundAttenuationSettings AttenSettings;
    AttenSettings.bAttenuate = true;
    AttenSettings.bSpatialize = true;
    AttenSettings.bAttenuateWithLPF = true;
    AttenSettings.FalloffDistance = GS ? GS->GameSettingsConfig.AudioVirtualizationConfig.MaxAudibleDistance : 12000.0f;
    NewComp->AttenuationOverrides = AttenSettings;
    NewComp->AdjustAttenuation(AttenSettings);

    NewComp->OnAudioFinishedNative.AddUObject(this, &UDynamicAudioSubsystem::OnAudioComponentPlaybackFinished);
    ActiveAudioComponents.Add(NewComp);
    return NewComp;
}

void UDynamicAudioSubsystem::OnAudioComponentPlaybackFinished(UAudioComponent* FinishedComponent)
{
    if (!IsValid(FinishedComponent)) return;

    ActiveAudioComponents.RemoveSwap(FinishedComponent);
    FinishedComponent->SetSound(nullptr);
    AudioComponentPool.Add(FinishedComponent);
}

void UDynamicAudioSubsystem::CalculateAcousticPathProperties(
    const FVector& SourceLocation, 
    const FVector& ListenerLocation, 
    const AActor* ListenerActor,
    float& OutAttenuationRatio, 
    float& OutLowPassCutoffFreq, 
    float& OutReverbSendLevel,
    FVector& OutApparentLocation) const
{
    OutAttenuationRatio = 1.0f;
    OutLowPassCutoffFreq = 20000.0f; // Full spectrum
    OutReverbSendLevel = 0.0f;
    OutApparentLocation = SourceLocation; 

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return;

    const float DirectDistance = FVector::Dist(SourceLocation, ListenerLocation);
    const float MaxAudibleDist = GS->GameSettingsConfig.AudioVirtualizationConfig.MaxAudibleDistance;

    // 1. DIRECT LINE-OF-SIGHT CHECK (O(1) Raycast)
    FHitResult Hit;
    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AudioRaymarchOcclusion), false, ListenerActor);
    const bool bIsOccluded = World->LineTraceSingleByChannel(Hit, SourceLocation, ListenerLocation, ECC_Visibility, TraceParams);

    bool bDirectPathPassable = !bIsOccluded;

    if (bIsOccluded)
    {
        // Evaluate if the direct hit is actually an acoustic portal (Open door, damaged wall, or ghost wall)
        if (ADynamicBuildingChunkActor* Chunk = Cast<ADynamicBuildingChunkActor>(Hit.GetActor()))
        {
            if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Hit.GetComponent()))
            {
                if (const TMap<int32, FGuid>* InstMap = Chunk->InstanceHitMap.Find(HISM))
                {
                    if (const FGuid* TargetGuid = InstMap->Find(Hit.Item))
                    {
                        const FStructureInstanceItem* StructItem = Chunk->ReplicatedStructures.Items.FindByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == *TargetGuid; });
                        if (StructItem)
                        {
                            const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(StructItem->MaterialTag);
                            const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(StructItem->PieceTag);
                            const float MaxHP = (PieceDef ? PieceDef->BaseMaxHP : 100.0f) * (MatDef ? MatDef->DurabilityMultiplier : 1.0f);
                            
                            // Partially built (Ghost) or heavily damaged walls (< 50% HP) have gaps that allow sound through
                            if (StructItem->bIsGhost || StructItem->CurrentHP <= (MaxHP * 0.5f))
                            {
                                bDirectPathPassable = true;
                            }
                        }
                    }
                }
            }
        }
        else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Hit.GetActor()))
        {
            const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag());
            if (Def && Def->bIsAcousticPortal)
            {
                if (UDynamicInteractableComponent* Interactable = PhysActor->GetInteractableComponent())
                {
                    static const FGameplayTag OpenTag = FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.Open"), false);
                    static const FGameplayTag BrokenTag = FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.BrokenHinge"), false);
                    if (Interactable->HasStateTag(OpenTag) || Interactable->HasStateTag(BrokenTag))
                    {
                        bDirectPathPassable = true;
                    }
                }
            }
        }
    }

    if (bDirectPathPassable)
    {
        // Clear path (or passing straight through a gaping hole): Apply standard distance air-absorption LPF.
        if (DirectDistance > 2500.0f)
        {
            const float DistanceAlpha = FMath::Clamp((DirectDistance - 2500.0f) / FMath::Max(1.0f, MaxAudibleDist - 2500.0f), 0.0f, 1.0f);
            OutLowPassCutoffFreq = FMath::Lerp(20000.0f, 2200.0f, DistanceAlpha);
        }
        
        // Apply a minor muffling if it went through a damaged wall/portal rather than completely open air
        if (bIsOccluded)
        {
            OutLowPassCutoffFreq *= 0.85f;
        }
        return;
    }

    // ========================================================================
    // 2. OCCLUDED PATH: Evaluate Structural Transmission (Through Walls)
    // ========================================================================
    float DirectInsulation = 0.0f;
    if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
    {
        const float SourceIns = BuildSub->EvaluateShelterInsulationAtLocation(SourceLocation);
        const float ListenerIns = BuildSub->EvaluateShelterInsulationAtLocation(ListenerLocation);
        DirectInsulation = FMath::Max(SourceIns, ListenerIns);
    }

    // Default Transmission: Sound traveling straight through a wall/floor. 
    // High insulation heavily drops the LPF to ~250Hz, leaving only a dull "thud".
    float BestAttenuation = FMath::Clamp(1.0f - (DirectInsulation * 0.06f), 0.1f, 1.0f);
    float BestLpf = FMath::Clamp(16000.0f - (DirectInsulation * 1200.0f), 250.0f, 16000.0f);
    FVector BestApparentLocation = SourceLocation;
    bool bDiffractedPathFound = false;

    // ========================================================================
    // 3. ACOUSTIC DIFFRACTION: 1-Bounce Portal Raycasting (Around Walls)
    // ========================================================================
    // Sweep a sphere around the source to find nearby portals (doors, windows, damaged walls)
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(1500.0f); 
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
    ObjParams.AddObjectTypesToQuery(ECC_WorldStatic); // Include building chunk HISMs

    if (World->OverlapMultiByObjectType(Overlaps, SourceLocation, FQuat::Identity, ObjParams, Sphere))
    {
        static const FGameplayTag OpenTag = FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.Open"), false);
        static const FGameplayTag BrokenHingeTag = FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.BrokenHinge"), false);
        
        for (const FOverlapResult& Overlap : Overlaps)
        {
            bool bIsAcousticPortal = false;
            FVector PortalLoc = FVector::ZeroVector;

            if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
            {
                const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag());
                if (Def && Def->bIsAcousticPortal)
                {
                    if (UDynamicInteractableComponent* Interactable = PhysActor->GetInteractableComponent())
                    {
                        if (Interactable->HasStateTag(OpenTag) || Interactable->HasStateTag(BrokenHingeTag))
                        {
                            bIsAcousticPortal = true;
                            PortalLoc = PhysActor->GetActorLocation();
                        }
                    }
                }
            }
            else if (ADynamicBuildingChunkActor* Chunk = Cast<ADynamicBuildingChunkActor>(Overlap.GetActor()))
            {
                if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Overlap.GetComponent()))
                {
                    if (const TMap<int32, FGuid>* InstMap = Chunk->InstanceHitMap.Find(HISM))
                    {
                        if (const FGuid* TargetGuid = InstMap->Find(Overlap.ItemIndex))
                        {
                            const FStructureInstanceItem* StructItem = Chunk->ReplicatedStructures.Items.FindByPredicate([&](const FStructureInstanceItem& I) { return I.InstanceID == *TargetGuid; });
                            if (StructItem)
                            {
                                const FDynamicMaterialDef* MatDef = GS->MaterialMasterRegistry.Find(StructItem->MaterialTag);
                                const FDynamicStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(StructItem->PieceTag);
                                const float MaxHP = (PieceDef ? PieceDef->BaseMaxHP : 100.0f) * (MatDef ? MatDef->DurabilityMultiplier : 1.0f);
                                
                                if (StructItem->bIsGhost || StructItem->CurrentHP <= (MaxHP * 0.5f))
                                {
                                    bIsAcousticPortal = true;
                                    PortalLoc = StructItem->WorldTransform.GetLocation();
                                }
                            }
                        }
                    }
                }
            }

            if (bIsAcousticPortal)
            {
                // Raycast 1: Source to Portal
                FHitResult PortalHit;
                if (!World->LineTraceSingleByChannel(PortalHit, SourceLocation, PortalLoc, ECC_Visibility, TraceParams))
                {
                    // Raycast 2: Portal to Listener
                    if (!World->LineTraceSingleByChannel(PortalHit, PortalLoc, ListenerLocation, ECC_Visibility, TraceParams))
                    {
                        const float PathDistance = FVector::Dist(SourceLocation, PortalLoc) + FVector::Dist(PortalLoc, ListenerLocation);
                        
                        if (PathDistance <= MaxAudibleDist)
                        {
                            // Attenuate based on the bent path distance
                            const float PathAttenuation = FMath::Clamp(1.0f - (PathDistance / FMath::Max(1.0f, MaxAudibleDist)), 0.01f, 1.0f);
                            
                            // Diffraction applies a mild LPF (bending sound loses some high freq), but much clearer than transmission
                            const float DiffractedLpf = FMath::Clamp(12000.0f - (PathDistance * 0.5f), 2000.0f, 18000.0f);

                            // Select this path if it's clearer/louder than transmitting directly through the wall
                            if (DiffractedLpf > BestLpf || PathAttenuation > BestAttenuation)
                            {
                                BestAttenuation = PathAttenuation;
                                BestLpf = DiffractedLpf;
                                BestApparentLocation = PortalLoc; // Shifts the spatial audio origin to the gap!
                                bDiffractedPathFound = true;
                            }
                        }
                    }
                }
            }
        }
    }

    OutAttenuationRatio = BestAttenuation;
    OutLowPassCutoffFreq = BestLpf;
    OutApparentLocation = BestApparentLocation;
    
    // Only apply heavy internal reverb if the sound transmitted through the structure rather than escaping through a portal
    OutReverbSendLevel = bDiffractedPathFound ? 0.0f : FMath::Clamp(DirectInsulation * 0.06f, 0.0f, 0.85f);
}

void UDynamicAudioSubsystem::InjectMaterialParametersToMetaSound(UAudioComponent* AudioComp, const FString& Prefix, const FAcousticMaterialProperties& Props)
{
    if (!AudioComp) return;

    AudioComp->SetIntParameter(FName(*(Prefix + TEXT("HarmonicProfile"))), static_cast<int32>(Props.HarmonicProfile));
    AudioComp->SetFloatParameter(FName(*(Prefix + TEXT("F0"))), Props.FundamentalFrequency);
    AudioComp->SetFloatParameter(FName(*(Prefix + TEXT("QFactor"))), Props.QFactor);
    AudioComp->SetFloatParameter(FName(*(Prefix + TEXT("Decay"))), Props.DecayMultiplier);
    AudioComp->SetFloatParameter(FName(*(Prefix + TEXT("HFDamp"))), Props.HighFreqDamping);
}

void UDynamicAudioSubsystem::PlayProceduralMaterialImpact(
    FGameplayTag MaterialA, 
    FGameplayTag MaterialB, 
    const FVector& Location, 
    AActor* Instigator, 
    float ImpactVelocity,
    float MassRatio)
{
    if (!MaterialA.IsValid() || !MaterialB.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicMaterialDef* MatDefA = GS->MaterialMasterRegistry.Find(MaterialA);
    const FDynamicMaterialDef* MatDefB = GS->MaterialMasterRegistry.Find(MaterialB);
    if (!MatDefA || !MatDefB) return;

    const float MaxAudibleDist = GS->GameSettingsConfig.AudioVirtualizationConfig.MaxAudibleDistance;

    if (World->GetNetMode() != NM_DedicatedServer && !GS->GameSettingsConfig.MasterAcousticMetaSounds.PhysicsImpact.IsNull())
    {
        APlayerController* PC = World->GetFirstPlayerController();
        FVector ListenerPos = FVector::ZeroVector;
        FRotator ListenerRot = FRotator::ZeroRotator;
        if (PC) PC->GetPlayerViewPoint(ListenerPos, ListenerRot);

        const float DistSq = FVector::DistSquared(Location, ListenerPos);
        if (DistSq <= FMath::Square(MaxAudibleDist))
        {
            if (USoundBase* ImpactMetaSound = GS->GameSettingsConfig.MasterAcousticMetaSounds.PhysicsImpact.LoadSynchronous())
            {
                float AttenRatio = 1.0f, LpfCutoff = 20000.0f, ReverbSend = 0.0f;
                FVector ApparentLocation = Location;
                CalculateAcousticPathProperties(Location, ListenerPos, PC ? PC->GetPawn() : nullptr, AttenRatio, LpfCutoff, ReverbSend, ApparentLocation);

                // Re-locate pooled audio component to the diffracted portal location
                UAudioComponent* AudioComp = AcquirePooledAudioComponent(ApparentLocation);
                if (AudioComp)
                {
                    AudioComp->SetSound(ImpactMetaSound);
                    AudioComp->SetLowPassFilterFrequency(LpfCutoff);

                    const float NormalizedForce = FMath::Clamp((ImpactVelocity / 1500.0f) * AttenRatio, 0.05f, 3.0f);
                    AudioComp->SetFloatParameter(FName("ImpactForce"), NormalizedForce);
                    AudioComp->SetFloatParameter(FName("MassRatio"), MassRatio);
                    AudioComp->SetFloatParameter(FName("ReverbSendLevel"), ReverbSend);

                    InjectMaterialParametersToMetaSound(AudioComp, TEXT("MatA_"), MatDefA->AcousticProperties);
                    InjectMaterialParametersToMetaSound(AudioComp, TEXT("MatB_"), MatDefB->AcousticProperties);

                    AudioComp->Play();
                }
            }
        }
    }

    if (Instigator && Instigator->HasAuthority())
    {
        const float AvgDensity = (MatDefA->StructuralDensity + MatDefB->StructuralDensity) * 0.5f;
        const float StimulusMag = FMath::Clamp(ImpactVelocity * AvgDensity, 500.0f, 8000.0f);
        
        static const FGameplayTag DefaultImpactStimTag = FGameplayTag::RequestGameplayTag(TEXT("Stimulus.Sound.Footstep"), false);
        BroadcastAcousticStimulusToAI(DefaultImpactStimTag, StimulusMag, Location, Instigator);
    }
}

void UDynamicAudioSubsystem::PlayEquipmentFrictionAcoustics(const AActor* MovingCharacter, float MovementVelocity, uint8 LimbId)
{
    if (!MovingCharacter || MovementVelocity < 10.0f) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || GS->GameSettingsConfig.MasterAcousticMetaSounds.EquipmentRustle.IsNull()) return;

    if (World->GetNetMode() != NM_DedicatedServer)
    {
        APlayerController* PC = World->GetFirstPlayerController();
        FVector ListenerPos = FVector::ZeroVector;
        FRotator ListenerRot = FRotator::ZeroRotator;
        if (PC) PC->GetPlayerViewPoint(ListenerPos, ListenerRot);

        const float DistSq = FVector::DistSquared(MovingCharacter->GetActorLocation(), ListenerPos);
        if (DistSq > FMath::Square(GS->GameSettingsConfig.AudioVirtualizationConfig.SignificanceCullRadius))
        {
            return;
        }
    }

    const UDynamicEquipmentComponent* EquipComp = MovingCharacter->FindComponentByClass<UDynamicEquipmentComponent>();
    if (!EquipComp) return;

    FGameplayTag OuterMat = FGameplayTag::EmptyTag;
    FGameplayTag MainMat = FGameplayTag::EmptyTag;
    FGameplayTag UnderMat = FGameplayTag::EmptyTag;

    static const FGameplayTag LayerOuterTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Outer"), false);
    static const FGameplayTag LayerMainTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Main"), false);
    static const FGameplayTag LayerUnderTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Under"), false);

    for (const FDynamicEquipmentSlotItem& SlotItem : EquipComp->ReplicatedEquipmentSlots.Items)
    {
        if (SlotItem.LinkedLimbId == LimbId && SlotItem.EquippedItemArchetypeTag.IsValid())
        {
            if (const FDynamicPhysicsActorDef* ArmorDef = GS->MasterPhysicsActorRegistry.Find(SlotItem.EquippedItemArchetypeTag))
            {
                for (const FDynamicEquipmentSlotProfile& SlotProf : EquipComp->ConfiguredSlots)
                {
                    if (SlotProf.SlotTag == SlotItem.SlotTag)
                    {
                        if (SlotProf.LayerTag == LayerOuterTag) OuterMat = ArmorDef->AcousticMaterialTag;
                        else if (SlotProf.LayerTag == LayerMainTag) MainMat = ArmorDef->AcousticMaterialTag;
                        else if (SlotProf.LayerTag == LayerUnderTag) UnderMat = ArmorDef->AcousticMaterialTag;
                        break;
                    }
                }
            }
        }
    }

    auto EvaluateFriction = [&](FGameplayTag MatA, FGameplayTag MatB)
    {
        if (!MatA.IsValid() || !MatB.IsValid()) return;
        
        const FDynamicMaterialDef* DefA = GS->MaterialMasterRegistry.Find(MatA);
        const FDynamicMaterialDef* DefB = GS->MaterialMasterRegistry.Find(MatB);
        if (!DefA || !DefB) return;

        if (World->GetNetMode() != NM_DedicatedServer)
        {
            if (USoundBase* RustleMetaSound = GS->GameSettingsConfig.MasterAcousticMetaSounds.EquipmentRustle.LoadSynchronous())
            {
                UAudioComponent* AudioComp = AcquirePooledAudioComponent(MovingCharacter->GetActorLocation());
                if (AudioComp)
                {
                    AudioComp->SetSound(RustleMetaSound);
                    AudioComp->SetFloatParameter(FName("FrictionVelocity"), MovementVelocity);
                    InjectMaterialParametersToMetaSound(AudioComp, TEXT("MatA_"), DefA->AcousticProperties);
                    InjectMaterialParametersToMetaSound(AudioComp, TEXT("MatB_"), DefB->AcousticProperties);
                    AudioComp->Play();
                }
            }
        }
    };

    EvaluateFriction(OuterMat, MainMat);
    EvaluateFriction(MainMat, UnderMat);
}

void UDynamicAudioSubsystem::PlayProceduralFoliageWind(FGameplayTag FloraMaterialTag, const FVector& Location, float WindSpeed, float FoliageDensity)
{
    if (WindSpeed < 5.0f || !FloraMaterialTag.IsValid()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || GS->GameSettingsConfig.MasterAcousticMetaSounds.FoliageWind.IsNull()) return;

    if (World->GetNetMode() != NM_DedicatedServer)
    {
        if (const FDynamicMaterialDef* FloraDef = GS->MaterialMasterRegistry.Find(FloraMaterialTag))
        {
            if (USoundBase* WindMetaSound = GS->GameSettingsConfig.MasterAcousticMetaSounds.FoliageWind.LoadSynchronous())
            {
                UAudioComponent* AudioComp = AcquirePooledAudioComponent(Location);
                if (AudioComp)
                {
                    AudioComp->SetSound(WindMetaSound);
                    AudioComp->SetFloatParameter(FName("WindSpeed"), WindSpeed);
                    AudioComp->SetFloatParameter(FName("FoliageDensity"), FoliageDensity);
                    AudioComp->SetFloatParameter(FName("StructureStiffness"), FloraDef->SupportStrength);
                    
                    InjectMaterialParametersToMetaSound(AudioComp, TEXT("Flora_"), FloraDef->AcousticProperties);
                    AudioComp->Play();
                }
            }
        }
    }
}

void UDynamicAudioSubsystem::UpdateContinuousEnvironmentAudio(float DeltaSeconds)
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS) return;

    AmbientSampleAccumulator += DeltaSeconds;
    const float SampleInterval = FMath::Max(0.05f, GS->GameSettingsConfig.AudioVirtualizationConfig.EnvironmentalAmbientUpdateRate);
    if (AmbientSampleAccumulator < SampleInterval) return;

    AmbientSampleAccumulator = 0.0f;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return;

    FVector ListenerPos = FVector::ZeroVector;
    FRotator ListenerRot = FRotator::ZeroRotator;
    PC->GetPlayerViewPoint(ListenerPos, ListenerRot);

    if (!ContinuousAmbientAudioComponent && !GS->GameSettingsConfig.MasterAcousticMetaSounds.AmbientEnvironmentLoop.IsNull())
    {
        if (USoundBase* AmbientSound = GS->GameSettingsConfig.MasterAcousticMetaSounds.AmbientEnvironmentLoop.LoadSynchronous())
        {
            ContinuousAmbientAudioComponent = NewObject<UAudioComponent>(this);
            ContinuousAmbientAudioComponent->RegisterComponentWithWorld(World);
            ContinuousAmbientAudioComponent->bAllowSpatialization = false;
            ContinuousAmbientAudioComponent->SetSound(AmbientSound);
            ContinuousAmbientAudioComponent->Play();
        }
    }

    if (ContinuousAmbientAudioComponent && ContinuousAmbientAudioComponent->IsPlaying())
    {
        static const FGameplayTag WindSpeedTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.WindSpeed"), false);
        static const FGameplayTag WetnessTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.Wetness"), false);
        static const FGameplayTag TempTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.Temperature"), false);

        const float WindSpeed = GS->GetEnvironmentMetricAtLocation(ListenerPos, WindSpeedTag);
        const float Wetness = GS->GetEnvironmentMetricAtLocation(ListenerPos, WetnessTag);
        const float Temperature = GS->GetEnvironmentMetricAtLocation(ListenerPos, TempTag);

        ContinuousAmbientAudioComponent->SetFloatParameter(FName("WindSpeed"), WindSpeed);
        ContinuousAmbientAudioComponent->SetFloatParameter(FName("RainIntensity"), FMath::Clamp((Wetness - 40.0f) / 60.0f, 0.0f, 1.0f));
        ContinuousAmbientAudioComponent->SetFloatParameter(FName("AmbientTemperature"), Temperature);

        UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>();
        const float Insulation = BuildSub ? BuildSub->EvaluateShelterInsulationAtLocation(ListenerPos) : 0.0f;
        ContinuousAmbientAudioComponent->SetFloatParameter(FName("IndoorShelterInsulation"), Insulation);

        if (BuildSub)
        {
            UReverbEffect* Reverb = BuildSub->GetAcousticReverbAtLocation(ListenerPos);
            ContinuousAmbientAudioComponent->SetFloatParameter(FName("CaveOrRoomReverbAmount"), Reverb ? 0.75f : 0.0f);
        }
    }
}

void UDynamicAudioSubsystem::PlayLimbGroundAcoustics(
    const AActor* ImpactingCharacter, 
    const FHitResult& FloorHit, 
    const FName& ContactBoneName,
    float ForceMultiplier)
{
    if (!ImpactingCharacter || !FloorHit.bBlockingHit || ContactBoneName.IsNone()) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    float MovementSpeed = ImpactingCharacter->GetVelocity().Size();
    float PaceScale = 1.0f;

    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(ImpactingCharacter))
    {
        PaceScale = CharOwner->TargetPaceScale;

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

    const uint8 ImpactLimbId = GS->GetLimbIdForBone(ContactBoneName);

    // Default to bare flesh if no armor is worn on the impacting limb
    FGameplayTag OutermostMaterial = FGameplayTag::RequestGameplayTag(TEXT("Material.Flesh"), false);
    int32 HighestLayerDepth = 0; // 0 = Flesh, 1 = Under, 2 = Main, 3 = Outer

    static const FGameplayTag LayerOuterTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Outer"), false);
    static const FGameplayTag LayerMainTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Main"), false);
    static const FGameplayTag LayerUnderTag = FGameplayTag::RequestGameplayTag(TEXT("Layer.Under"), false);

    // Identify the outermost acoustic material equipped on the specific limb making contact
    if (const UDynamicEquipmentComponent* EquipComp = ImpactingCharacter->FindComponentByClass<UDynamicEquipmentComponent>())
    {
        for (const FDynamicEquipmentSlotItem& SlotItem : EquipComp->ReplicatedEquipmentSlots.Items)
        {
            if (SlotItem.LinkedLimbId == ImpactLimbId && SlotItem.EquippedItemArchetypeTag.IsValid())
            {
                if (const FDynamicPhysicsActorDef* ArmorDef = GS->MasterPhysicsActorRegistry.Find(SlotItem.EquippedItemArchetypeTag))
                {
                    if (ArmorDef->AcousticMaterialTag.IsValid())
                    {
                        // Resolve the topological layer of this slot
                        for (const FDynamicEquipmentSlotProfile& SlotProf : EquipComp->ConfiguredSlots)
                        {
                            if (SlotProf.SlotTag == SlotItem.SlotTag)
                            {
                                if (SlotProf.LayerTag == LayerOuterTag && HighestLayerDepth < 3) 
                                { 
                                    HighestLayerDepth = 3; 
                                    OutermostMaterial = ArmorDef->AcousticMaterialTag; 
                                }
                                else if (SlotProf.LayerTag == LayerMainTag && HighestLayerDepth < 2) 
                                { 
                                    HighestLayerDepth = 2; 
                                    OutermostMaterial = ArmorDef->AcousticMaterialTag; 
                                }
                                else if (SlotProf.LayerTag == LayerUnderTag && HighestLayerDepth < 1) 
                                { 
                                    HighestLayerDepth = 1; 
                                    OutermostMaterial = ArmorDef->AcousticMaterialTag; 
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    FGameplayTag FloorMaterial = FGameplayTag::RequestGameplayTag(TEXT("Material.Stone.Granite"), false);
    if (AActor* FloorActor = FloorHit.GetActor())
    {
        const FGameplayTag ResolvedFloor = ResolveActorAcousticMaterial(FloorActor);
        if (ResolvedFloor.IsValid()) FloorMaterial = ResolvedFloor;
    }

    // Fallback if stepping/falling in place
    if (MovementSpeed < 10.0f) MovementSpeed = 150.0f; 
    
    // Scale impact force into MetaSound excitation
    const float AcousticImpactForce = (MovementSpeed * PaceScale) * ForceMultiplier;

    // 1. Play core procedural impact (Leather Boot on Stone, Iron Pauldron on Dirt, etc.)
    PlayProceduralMaterialImpact(OutermostMaterial, FloorMaterial, FloorHit.ImpactPoint, const_cast<AActor*>(ImpactingCharacter), AcousticImpactForce, ForceMultiplier);

    // 2. Play gear friction/rattle (Chainmail jingling under the tabard during the impact)
    PlayEquipmentFrictionAcoustics(ImpactingCharacter, MovementSpeed, ImpactLimbId);
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

void UDynamicAudioSubsystem::PlayMaterialImpactAudio(
    FGameplayTag MaterialA, 
    FGameplayTag MaterialB, 
    const FVector& Location, 
    AActor* Instigator, 
    AActor* HitTargetActor, 
    float ImpactVelocity)
{
    PlayProceduralMaterialImpact(MaterialA, MaterialB, Location, Instigator, ImpactVelocity);
}

void UDynamicAudioSubsystem::PlayMaterialImpactAudio(
    FGameplayTag MaterialA, 
    FGameplayTag MaterialB, 
    const FVector& Location, 
    AActor* Instigator, 
    float ImpactVelocity)
{
    PlayProceduralMaterialImpact(MaterialA, MaterialB, Location, Instigator, ImpactVelocity);
}

void UDynamicAudioSubsystem::PlayDynamicAudioAtLocation(
    FGameplayTag AudioTag, 
    const FVector& Location, 
    AActor* Instigator, 
    float VelocityScalar, 
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
    if (VelocityScalar > 0.0f && AudioDef->VelocityToVolumeScalar > 0.0f)
    {
        FinalVolume *= FMath::Clamp(VelocityScalar * AudioDef->VelocityToVolumeScalar, 0.1f, 3.0f);
    }

    if (World->GetNetMode() != NM_DedicatedServer)
    {
        if (USoundBase* LoadedSound = AudioDef->SoundAsset.LoadSynchronous())
        {
            APlayerController* PC = World->GetFirstPlayerController();
            FVector ListenerPos = FVector::ZeroVector;
            FRotator ListenerRot = FRotator::ZeroRotator;
            if (PC) PC->GetPlayerViewPoint(ListenerPos, ListenerRot);

            float AttenRatio = 1.0f, LpfCutoff = 20000.0f, ReverbSend = 0.0f;
            FVector ApparentLocation = Location;

            // Route acoustic paths (Diffraction around walls/frames vs Transmission through walls)
            CalculateAcousticPathProperties(Location, ListenerPos, PC ? PC->GetPawn() : nullptr, AttenRatio, LpfCutoff, ReverbSend, ApparentLocation);

            UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAtLocation(
                World, 
                LoadedSound, 
                ApparentLocation, 
                FRotator::ZeroRotator, 
                FinalVolume * AttenRatio, 
                AudioDef->DefaultPitchMultiplier * PitchMultiplier
            );
            
            if (AudioComp)
            {
                AudioComp->SetLowPassFilterFrequency(LpfCutoff);
                AudioComp->SetFloatParameter(FName("ReverbSendLevel"), ReverbSend);
            }
        }
    }

    if (Instigator && Instigator->HasAuthority() && AudioDef->AssociatedStimulusTag.IsValid())
    {
        BroadcastAcousticStimulusToAI(AudioDef->AssociatedStimulusTag, AudioDef->BaseStimulusMagnitude * FinalVolume, Location, Instigator);
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

void UDynamicAudioSubsystem::BroadcastAcousticStimulusToAI(FGameplayTag StimulusTag, float Magnitude, const FVector& Location, AActor* Instigator)
{
    UWorld* World = GetWorld();
    if (!World) return;

    TMap<FGameplayTag, float> Signatures;
    Signatures.Add(StimulusTag, Magnitude);

    const float QueryRadius = FMath::Clamp(Magnitude * 4.5f, 500.0f, 15000.0f);
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
                        Social->ServerProcessCrimeSensoryEvent(FGameplayTag::EmptyTag, Instigator, nullptr, FGuid(), TArray<FGuid>(), Signatures);
                    }
                }
            }
        }
    }
}

void UDynamicAudioSubsystem::UpdateMaterialScrapeAudio(
    const AActor* ScrapingActor, 
    const AActor* SurfaceActor, 
    const FVector& Location, 
    float ScrapeVelocity, 
    float ContactPressure)
{
    if (!ScrapingActor || ScrapeVelocity < 5.0f) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || GS->GameSettingsConfig.MasterAcousticMetaSounds.MaterialScrape.IsNull()) return;

    // Resolve materials
    const FGameplayTag MatA = ResolveActorAcousticMaterial(ScrapingActor);
    FGameplayTag MatB = FGameplayTag::RequestGameplayTag(TEXT("Material.Stone.Granite"), false);
    if (SurfaceActor)
    {
        const FGameplayTag ResolvedB = ResolveActorAcousticMaterial(SurfaceActor);
        if (ResolvedB.IsValid()) MatB = ResolvedB;
    }

    if (!MatA.IsValid() || !MatB.IsValid()) return;

    const FDynamicMaterialDef* DefA = GS->MaterialMasterRegistry.Find(MatA);
    const FDynamicMaterialDef* DefB = GS->MaterialMasterRegistry.Find(MatB);
    if (!DefA || !DefB) return;

    TWeakObjectPtr<const AActor> WeakScraper(ScrapingActor);
    FActiveScrapeState& State = ActiveScrapeComponents.FindOrAdd(WeakScraper);
    State.LastUpdateTime = World->GetTimeSeconds();
    State.TargetVolume = 1.0f; // Maintain presence

    // Retrieve or initialize the audio component
    if (!State.AudioComp)
    {
        if (USoundBase* ScrapeSound = GS->GameSettingsConfig.MasterAcousticMetaSounds.MaterialScrape.LoadSynchronous())
        {
            State.AudioComp = AcquirePooledAudioComponent(Location);
            if (State.AudioComp)
            {
                State.AudioComp->SetSound(ScrapeSound);
                State.AudioComp->Play();
            }
        }
    }

    if (State.AudioComp)
    {
        State.AudioComp->SetWorldLocation(Location);

        // Map Mass and Force to Procedural Acoustic Parameters
        State.AudioComp->SetFloatParameter(FName("ScrapeVelocity"), ScrapeVelocity);
        State.AudioComp->SetFloatParameter(FName("ContactPressure"), ContactPressure);

        // Inject physical material properties for the resonator bank
        InjectMaterialParametersToMetaSound(State.AudioComp, TEXT("MatA_"), DefA->AcousticProperties);
        InjectMaterialParametersToMetaSound(State.AudioComp, TEXT("MatB_"), DefB->AcousticProperties);
        
        State.AudioComp->SetVolumeMultiplier(State.CurrentVolume); // Updated in Tick
    }
}

void UDynamicAudioSubsystem::UpdateContinuousScrapes(float DeltaSeconds)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const double Now = World->GetTimeSeconds();

    for (auto It = ActiveScrapeComponents.CreateIterator(); It; ++It)
    {
        FActiveScrapeState& State = It.Value();
        
        // If the object hasn't received a velocity update in 0.25 seconds, it has stopped moving
        if ((Now - State.LastUpdateTime) > 0.25f)
        {
            State.TargetVolume = 0.0f;
        }
        else
        {
            State.TargetVolume = 1.0f;
        }

        // Smoothly lerp the volume to prevent abrupt pops
        State.CurrentVolume = FMath::FInterpTo(State.CurrentVolume, State.TargetVolume, DeltaSeconds, 15.0f);

        if (State.AudioComp)
        {
            State.AudioComp->SetVolumeMultiplier(State.CurrentVolume);
        }

        // Cull and release the component when fully silent
        if (State.CurrentVolume <= 0.01f && State.TargetVolume == 0.0f)
        {
            if (State.AudioComp)
            {
                State.AudioComp->Stop();
                State.AudioComp = nullptr;
            }
            It.RemoveCurrent();
        }
    }
}