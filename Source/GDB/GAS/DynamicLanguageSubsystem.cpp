// DynamicLanguageSubsystem.cpp
#include "DynamicLanguageSubsystem.h"
#include "DynamicLanguageComponent.h"
#include "DynamicSocialComponent.h"
#include "Engine/OverlapResult.h"
#include "DynamicGameState.h"

void UDynamicLanguageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDynamicLanguageSubsystem::Deinitialize()
{
	Super::Deinitialize();
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