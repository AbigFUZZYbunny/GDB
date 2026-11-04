// DynamicLanguageSubsystem.cpp
#include "DynamicLanguageSubsystem.h"
#include "DynamicLanguageComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicBuildingSubsystem.h"
#include "Engine/OverlapResult.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UDynamicLanguageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDynamicLanguageSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UDynamicLanguageSubsystem::Tick(float DeltaTime)
{
    if (ActiveSpeakers.Num() == 0) return;

    for (int32 i = ActiveSpeakers.Num() - 1; i >= 0; --i)
    {
        FActiveSpeakerState& State = ActiveSpeakers[i];
        UDynamicLanguageComponent* Speaker = State.SpeakerComponent.Get();

        if (!Speaker || !Speaker->GetOwner())
        {
            ActiveSpeakers.RemoveAtSwap(i);
            continue;
        }

        ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(Speaker->GetOwner());
        USkeletalMeshComponent* Mesh = CharOwner ? CharOwner->GetMeshComponent() : nullptr;

        bool bHasActiveBlends = false;

        for (int32 v = State.VisemeQueue.Num() - 1; v >= 0; --v)
        {
            FActiveViseme& Vis = State.VisemeQueue[v];
            Vis.TimeRemaining -= DeltaTime;

            const float Target = (Vis.TimeRemaining > 0.0f) ? Vis.TargetWeight : 0.0f;
            const float Speed = (Vis.TimeRemaining > 0.0f) ? 15.0f : 20.0f; 

            Vis.CurrentWeight = FMath::FInterpTo(Vis.CurrentWeight, Target, DeltaTime, Speed);

            if (Mesh)
            {
                Mesh->SetMorphTarget(Vis.MorphTargetName, Vis.CurrentWeight);
            }

            Speaker->OnVisemeFrameGenerated.Broadcast(Vis.MorphTargetName, Vis.CurrentWeight);

            if (Vis.TimeRemaining <= 0.0f && FMath::IsNearlyZero(Vis.CurrentWeight, 0.05f))
            {
                if (Mesh) Mesh->SetMorphTarget(Vis.MorphTargetName, 0.0f);
                State.VisemeQueue.RemoveAt(v);
            }
            else
            {
                bHasActiveBlends = true;
            }
        }

        if (!bHasActiveBlends)
        {
            ActiveSpeakers.RemoveAtSwap(i);
        }
    }
}

void UDynamicLanguageSubsystem::RegisterActiveVisemes(UDynamicLanguageComponent* Speaker, const TArray<FActiveViseme>& NewVisemes)
{
    if (!Speaker) return;

    for (FActiveSpeakerState& State : ActiveSpeakers)
    {
        if (State.SpeakerComponent.Get() == Speaker)
        {
            State.VisemeQueue.Append(NewVisemes);
            return;
        }
    }

    FActiveSpeakerState NewState;
    NewState.SpeakerComponent = Speaker;
    NewState.VisemeQueue = NewVisemes;
    ActiveSpeakers.Add(NewState);
}

void UDynamicLanguageSubsystem::UnregisterSpeaker(UDynamicLanguageComponent* Speaker)
{
    ActiveSpeakers.RemoveAllSwap([Speaker](const FActiveSpeakerState& State) {
        return State.SpeakerComponent.Get() == Speaker;
    });
}

FString UDynamicLanguageSubsystem::FormatSubtitleForListener(
    AActor* Speaker, 
    AActor* Listener, 
    const FString& RawUtterance, 
    FGameplayTag SpokenLanguageTag, 
    FGameplayTag TargetLocalizationTag,
    float AudibleRadius,
    FGameplayTag VolumeTag) const
{
    if (!Listener || RawUtterance.IsEmpty()) return RawUtterance;

    UWorld* World = GetWorld();
    if (!World) return RawUtterance;

    // 1. Acoustic audibility check: if the listener is out of range, sound is completely inaudible
    if (Speaker)
    {
        if (const UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
        {
            const float Dist = FVector::Dist(Speaker->GetActorLocation(), Listener->GetActorLocation());
            if (!ListenerLang->CanHearUtteranceAtDistance(Dist, AudibleRadius, VolumeTag, Speaker->GetActorLocation()))
            {
                return TEXT("...");
            }
        }
    }

    const UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>();
    if (!ListenerLang) return RawUtterance;

    // 2. Decode/Decrypt utterance based on listener's comprehension
    const FString PrimaryDecrypted = ListenerLang->DecryptDialogueString(SpokenLanguageTag, RawUtterance, TargetLocalizationTag);

    // If the listener understood the sentence directly, return it immediately
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (SpokenLanguageTag == CommonLangTag || ListenerLang->CanUnderstandLanguage(SpokenLanguageTag))
    {
        return PrimaryDecrypted;
    }

    // 3. COMPANION INTERPRETER SUPPORT:
    // If the listener has low comprehension, evaluate whether a nearby friendly ally/companion can translate
    const UDynamicSocialComponent* ListenerSocial = Listener->FindComponentByClass<UDynamicSocialComponent>();
    if (ListenerSocial)
    {
        constexpr float InterpreterRadius = 800.0f; // Companion must be within 8 meters
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(InterpreterRadius);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionInterpreterSweep), false, Listener);

        if (World->OverlapMultiByChannel(Overlaps, Listener->GetActorLocation(), FQuat::Identity, ECC_Pawn, Sphere, Params))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                AActor* PotentialAlly = Overlap.GetActor();
                if (!PotentialAlly || PotentialAlly == Listener || PotentialAlly == Speaker) continue;

                // Check relationship affinity or party alliance
                const FDynamicRelationshipEdgeItem* RelEdge = ListenerSocial->FindRelationshipWithActor(PotentialAlly);
                const bool bIsAlly = RelEdge && (
                    RelEdge->PrimaryRelationshipTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Relation.Social.Friend"), false)) ||
                    RelEdge->PrimaryRelationshipTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Relation.Faction.PackLoyalty"), false)) ||
                    RelEdge->PrimaryRelationshipTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Relation.Social.Spouse"), false))
                );

                if (bIsAlly)
                {
                    if (const UDynamicLanguageComponent* AllyLang = PotentialAlly->FindComponentByClass<UDynamicLanguageComponent>())
                    {
                        if (AllyLang->CanUnderstandLanguage(SpokenLanguageTag))
                        {
                            const FString DecryptedByAlly = AllyLang->DecryptDialogueString(SpokenLanguageTag, RawUtterance, TargetLocalizationTag);
                            const FString AllyName = ListenerSocial->ResolveActorDisplayName(PotentialAlly);
                            return FString::Printf(TEXT("[%s translates]: \"%s\""), *AllyName, *DecryptedByAlly);
                        }
                    }
                }
            }
        }
    }

    return PrimaryDecrypted;
}

FString UDynamicLanguageSubsystem::FormatWrittenTextForReader(
    AActor* Reader, 
    const FString& RawWrittenText, 
    FGameplayTag WrittenLanguageTag, 
    FGameplayTag TargetLocalizationTag) const
{
    if (!Reader || RawWrittenText.IsEmpty()) return RawWrittenText;

    if (const UDynamicLanguageComponent* ReaderLang = Reader->FindComponentByClass<UDynamicLanguageComponent>())
    {
        return ReaderLang->DecryptDialogueString(WrittenLanguageTag, RawWrittenText, TargetLocalizationTag);
    }
    return RawWrittenText;
}

bool UDynamicLanguageSubsystem::GetVoiceProfile(FGameplayTag VoiceProfileTag, FVoiceProfileDef& OutProfile) const
{
    if (!VoiceProfileTag.IsValid()) return false;
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    if (const FVoiceProfileDef* Def = GS->VoiceProfileMasterRegistry.Find(VoiceProfileTag))
    {
        OutProfile = *Def;
        return true;
    }
    return false;
}

float UDynamicLanguageSubsystem::CalculateAcousticLowPassFilterFrequency(
    const FVector& SpeakerLoc, 
    const FVector& ListenerLoc, 
    AActor* ListenerActor) const
{
    float CutoffFrequency = 20000.0f; // Default crisp, unobstructed audio
    UWorld* World = GetWorld();
    if (!World) return CutoffFrequency;

    // 1. Check dynamic building wall occlusion
    FHitResult Hit;
    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VoiceLowPassCheck), false, ListenerActor);
    if (World->LineTraceSingleByChannel(Hit, SpeakerLoc, ListenerLoc, ECC_Visibility, TraceParams))
    {
        // Muffle heavily if striking a solid surface
        CutoffFrequency = 1200.0f;
        
        // If it's a building piece, muffle based on its specific material insulation density
        if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
        {
            const float Insulation = BuildSub->EvaluateShelterInsulationAtLocation(Hit.ImpactPoint);
            if (Insulation > 0.0f)
            {
                CutoffFrequency = FMath::Clamp(1200.0f - (Insulation * 100.0f), 400.0f, 1200.0f);
            }
        }
    }

    // 2. High-frequency falloff over extreme distance (simulating air absorption)
    const float Dist = FVector::Dist(SpeakerLoc, ListenerLoc);
    if (Dist > 2000.0f)
    {
        const float FalloffAlpha = FMath::Clamp((Dist - 2000.0f) / 5000.0f, 0.0f, 1.0f);
        CutoffFrequency = FMath::Lerp(CutoffFrequency, 3000.0f, FalloffAlpha);
    }

    return CutoffFrequency;
}