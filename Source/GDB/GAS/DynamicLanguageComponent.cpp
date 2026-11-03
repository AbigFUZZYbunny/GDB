// DynamicLanguageComponent.cpp
#include "DynamicLanguageComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicBuildingSubsystem.h"
#include "DynamicSocialEntity.h"
#include "DynamicLanguageSubsystem.h"
#include "DynamicSocialComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"

UDynamicLanguageComponent::UDynamicLanguageComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedKnownWords.OwningComp = this;
    ActiveSpokenLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    NativeLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
}

void UDynamicLanguageComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedKnownWords.OwningComp = this;
    RebuildKnownWordCache();

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (KnownLanguageTags.Num() == 0)
        {
            KnownLanguageTags.AddUnique(NativeLanguageTag);
            static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
            KnownLanguageTags.AddUnique(CommonLangTag);
        }
    }
}

void UDynamicLanguageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicLanguageComponent, ActiveSpokenLanguageTag);
    DOREPLIFETIME(UDynamicLanguageComponent, NativeLanguageTag);
    DOREPLIFETIME(UDynamicLanguageComponent, KnownLanguageTags);
    DOREPLIFETIME(UDynamicLanguageComponent, ReplicatedKnownWords);
}

void UDynamicLanguageComponent::InitializeFromTemplate(const FCharacterTemplateDef& TemplateDef)
{
    if (TemplateDef.NativeLanguageTag.IsValid())
    {
        NativeLanguageTag = TemplateDef.NativeLanguageTag;
        ActiveSpokenLanguageTag = TemplateDef.NativeLanguageTag;
    }

    KnownLanguageTags = TemplateDef.KnownLanguageTags;
    if (NativeLanguageTag.IsValid())
    {
        KnownLanguageTags.AddUnique(NativeLanguageTag);
    }
}

void FDynamicKnownWordItem::PostReplicatedAdd(const FFastKnownWordArray& InArrayOwner)
{
    if (InArrayOwner.OwningComp) InArrayOwner.OwningComp->RebuildKnownWordCache();
}

void FDynamicKnownWordItem::PostReplicatedChange(const FFastKnownWordArray& InArrayOwner)
{
    if (InArrayOwner.OwningComp) InArrayOwner.OwningComp->RebuildKnownWordCache();
}

void UDynamicLanguageComponent::RebuildKnownWordCache()
{
    LocalKnownWordCache.Empty(ReplicatedKnownWords.Items.Num());
    for (const FDynamicKnownWordItem& Item : ReplicatedKnownWords.Items)
    {
        LocalKnownWordCache.Add(Item.LanguageTag.ToString() + TEXT("_") + Item.UnlockedNativeWord.ToLower());
    }
}

bool UDynamicLanguageComponent::IsWordKnown(FGameplayTag LanguageTag, const FString& NativeWord) const
{
    return LocalKnownWordCache.Contains(LanguageTag.ToString() + TEXT("_") + NativeWord.ToLower());
}

bool UDynamicLanguageComponent::CanSpeakLanguage(FGameplayTag LanguageTag) const
{
    return LanguageTag.IsValid() && (LanguageTag == NativeLanguageTag || KnownLanguageTags.Contains(LanguageTag));
}

bool UDynamicLanguageComponent::CanUnderstandLanguage(FGameplayTag LanguageTag) const
{
    if (CanSpeakLanguage(LanguageTag)) return true;

    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (LanguageTag == CommonLangTag) return true;

    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
            {
                if (const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(LanguageTag))
                {
                    if (LangDef->ComprehensionAttributeTag.IsValid())
                    {
                        return AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0) >= 75.0f;
                    }
                }
            }
        }
    }
    return false;
}

float UDynamicLanguageComponent::GetAuditoryPerceptionMultiplier() const
{
    float Multiplier = 1.0f;
    const AActor* OwnerActor = GetOwner();
    if (!OwnerActor) return Multiplier;

    // 1. Evaluate acoustic attributes (Hearing attribute or Perception attribute)
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(OwnerActor))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            static const FGameplayTag HearingAttrTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Sense.Hearing"), false);
            static const FGameplayTag PerceptionAttrTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stat.Perception"), false);

            if (HearingAttrTag.IsValid())
            {
                const float HearingVal = AttrSet->GetDynamicAttributeValue(HearingAttrTag, 0);
                if (HearingVal > 0.0f)
                {
                    Multiplier *= (HearingVal / 100.0f);
                }
            }
            if (PerceptionAttrTag.IsValid())
            {
                const float PerceptionVal = AttrSet->GetDynamicAttributeValue(PerceptionAttrTag, 0);
                if (PerceptionVal > 0.0f)
                {
                    Multiplier += ((PerceptionVal - 10.0f) * 0.03f);
                }
            }
        }

        // 2. Evaluate active Gameplay Tags (Buffs, Abilities, Spying Stances)
        if (const UAbilitySystemComponent* ASC = CharOwner->GetAbilitySystemComponent())
        {
            static const FGameplayTag UltraHearingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Buff.UltraHearing"), false);
            static const FGameplayTag SpyingStanceTag = FGameplayTag::RequestGameplayTag(TEXT("State.Ability.FocusHearing"), false);

            if (UltraHearingTag.IsValid() && ASC->HasMatchingGameplayTag(UltraHearingTag))
            {
                Multiplier *= 4.5f; // Amplifies whisper detection up to 15m
            }
            if (SpyingStanceTag.IsValid() && ASC->HasMatchingGameplayTag(SpyingStanceTag))
            {
                Multiplier *= 2.0f;
            }
        }
    }

    return FMath::Max(0.1f, Multiplier);
}

bool UDynamicLanguageComponent::CanHearUtteranceAtDistance(
    float DistanceUnits, 
    float AudibleVolumeRadius, 
    FGameplayTag VolumeTag, 
    const FVector& SpeakerLocation, 
    AActor* SpeakerActor) const
{
    if (DistanceUnits <= 0.0f) return true;

    UWorld* World = GetWorld();
    if (!World) return false;

    // 1. Auditory Perception Multiplier (Attributes, Buffs, Ultra-Hearing)
    const float PerceptionMultiplier = GetAuditoryPerceptionMultiplier();

    // 2. Environmental Acoustic Noise Damping (Storms, High Wind, Rain)
    float EnvironmentalNoiseDamping = 0.0f;
    if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
    {
        static const FGameplayTag WindMetricTag = FGameplayTag::RequestGameplayTag(TEXT("Env.Metric.WindSpeed"), false);
        if (WindMetricTag.IsValid())
        {
            const float WindSpeed = GS->GetEnvironmentMetricAtLocation(SpeakerLocation, WindMetricTag);
            if (WindSpeed > 25.0f)
            {
                EnvironmentalNoiseDamping = (WindSpeed - 25.0f) * 12.0f;
            }
        }
    }

    // 3. O(1) Room & Structural Physical Occlusion
    float OcclusionFactor = 1.0f;
    const AActor* OwnerActor = GetOwner();

    if (OwnerActor)
    {
        const UDynamicSocialComponent* ListenerSocial = OwnerActor->FindComponentByClass<UDynamicSocialComponent>();
        const UDynamicSocialComponent* SpeakerSocial = SpeakerActor ? SpeakerActor->FindComponentByClass<UDynamicSocialComponent>() : nullptr;

        // A. O(1) Room Check: If both are within the same property/room, sound is unobstructed
        if (ListenerSocial && SpeakerSocial && ListenerSocial->CurrentPropertyID.IsValid() && ListenerSocial->CurrentPropertyID == SpeakerSocial->CurrentPropertyID)
        {
            OcclusionFactor = 1.0f;
        }
        else
        {
            UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>();
            const float ListenerInsulation = BuildSub ? BuildSub->EvaluateShelterInsulationAtLocation(OwnerActor->GetActorLocation()) : 0.0f;
            const float SpeakerInsulation = BuildSub ? BuildSub->EvaluateShelterInsulationAtLocation(SpeakerLocation) : 0.0f;

            // B. If either actor is inside a sheltered building, attenuate according to structure insulation
            if (ListenerInsulation > 0.0f || SpeakerInsulation > 0.0f)
            {
                const float HighestInsulation = FMath::Max(ListenerInsulation, SpeakerInsulation);
                OcclusionFactor = FMath::Clamp(1.0f - (HighestInsulation * 0.05f), 0.15f, 0.5f);
            }
            else
            {
                // C. Fallback: outdoor physical raycast only if neither is inside a recognized room
                FHitResult Hit;
                FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AcousticOcclusionCheck), false, OwnerActor);
                if (World->LineTraceSingleByChannel(Hit, SpeakerLocation, OwnerActor->GetActorLocation(), ECC_Visibility, TraceParams))
                {
                    OcclusionFactor = 0.25f;
                }
            }
        }
    }

    const float AdjustedRadius = FMath::Max(30.0f, AudibleVolumeRadius - EnvironmentalNoiseDamping);
    const float EffectiveAudibleRadius = AdjustedRadius * PerceptionMultiplier * OcclusionFactor;

    return DistanceUnits <= EffectiveAudibleRadius;
}

void UDynamicLanguageComponent::CheckAndApplyLanguageBreakthrough(FGameplayTag LanguageTag, float OldComprehension, float NewComprehension)
{
    if (!GetOwner()->HasAuthority() || !LanguageTag.IsValid()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicLanguageDef* LangDef = GS ? GS->LanguageMasterRegistry.Find(LanguageTag) : nullptr;
    if (!LangDef) return;

    const float Breakthrough = LangDef->ConversationalBreakthroughThreshold;
    const float Fluency = LangDef->FluencyMasteryThreshold;

    // Breakthrough Tier 1: Conversational acquisition (Add to KnownLanguageTags)
    if (OldComprehension < Breakthrough && NewComprehension >= Breakthrough)
    {
        if (!KnownLanguageTags.Contains(LanguageTag))
        {
            KnownLanguageTags.AddUnique(LanguageTag);
            OnLanguageMasteryTierReached.Broadcast(LanguageTag, 1);
        }
    }

    // Breakthrough Tier 2: Full grammatical fluency (Bypasses pidgin/broken phrasing)
    if (OldComprehension < Fluency && NewComprehension >= Fluency)
    {
        OnLanguageMasteryTierReached.Broadcast(LanguageTag, 2);
    }
}

FGameplayTag UDynamicLanguageComponent::DetermineSpokenLanguageForTarget(AActor* TargetListener, const FGameplayTagContainer& ContextTags)
{
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);

    // 1. If speaking with no specific target (e.g. soliloquy, singing, or general town broadcast)
    if (!TargetListener)
    {
        static const FGameplayTag PublicHelpTag = FGameplayTag::RequestGameplayTag(TEXT("Social.Context.Emergency"), false);
        if (ContextTags.HasTag(PublicHelpTag) && CanSpeakLanguage(CommonLangTag))
        {
            ActiveSpokenLanguageTag = CommonLangTag;
            return ActiveSpokenLanguageTag;
        }

        ActiveSpokenLanguageTag = NativeLanguageTag.IsValid() ? NativeLanguageTag : CommonLangTag;
        return ActiveSpokenLanguageTag;
    }

    const UDynamicLanguageComponent* TargetLang = TargetListener->FindComponentByClass<UDynamicLanguageComponent>();
    if (!TargetLang)
    {
        ActiveSpokenLanguageTag = CanSpeakLanguage(CommonLangTag) ? CommonLangTag : NativeLanguageTag;
        return ActiveSpokenLanguageTag;
    }

    const FGameplayTag TargetNative = TargetLang->NativeLanguageTag;
    const bool bTargetKnowsMyNative = TargetLang->CanUnderstandLanguage(NativeLanguageTag);
    const bool bIKnowTargetNative = CanSpeakLanguage(TargetNative);

    // 2. Determine if the conversation is acoustically private or in a mixed crowd
    static const FGameplayTag PrivateTag = FGameplayTag::RequestGameplayTag(TEXT("Social.Context.Private"), false);
    bool bIsPrivate = ContextTags.HasTag(PrivateTag);

    if (!bIsPrivate)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            // Evaluate presence of 3rd-party non-native speakers within normal conversational distance (1200 units)
            TArray<FOverlapResult> Overlaps;
            FCollisionShape Sphere = FCollisionShape::MakeSphere(1200.0f);
            FCollisionQueryParams Params(SCENE_QUERY_STAT(CrowdLanguageAudit), false, GetOwner());

            int32 NonNativeWitnesses = 0;
            if (World->OverlapMultiByChannel(Overlaps, GetOwner()->GetActorLocation(), FQuat::Identity, ECC_Pawn, Sphere, Params))
            {
                for (const FOverlapResult& Overlap : Overlaps)
                {
                    AActor* Witness = Overlap.GetActor();
                    if (Witness && Witness != GetOwner() && Witness != TargetListener)
                    {
                        if (const UDynamicLanguageComponent* WitnessLang = Witness->FindComponentByClass<UDynamicLanguageComponent>())
                        {
                            if (!WitnessLang->CanUnderstandLanguage(NativeLanguageTag) || !WitnessLang->CanUnderstandLanguage(TargetNative))
                            {
                                NonNativeWitnesses++;
                            }
                        }
                    }
                }
            }

            bIsPrivate = (NonNativeWitnesses == 0);
        }
    }

    // 3. Sociolinguistic Code-Switching Logic
    // Rule A: Cross-cultural intimacy - Human speaking privately to an Elf who knows Elvish speaks Elvish out of respect
    if (bIsPrivate && bIKnowTargetNative && TargetNative != CommonLangTag && NativeLanguageTag == CommonLangTag)
    {
        ActiveSpokenLanguageTag = TargetNative;
        return ActiveSpokenLanguageTag;
    }

    // Rule B: Intraracial camaraderie - Elf speaking to Elf in private speaks Elvish
    if (bIsPrivate && NativeLanguageTag == TargetNative && NativeLanguageTag.IsValid() && NativeLanguageTag != CommonLangTag)
    {
        ActiveSpokenLanguageTag = NativeLanguageTag;
        return ActiveSpokenLanguageTag;
    }

    // Rule C: Private mutual native understanding (e.g. Elf speaking to a Human who understands Elvish in private)
    if (bIsPrivate && bTargetKnowsMyNative && NativeLanguageTag.IsValid() && NativeLanguageTag != CommonLangTag)
    {
        ActiveSpokenLanguageTag = NativeLanguageTag;
        return ActiveSpokenLanguageTag;
    }

    // Rule D: Polite accommodation - In public/mixed crowds or when speaking to non-speakers, switch to Common
    if (CanSpeakLanguage(CommonLangTag) && TargetLang->CanSpeakLanguage(CommonLangTag))
    {
        ActiveSpokenLanguageTag = CommonLangTag;
        return ActiveSpokenLanguageTag;
    }

    // Rule E: Fallback to speaker's native tongue
    ActiveSpokenLanguageTag = NativeLanguageTag.IsValid() ? NativeLanguageTag : CommonLangTag;
    return ActiveSpokenLanguageTag;
}

FString UDynamicLanguageComponent::DecryptDialogueString(
    FGameplayTag SpokenLanguageTag, 
    const FString& RawUtterance, 
    FGameplayTag TargetLocalizationTag) const
{
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (SpokenLanguageTag == CommonLangTag || CanSpeakLanguage(SpokenLanguageTag))
    {
        return RawUtterance;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return RawUtterance;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(SpokenLanguageTag);
    if (!LangDef) return RawUtterance;

    float ComprehensionLevel = 0.0f;
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            ComprehensionLevel = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
        }
    }

    TArray<FString> Tokens;
    TArray<FString> Separators;
    FString CurrentWord;

    for (int32 i = 0; i < RawUtterance.Len(); ++i)
    {
        const TCHAR C = RawUtterance[i];
        if (FChar::IsAlnum(C) || C == '\'')
        {
            CurrentWord.AppendChar(C);
        }
        else
        {
            if (!CurrentWord.IsEmpty())
            {
                Tokens.Add(CurrentWord);
                CurrentWord.Empty();
            }
            Separators.Add(FString(1, &C));
        }
    }
    if (!CurrentWord.IsEmpty())
    {
        Tokens.Add(CurrentWord);
    }

    FString DecryptedResult;
    int32 SepIndex = 0;

    for (const FString& Token : Tokens)
    {
        FString TranslatedWord = Token;
        bool bUnderstood = false;

        if (IsWordKnown(SpokenLanguageTag, Token))
        {
            bUnderstood = true;
        }

        const FDynamicVocabularyWordDef* VocabMatch = nullptr;
        for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
        {
            if (WordDef.NativeWord.Equals(Token, ESearchCase::IgnoreCase))
            {
                VocabMatch = &WordDef;
                break;
            }
        }

        if (VocabMatch)
        {
            if (bUnderstood || ComprehensionLevel >= VocabMatch->DifficultyRating)
            {
                FString LocalizedText = VocabMatch->LocalizedTranslations.GetLocalizedText(TargetLocalizationTag);
                if (!LocalizedText.IsEmpty())
                {
                    TranslatedWord = LocalizedText;
                }
            }
            else
            {
                TranslatedWord = VocabMatch->NativeWord;
            }
        }
        else
        {
            TranslatedWord = Token;
        }

        DecryptedResult.Append(TranslatedWord);
        if (Separators.IsValidIndex(SepIndex))
        {
            DecryptedResult.Append(Separators[SepIndex]);
            ++SepIndex;
        }
        else
        {
            DecryptedResult.Append(TEXT(" "));
        }
    }

    return DecryptedResult.TrimEnd();
}

FString UDynamicLanguageComponent::TranslateToNativeTongue(FGameplayTag LanguageTag, const FString& SourceText) const
{
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (LanguageTag == CommonLangTag || !LanguageTag.IsValid()) return SourceText;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return SourceText;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(LanguageTag);
    if (!LangDef) return SourceText;

    TArray<FString> Tokens;
    TArray<FString> Separators;
    FString CurrentWord;

    for (int32 i = 0; i < SourceText.Len(); ++i)
    {
        const TCHAR C = SourceText[i];
        if (FChar::IsAlnum(C) || C == '\'')
        {
            CurrentWord.AppendChar(C);
        }
        else
        {
            if (!CurrentWord.IsEmpty())
            {
                Tokens.Add(CurrentWord);
                CurrentWord.Empty();
            }
            Separators.Add(FString(1, &C));
        }
    }
    if (!CurrentWord.IsEmpty()) Tokens.Add(CurrentWord);

    FString TranslatedResult;
    int32 SepIndex = 0;

    for (const FString& Token : Tokens)
    {
        bool bFoundTranslation = false;
        FString OutWord = Token;
        const FString CleanToken = Token.ToLower();

        // 1. Search dictionary for established reverse mapping (English -> Native)
        for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
        {
            for (const FTagStringPair& TransPair : WordDef.LocalizedTranslations.ReplicatedEntries)
            {
                if (TransPair.DisplayValue.Equals(CleanToken, ESearchCase::IgnoreCase))
                {
                    OutWord = WordDef.NativeWord;
                    if (FChar::IsUpper(Token[0]) && OutWord.Len() > 0)
                    {
                        OutWord[0] = FChar::ToUpper(OutWord[0]);
                    }
                    bFoundTranslation = true;
                    break;
                }
            }
            if (bFoundTranslation) break;
        }

        // 2. Procedural Word Generation & Dynamic Definition Persistence:
        // When no translation exists, coin the word using phonemes and add it permanently to definitions
        if (!bFoundTranslation && LangDef->PhoneticPhonemes.Num() > 0)
        {
            OutWord = GS->GetOrCreateDynamicVocabularyWord(LanguageTag, CleanToken);
            if (FChar::IsUpper(Token[0]) && OutWord.Len() > 0)
            {
                OutWord[0] = FChar::ToUpper(OutWord[0]);
            }
        }

        TranslatedResult.Append(OutWord);
        if (Separators.IsValidIndex(SepIndex))
        {
            TranslatedResult.Append(Separators[SepIndex]);
            ++SepIndex;
        }
        else if (Token != Tokens.Last())
        {
            TranslatedResult.Append(TEXT(" "));
        }
    }

    return TranslatedResult.TrimEnd();
}

void UDynamicLanguageComponent::ServerRegisterLanguageExposure(FGameplayTag SpokenLanguageTag, const FString& SpokenSentence)
{
    if (!GetOwner()->HasAuthority()) return;

    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (SpokenLanguageTag == CommonLangTag || CanSpeakLanguage(SpokenLanguageTag)) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(SpokenLanguageTag);
    if (!LangDef) return;

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    UDynamicAttributeSet* AttrSet = CharOwner ? const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()) : nullptr;

    for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
    {
        if (SpokenSentence.Contains(WordDef.NativeWord, ESearchCase::IgnoreCase))
        {
            int32 FoundIdx = ReplicatedKnownWords.Items.IndexOfByPredicate([&](const FDynamicKnownWordItem& Item) {
                return Item.LanguageTag == SpokenLanguageTag && Item.UnlockedNativeWord.Equals(WordDef.NativeWord, ESearchCase::IgnoreCase);
            });

            int32 UpdatedCount = 1;

            if (FoundIdx != INDEX_NONE)
            {
                ReplicatedKnownWords.Items[FoundIdx].TimesEncountered++;
                UpdatedCount = ReplicatedKnownWords.Items[FoundIdx].TimesEncountered;
                ReplicatedKnownWords.MarkItemDirty(ReplicatedKnownWords.Items[FoundIdx]);
            }
            else
            {
                FDynamicKnownWordItem NewItem;
                NewItem.LanguageTag = SpokenLanguageTag;
                NewItem.UnlockedNativeWord = WordDef.NativeWord;
                NewItem.TimesEncountered = 1;

                FDynamicKnownWordItem& Added = ReplicatedKnownWords.Items.Add_GetRef(NewItem);
                ReplicatedKnownWords.MarkItemDirty(Added);
            }

            // Learning Progression: Hearing words repeatedly grants passive comprehension XP
            if (AttrSet && LangDef->ComprehensionAttributeTag.IsValid() && (UpdatedCount % 3 == 0))
            {
                const float CurVal = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
                const float PassiveXP = FMath::Max(0.5f, WordDef.ComprehensionXPGranted * 0.15f);
                const float NewVal = CurVal + PassiveXP;
                
                AttrSet->SetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, NewVal, 0);
                CheckAndApplyLanguageBreakthrough(SpokenLanguageTag, CurVal, NewVal);
            }

            // Milestone Trigger: On 3rd encounter, prompt player with mini-game challenge
            if (UpdatedCount == 3)
            {
                FTranslationMiniGameChallenge Challenge;
                RequestMiniGameChallengeForWord(SpokenLanguageTag, WordDef.NativeWord, Challenge);
            }
        }
    }
}

bool UDynamicLanguageComponent::RequestMiniGameChallengeForWord(
    FGameplayTag LanguageTag, 
    const FString& NativeWord, 
    FTranslationMiniGameChallenge& OutChallenge)
{
    if (!GetOwner()->HasAuthority()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(LanguageTag);
    if (!LangDef) return false;

    for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
    {
        if (WordDef.NativeWord.Equals(NativeWord, ESearchCase::IgnoreCase))
        {
            static const FGameplayTag DefaultEnglishTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Text.en"), false);
            const FString CorrectAnswer = WordDef.LocalizedTranslations.GetLocalizedText(DefaultEnglishTag);

            const FGuid ChallengeGuid = FGuid::NewGuid();

            OutChallenge.ChallengeGuid = ChallengeGuid;
            OutChallenge.LanguageTag = LanguageTag;
            OutChallenge.NativeWord = WordDef.NativeWord;
            OutChallenge.Difficulty = WordDef.DifficultyRating;
            OutChallenge.ShuffledOptions.Empty();
            OutChallenge.ShuffledOptions.Add(CorrectAnswer);
            OutChallenge.ShuffledOptions.Append(WordDef.MiniGameDecoyOptions);

            for (int32 i = OutChallenge.ShuffledOptions.Num() - 1; i > 0; --i)
            {
                int32 SwapIdx = FMath::RandRange(0, i);
                OutChallenge.ShuffledOptions.Swap(i, SwapIdx);
            }

            ServerSecretAnswers.Add(ChallengeGuid, CorrectAnswer);
            ServerChallengeLanguages.Add(ChallengeGuid, LanguageTag);
            ServerChallengeNativeWords.Add(ChallengeGuid, WordDef.NativeWord);

            OnTranslationChallengeReady.Broadcast(OutChallenge);
            return true;
        }
    }
    return false;
}

void UDynamicLanguageComponent::ServerSubmitMiniGameAnswer_Implementation(
    const FGuid& ChallengeGuid, 
    const FString& SelectedAnswer)
{
    if (!GetOwner()->HasAuthority()) return;

    FString ExpectedAnswer;
    if (!ServerSecretAnswers.RemoveAndCopyValue(ChallengeGuid, ExpectedAnswer))
    {
        return;
    }

    FGameplayTag LanguageTag;
    ServerChallengeLanguages.RemoveAndCopyValue(ChallengeGuid, LanguageTag);

    FString NativeWord;
    ServerChallengeNativeWords.RemoveAndCopyValue(ChallengeGuid, NativeWord);

    if (SelectedAnswer.Equals(ExpectedAnswer, ESearchCase::IgnoreCase))
    {
        ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
        if (!GS) return;

        const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(LanguageTag);
        if (!LangDef) return;

        float XPToGrant = 2.0f;
        for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
        {
            if (WordDef.NativeWord.Equals(NativeWord, ESearchCase::IgnoreCase))
            {
                XPToGrant = WordDef.ComprehensionXPGranted;
                break;
            }
        }

        if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()))
            {
                const float CurrentXP = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
                const float NewXP = CurrentXP + XPToGrant;
                AttrSet->SetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, NewXP, 0);
                CheckAndApplyLanguageBreakthrough(LanguageTag, CurrentXP, NewXP);
            }
        }

        int32 FoundIdx = ReplicatedKnownWords.Items.IndexOfByPredicate([&](const FDynamicKnownWordItem& Item) {
            return Item.LanguageTag == LanguageTag && Item.UnlockedNativeWord.Equals(NativeWord, ESearchCase::IgnoreCase);
        });

        if (FoundIdx == INDEX_NONE)
        {
            FDynamicKnownWordItem NewItem;
            NewItem.LanguageTag = LanguageTag;
            NewItem.UnlockedNativeWord = NativeWord;
            NewItem.TimesEncountered = 1;
            FDynamicKnownWordItem& Added = ReplicatedKnownWords.Items.Add_GetRef(NewItem);
            ReplicatedKnownWords.MarkItemDirty(Added);
        }

        LocalKnownWordCache.Add(LanguageTag.ToString() + TEXT("_") + NativeWord.ToLower());
        OnWordLearned.Broadcast(LanguageTag, NativeWord);
    }
}

bool UDynamicLanguageComponent::ServerForgetWord(FGameplayTag LanguageTag, const FString& Word)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !LanguageTag.IsValid()) return false;

    int32 Removed = ReplicatedKnownWords.Items.RemoveAll([LanguageTag, &Word](const FDynamicKnownWordItem& Item)
    {
        return Item.LanguageTag == LanguageTag && Item.UnlockedNativeWord.Equals(Word, ESearchCase::IgnoreCase);
    });

    if (Removed > 0)
    {
        ReplicatedKnownWords.MarkArrayDirty();
        RebuildKnownWordCache();
        return true;
    }
    return false;
}

float UDynamicLanguageComponent::GetLanguageFluency(FGameplayTag LanguageTag) const
{
    if (!LanguageTag.IsValid()) return 0.0f;

    // Native language is always 100% fluent
    if (LanguageTag == NativeLanguageTag)
    {
        return 100.0f;
    }

    float BaseFluency = 0.0f;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicLanguageDef* LangDef = GS ? GS->LanguageMasterRegistry.Find(LanguageTag) : nullptr;

    // 1. Query dynamic attribute set for comprehension/fluency
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            if (LangDef && LangDef->ComprehensionAttributeTag.IsValid())
            {
                BaseFluency = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
            }
        }
    }

    // 2. Fall back to conversational baseline if explicitly listed in known languages
    if (BaseFluency <= 0.0f && KnownLanguageTags.Contains(LanguageTag))
    {
        BaseFluency = 45.0f;
    }

    // 3. Mini-game mastery bonus: each word learned through exposure/challenges adds dynamic mastery
    int32 LearnedWordCount = 0;
    for (const FDynamicKnownWordItem& Item : ReplicatedKnownWords.Items)
    {
        if (Item.LanguageTag == LanguageTag)
        {
            LearnedWordCount++;
        }
    }

    const float WordBonus = FMath::Min(25.0f, static_cast<float>(LearnedWordCount) * 1.5f);
    return FMath::Clamp(BaseFluency + WordBonus, 0.0f, 100.0f);
}

FString UDynamicLanguageComponent::GenerateNonNativeSpeakingAttempt(FGameplayTag TargetLanguageTag, const FString& IntendedEnglishText) const
{
    if (IntendedEnglishText.IsEmpty()) return IntendedEnglishText;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicLanguageDef* LangDef = GS ? GS->LanguageMasterRegistry.Find(TargetLanguageTag) : nullptr;

    const float Fluency = GetLanguageFluency(TargetLanguageTag);
    const float MasteryThreshold = LangDef ? LangDef->FluencyMasteryThreshold : 85.0f;

    // Fluent speakers bypass simplification completely
    if (Fluency >= MasteryThreshold)
    {
        static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
        return (TargetLanguageTag == CommonLangTag) ? IntendedEnglishText : TranslateToNativeTongue(TargetLanguageTag, IntendedEnglishText);
    }

    TArray<FString> RawTokens;
    FString TrailingPunctuation;
    FString CurrentWord;

    for (int32 i = 0; i < IntendedEnglishText.Len(); ++i)
    {
        const TCHAR C = IntendedEnglishText[i];
        if (FChar::IsAlnum(C) || C == '\'')
        {
            CurrentWord.AppendChar(C);
        }
        else
        {
            if (!CurrentWord.IsEmpty())
            {
                RawTokens.Add(CurrentWord);
                CurrentWord.Empty();
            }
            if (C == '?' || C == '!' || C == '.')
            {
                TrailingPunctuation.AppendChar(C);
            }
        }
    }
    if (!CurrentWord.IsEmpty()) RawTokens.Add(CurrentWord);
    if (RawTokens.Num() == 0) return IntendedEnglishText;

    // 1. Non-Native Pidgin Simplifications (e.g., "something" -> "thing", "cannot" -> "no")
    TArray<FString> SimplifiedTokens;
    for (const FString& Token : RawTokens)
    {
        FString MappedWord = Token;
        if (LangDef)
        {
            for (const auto& SubPair : LangDef->PidginSubstitutions)
            {
                if (SubPair.Key.Equals(Token, ESearchCase::IgnoreCase))
                {
                    MappedWord = SubPair.Value;
                    break;
                }
            }
        }
        SimplifiedTokens.Add(MappedWord);
    }

    // 2. Stop-Word and Auxiliary Pruning based on Fluency Tiers
    TArray<FString> FilteredTokens;
    for (const FString& Word : SimplifiedTokens)
    {
        const FString Lower = Word.ToLower();
        bool bShouldPrune = false;

        if (LangDef && LangDef->StopWords.Num() > 0)
        {
            for (const FString& StopWord : LangDef->StopWords)
            {
                if (StopWord.Equals(Lower, ESearchCase::IgnoreCase))
                {
                    if (Fluency < 40.0f)
                    {
                        bShouldPrune = true;
                    }
                    else if (Fluency < 70.0f)
                    {
                        if (Lower == TEXT("do") || Lower == TEXT("does") || Lower == TEXT("did") || Lower == TEXT("out") || Lower == TEXT("the") || Lower == TEXT("a") || Lower == TEXT("an"))
                        {
                            bShouldPrune = true;
                        }
                    }
                    else if (Fluency < 85.0f)
                    {
                        if (Lower == TEXT("do") || Lower == TEXT("does"))
                        {
                            bShouldPrune = true;
                        }
                    }
                    break;
                }
            }
        }

        if (!bShouldPrune)
        {
            FilteredTokens.Add(Word);
        }
    }

    if (FilteredTokens.Num() == 0 && SimplifiedTokens.Num() > 0)
    {
        FilteredTokens.Add(SimplifiedTokens.Last());
    }

    // 3. Word-by-Word Translation & Permanent Coining
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    TArray<FString> FinalSpokenWords;

    for (const FString& Token : FilteredTokens)
    {
        if (TargetLanguageTag == CommonLangTag)
        {
            FinalSpokenWords.Add(Token.ToLower());
        }
        else
        {
            bool bWordKnownInTarget = IsWordKnown(TargetLanguageTag, Token);
            FString TargetWord;

            if (GS)
            {
                TargetWord = GS->GetOrCreateDynamicVocabularyWord(TargetLanguageTag, Token);
            }

            if (!TargetWord.IsEmpty() && (bWordKnownInTarget || Fluency >= 50.0f))
            {
                FinalSpokenWords.Add(TargetWord);
            }
            else
            {
                FinalSpokenWords.Add(Token.ToLower());
            }
        }
    }

    // 4. Broken Phrasing & Hesitation Cadence (Lower fluency inserts pauses and uncertainty)
    FString ResultUtterance;
    for (int32 i = 0; i < FinalSpokenWords.Num(); ++i)
    {
        FString Word = FinalSpokenWords[i];
        if (i == 0 && Word.Len() > 0)
        {
            Word[0] = FChar::ToUpper(Word[0]);
        }

        ResultUtterance += Word;

        if (i < FinalSpokenWords.Num() - 1)
        {
            // Severe hesitation for beginners (< 35 fluency)
            if (Fluency < 35.0f)
            {
                ResultUtterance += TEXT("... ");
            }
            else
            {
                ResultUtterance += TEXT(" ");
            }
        }
    }

    if (!TrailingPunctuation.IsEmpty())
    {
        ResultUtterance += TrailingPunctuation;
    }
    else
    {
        ResultUtterance += TEXT("?");
    }

    return ResultUtterance;
}

FString UDynamicLanguageComponent::FormulateSpokenUtterance(FGameplayTag TargetLanguageTag, const FString& IntendedEnglishText) const
{
    if (IntendedEnglishText.IsEmpty()) return IntendedEnglishText;

    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);

    // Native Speaker Path
    if (TargetLanguageTag == NativeLanguageTag)
    {
        if (TargetLanguageTag == CommonLangTag)
        {
            return IntendedEnglishText;
        }
        return TranslateToNativeTongue(TargetLanguageTag, IntendedEnglishText);
    }

    // Non-Native Speaker Attempt Path (Broken / Pidgin Phrasing)
    return GenerateNonNativeSpeakingAttempt(TargetLanguageTag, IntendedEnglishText);
}

bool UDynamicLanguageComponent::ServerBroadcastSpokenUtterance_Validate(
    const FString& RawEnglishText, 
    FGameplayTag VolumeTag, 
    AActor* TargetActor, 
    FGameplayTag AssociatedTopicTag)
{
    return !RawEnglishText.IsEmpty() && RawEnglishText.Len() <= 500;
}

void UDynamicLanguageComponent::ServerBroadcastSpokenUtterance_Implementation(
    const FString& RawEnglishText, 
    FGameplayTag VolumeTag, 
    AActor* TargetActor, 
    FGameplayTag AssociatedTopicTag)
{
    AActor* SpeakerActor = GetOwner();
    UWorld* World = GetWorld();
    if (!SpeakerActor || !World || !SpeakerActor->HasAuthority()) return;

    // 1. Resolve Spoken Language (Code-Switching)
    FGameplayTagContainer ContextTags;
    if (VolumeTag.IsValid()) ContextTags.AddTag(VolumeTag);
    if (AssociatedTopicTag.IsValid()) ContextTags.AddTag(AssociatedTopicTag);
    const FGameplayTag ResolvedLanguageTag = DetermineSpokenLanguageForTarget(TargetActor, ContextTags);

    // 2. Formulate Utterance (Native or Broken/Pidgin attempt based on speaker's fluency)
    const FString FormulatedUtterance = FormulateSpokenUtterance(ResolvedLanguageTag, RawEnglishText);

    // 3. Resolve Acoustic Base Audible Radius
    float BaseRadius = 1500.0f; // Normal speech default
    static const FGameplayTag WhisperTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Whisper"), false);
    static const FGameplayTag ShoutTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Shout"), false);
    static const FGameplayTag ScreamTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Scream"), false);

    if (VolumeTag == WhisperTag) BaseRadius = 300.0f;
    else if (VolumeTag == ShoutTag) BaseRadius = 3000.0f;
    else if (VolumeTag == ScreamTag) BaseRadius = 5000.0f;

    // Health and stance volume modulation
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(SpeakerActor))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            static const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Health"), false);
            if (HealthTag.IsValid())
            {
                const float CurHP = AttrSet->GetDynamicAttributeValue(HealthTag, 0);
                if (CurHP > 0.0f && CurHP <= 30.0f)
                {
                    VolumeTag = ScreamTag;
                    BaseRadius = FMath::Max(BaseRadius, 4500.0f);
                }
            }
        }
        static const FGameplayTag StanceCrouch = FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Crouch"), false);
        if (CharOwner->ActiveMovementStateTag.MatchesTag(StanceCrouch))
        {
            VolumeTag = WhisperTag;
            BaseRadius = FMath::Min(BaseRadius, 300.0f);
        }
    }

    // 4. Record Speaker's own conversational history turn & gossip topic
    UDynamicSocialComponent* SpeakerSocial = SpeakerActor->FindComponentByClass<UDynamicSocialComponent>();
    if (SpeakerSocial)
    {
        SpeakerSocial->RecordDialogueTurn(
            SpeakerSocial->CharacterPersistentGuid, 
            SpeakerSocial->ResolveActorDisplayName(SpeakerActor), 
            ResolvedLanguageTag, 
            FormulatedUtterance
        );

        if (AssociatedTopicTag.IsValid())
        {
            SpeakerSocial->ServerRecordOverheardTopic(AssociatedTopicTag, RawEnglishText, SpeakerSocial->CharacterPersistentGuid, 1.0f);
        }
    }

    // 5. Acoustic Broadcast to Listeners in Proximity
    const float QueryRadius = FMath::Clamp(BaseRadius * 4.5f, 300.0f, 15000.0f);
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(QueryRadius);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpokenUtteranceAcousticSweep), false, SpeakerActor);

    const FVector SpeakerLoc = SpeakerActor->GetActorLocation();

    if (World->OverlapMultiByChannel(Overlaps, SpeakerLoc, FQuat::Identity, ECC_Pawn, Sphere, QueryParams))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* Listener = Overlap.GetActor();
            if (!Listener || Listener == SpeakerActor) continue;

            if (UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
            {
                const float Distance = FVector::Dist(SpeakerLoc, Listener->GetActorLocation());

                if (ListenerLang->CanHearUtteranceAtDistance(Distance, BaseRadius, VolumeTag, SpeakerLoc, SpeakerActor))
                {
                    // A. Language Exposure Acquisition
                    ListenerLang->ServerRegisterLanguageExposure(ResolvedLanguageTag, FormulatedUtterance);

                    // B. Deliver Subtitle to Player Controller via Client RPC
                    APawn* ListenerPawn = Cast<APawn>(Listener);
                    if (ListenerPawn && ListenerPawn->IsPlayerControlled())
                    {
                        ListenerLang->ClientReceiveHeardDialogue(SpeakerActor, FormulatedUtterance, ResolvedLanguageTag, BaseRadius, VolumeTag);
                    }

                    // C. Ingest into Listener's Conversational Memory & Trigger AI Reactivity if an NPC
                    if (UDynamicSocialComponent* ListenerSocial = Listener->FindComponentByClass<UDynamicSocialComponent>())
                    {
                        ListenerSocial->RecordDialogueTurn(
                            SpeakerSocial ? SpeakerSocial->CharacterPersistentGuid : FGuid(),
                            SpeakerSocial ? SpeakerSocial->CharacterDisplayName : SpeakerActor->GetName(),
                            ResolvedLanguageTag,
                            FormulatedUtterance
                        );

                        if (AssociatedTopicTag.IsValid())
                        {
                            ListenerSocial->ServerRecordOverheardTopic(
                                AssociatedTopicTag, 
                                RawEnglishText, 
                                SpeakerSocial ? SpeakerSocial->CharacterPersistentGuid : FGuid(), 
                                0.85f
                            );
                        }

                        if (!ListenerPawn || !ListenerPawn->IsPlayerControlled())
                        {
                            ListenerSocial->HandleHeardDialogueFromSpeaker(SpeakerActor, FormulatedUtterance, ResolvedLanguageTag, VolumeTag);
                        }
                    }
                }
            }
        }
    }
}

void UDynamicLanguageComponent::ClientReceiveHeardDialogue_Implementation(
    AActor* Speaker, 
    const FString& FormulatedSpokenText, 
    FGameplayTag SpokenLanguageTag, 
    float BaseAudibleRadius, 
    FGameplayTag VolumeTag)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UDynamicLanguageSubsystem* Subsystem = World->GetSubsystem<UDynamicLanguageSubsystem>();
    if (!Subsystem) return;

    static const FGameplayTag DefaultLocTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Text.en"), false);

    // Decrypt listener-specific subtitles according to local comprehension level
    const FString DecryptedSubtitle = Subsystem->FormatSubtitleForListener(
        Speaker, 
        GetOwner(), 
        FormulatedSpokenText, 
        SpokenLanguageTag, 
        DefaultLocTag, 
        BaseAudibleRadius, 
        VolumeTag
    );

    // Notify local HUD, Overhead Nameplate, and Chat Box widgets
    OnDialogueSubtitleReceived.Broadcast(Speaker, DecryptedSubtitle, SpokenLanguageTag, VolumeTag);
}

void UDynamicLanguageComponent::ClientReceiveHeardDialogueChunk_Implementation(
    AActor* Speaker, 
    const FString& FormulatedChunk, 
    FGameplayTag SpokenLanguageTag, 
    float BaseAudibleRadius, 
    FGameplayTag VolumeTag, 
    bool bIsFinalChunk)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UDynamicLanguageSubsystem* Subsystem = World->GetSubsystem<UDynamicLanguageSubsystem>();
    if (!Subsystem) return;

    static const FGameplayTag DefaultLocTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Text.en"), false);

    // Decrypt just this specific chunk (word boundary)
    const FString DecryptedChunk = Subsystem->FormatSubtitleForListener(
        Speaker, 
        GetOwner(), 
        FormulatedChunk, 
        SpokenLanguageTag, 
        DefaultLocTag, 
        BaseAudibleRadius, 
        VolumeTag
    );

    // Broadcast to UI for the typing effect
    OnDialogueSubtitleChunkReceived.Broadcast(Speaker, DecryptedChunk, SpokenLanguageTag, VolumeTag, bIsFinalChunk);
}

void UDynamicLanguageComponent::ServerReadWrittenText(FGameplayTag WrittenLanguageTag, const FString& WrittenText, AActor* TextSourceActor)
{
    if (!GetOwner()->HasAuthority() || !WrittenLanguageTag.IsValid() || WrittenText.IsEmpty()) return;

    // 1. Reading grants linguistic exposure just like hearing it spoken
    ServerRegisterLanguageExposure(WrittenLanguageTag, WrittenText);

    // 2. Send to client for decryption and UI display
    if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
    {
        if (PawnOwner->IsPlayerControlled())
        {
            ClientReceiveWrittenText(WrittenText, WrittenLanguageTag, TextSourceActor);
        }
    }
}

void UDynamicLanguageComponent::ClientReceiveWrittenText_Implementation(const FString& RawWrittenText, FGameplayTag WrittenLanguageTag, AActor* TextSourceActor)
{
    static const FGameplayTag DefaultLocTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Text.en"), false);
    
    // Decrypt based on the reader's comprehension
    const FString DecryptedText = DecryptDialogueString(WrittenLanguageTag, RawWrittenText, DefaultLocTag);
    
    OnWrittenTextDecrypted.Broadcast(DecryptedText, WrittenLanguageTag, TextSourceActor);
}

bool UDynamicLanguageComponent::ServerInquireWordFromSpeaker(AActor* SpeakerActor, FGameplayTag LanguageTag, const FString& ForeignWord)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !SpeakerActor || !LanguageTag.IsValid() || ForeignWord.IsEmpty())
    {
        return false;
    }

    const UDynamicLanguageComponent* SpeakerLang = SpeakerActor->FindComponentByClass<UDynamicLanguageComponent>();
    if (!SpeakerLang || !SpeakerLang->CanUnderstandLanguage(LanguageTag))
    {
        return false;
    }

    // 1. Proximity Check: Must be in conversational distance (within 350 units)
    if (FVector::Dist(GetOwner()->GetActorLocation(), SpeakerActor->GetActorLocation()) > 350.0f)
    {
        return false;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(LanguageTag);
    if (!LangDef) return false;

    // 2. Dynamic Teaching Attribute Evaluation (Defined 100% in JSON, no hardcoding)
    float TeachingSkill = 0.0f;
    const UDynamicAttributeSet* SpeakerAttrSet = nullptr;
    if (const ADynamicCharacter* SpeakerChar = Cast<ADynamicCharacter>(SpeakerActor))
    {
        SpeakerAttrSet = SpeakerChar->GetDynamicAttributeSet();
    }
    else if (const UAbilitySystemComponent* SpeakerASC = SpeakerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        SpeakerAttrSet = Cast<UDynamicAttributeSet>(SpeakerASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    }

    if (SpeakerAttrSet && LangDef->TeachingAttributeTag.IsValid())
    {
        TeachingSkill = SpeakerAttrSet->GetDynamicAttributeValue(LangDef->TeachingAttributeTag, 0);

        if (LangDef->MinTeacherAttributeRequired > 0.0f && TeachingSkill < LangDef->MinTeacherAttributeRequired)
        {
            return false;
        }
    }

    // 3. Social Check: Speaker checks affinity toward the inquirer
    const UDynamicSocialComponent* SpeakerSocial = SpeakerActor->FindComponentByClass<UDynamicSocialComponent>();
    const UDynamicSocialComponent* InquirerSocial = GetOwner()->FindComponentByClass<UDynamicSocialComponent>();

    if (SpeakerSocial && InquirerSocial)
    {
        static const FGameplayTag AffinityTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Affinity"), false);
        if (AffinityTag.IsValid())
        {
            const float CurrentAffinity = SpeakerSocial->GetDecayedRelationshipMetric(InquirerSocial->CharacterPersistentGuid, AffinityTag);
            if (CurrentAffinity < 10.0f)
            {
                return false;
            }
        }
    }

    // 4. Resolve Definition from GameState
    FString EnglishTranslation;
    float WordDifficulty = 10.0f;
    if (!GS->FindTranslationForNativeWord(LanguageTag, ForeignWord, EnglishTranslation, WordDifficulty))
    {
        return false;
    }

    // 5. Inquirer learns the word and gains comprehension progression scaled by the teacher's proficiency
    if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()))
        {
            if (LangDef->ComprehensionAttributeTag.IsValid())
            {
                const float CurXP = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
                const float TeachingMultiplier = 1.0f + (TeachingSkill * LangDef->TeachingAttributeBonusScalar);
                const float GrantedXP = FMath::Max(1.5f, (WordDifficulty * 0.25f) * TeachingMultiplier);
                
                const float NewXP = CurXP + GrantedXP;
                AttrSet->SetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, NewXP, 0);
                CheckAndApplyLanguageBreakthrough(LanguageTag, CurXP, NewXP);
            }
        }
    }

    // 6. Teacher Gains Teaching Experience (Pass-back progression if teacher attribute is defined)
    if (SpeakerAttrSet && LangDef->TeachingAttributeTag.IsValid() && LangDef->TeachingAttributeBonusScalar > 0.0f)
    {
        UDynamicAttributeSet* MutableSpeakerAttr = const_cast<UDynamicAttributeSet*>(SpeakerAttrSet);
        const float CurrentTeachingXP = MutableSpeakerAttr->GetDynamicAttributeValue(LangDef->TeachingAttributeTag, 0);
        MutableSpeakerAttr->SetDynamicAttributeValue(LangDef->TeachingAttributeTag, CurrentTeachingXP + 1.0f, 0);
    }

    // 7. Add to student's known words
    const int32 FoundIdx = ReplicatedKnownWords.Items.IndexOfByPredicate([&](const FDynamicKnownWordItem& Item) {
        return Item.LanguageTag == LanguageTag && Item.UnlockedNativeWord.Equals(ForeignWord, ESearchCase::IgnoreCase);
    });

    if (FoundIdx == INDEX_NONE)
    {
        FDynamicKnownWordItem NewItem;
        NewItem.LanguageTag = LanguageTag;
        NewItem.UnlockedNativeWord = ForeignWord;
        NewItem.TimesEncountered = 3;
        FDynamicKnownWordItem& Added = ReplicatedKnownWords.Items.Add_GetRef(NewItem);
        ReplicatedKnownWords.MarkItemDirty(Added);
    }

    LocalKnownWordCache.Add(LanguageTag.ToString() + TEXT("_") + ForeignWord.ToLower());
    OnWordLearned.Broadcast(LanguageTag, ForeignWord);

    // 8. Speaker utters an educational response
    const FString ExplanationPhrase = FString::Printf(TEXT("\"%s\" means \"%s\" in our tongue."), *ForeignWord, *EnglishTranslation);
    static const FGameplayTag NormalVolTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
    
    if (UDynamicLanguageComponent* MutableSpeakerLang = const_cast<UDynamicLanguageComponent*>(SpeakerLang))
    {
        MutableSpeakerLang->ServerBroadcastSpokenUtterance(ExplanationPhrase, NormalVolTag, GetOwner());
    }

    return true;
}

bool UDynamicLanguageComponent::ServerTeachRandomNativeWord(AActor* TargetStudent)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetStudent || !NativeLanguageTag.IsValid()) return false;

    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (NativeLanguageTag == CommonLangTag) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(NativeLanguageTag);
    if (!LangDef || LangDef->Vocabulary.Num() == 0) return false;

    UDynamicLanguageComponent* StudentLang = TargetStudent->FindComponentByClass<UDynamicLanguageComponent>();
    if (!StudentLang) return false;

    // Find words the student DOES NOT know yet
    TArray<const FDynamicVocabularyWordDef*> UnknownWords;
    for (const FDynamicVocabularyWordDef& Word : LangDef->Vocabulary)
    {
        if (!StudentLang->IsWordKnown(NativeLanguageTag, Word.NativeWord))
        {
            UnknownWords.Add(&Word);
        }
    }

    if (UnknownWords.Num() == 0)
    {
        // If they know all fixed words, coin a brand new procedural one to teach them
        const FString ProceduralEnglish = TEXT("magic"); // Placeholder fallback concept
        GS->GetOrCreateDynamicVocabularyWord(NativeLanguageTag, ProceduralEnglish);
        
        // Re-evaluate
        for (const FDynamicVocabularyWordDef& Word : LangDef->Vocabulary)
        {
            if (!StudentLang->IsWordKnown(NativeLanguageTag, Word.NativeWord))
            {
                UnknownWords.Add(&Word);
            }
        }
        if (UnknownWords.Num() == 0) return false;
    }

    // Pick a random unknown word and trigger the challenge for the student
    const int32 RandIdx = FMath::RandRange(0, UnknownWords.Num() - 1);
    const FString PickedNativeWord = UnknownWords[RandIdx]->NativeWord;

    FTranslationMiniGameChallenge Challenge;
    // We execute RequestMiniGameChallengeForWord ON THE STUDENT so their UI receives the event
    return StudentLang->RequestMiniGameChallengeForWord(NativeLanguageTag, PickedNativeWord, Challenge);
}