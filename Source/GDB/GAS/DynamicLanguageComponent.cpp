// DynamicLanguageComponent.cpp
#include "DynamicLanguageComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "Net/UnrealNetwork.h"

UDynamicLanguageComponent::UDynamicLanguageComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedKnownWords.OwningComp = this;
    ActiveSpokenLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
}

void UDynamicLanguageComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedKnownWords.OwningComp = this;
    RebuildKnownWordCache();
}

void UDynamicLanguageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicLanguageComponent, ActiveSpokenLanguageTag);
    DOREPLIFETIME(UDynamicLanguageComponent, ReplicatedKnownWords);
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

FString UDynamicLanguageComponent::DecryptDialogueString(
    FGameplayTag SpokenLanguageTag, 
    const FString& RawUtterance, 
    FGameplayTag TargetLocalizationTag) const
{
    // Common is universal: no decryption needed
    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (SpokenLanguageTag == CommonLangTag)
    {
        return RawUtterance;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return RawUtterance;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(SpokenLanguageTag);
    if (!LangDef) return RawUtterance;

    // Check listener's comprehension attribute
    float ComprehensionLevel = 0.0f;
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            ComprehensionLevel = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
        }
    }

    // Tokenize utterance by whitespace and punctuation
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

        // 1. Direct Word-Knowledge Cache Check
        if (IsWordKnown(SpokenLanguageTag, Token))
        {
            bUnderstood = true;
        }

        // 2. Vocabulary Registry Check against Comprehension Attribute
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
                // Native foreign word is heard without understanding
                TranslatedWord = VocabMatch->NativeWord;
            }
        }
        else
        {
            // Unknown foreign word: retain native spelling or procedural phonemes
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

void UDynamicLanguageComponent::ServerRegisterLanguageExposure(FGameplayTag SpokenLanguageTag, const FString& SpokenSentence)
{
    if (!GetOwner()->HasAuthority()) return;

    static const FGameplayTag CommonLangTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    if (SpokenLanguageTag == CommonLangTag) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicLanguageDef* LangDef = GS->LanguageMasterRegistry.Find(SpokenLanguageTag);
    if (!LangDef) return;

    // Track encounter counts for vocabulary words heard
    for (const FDynamicVocabularyWordDef& WordDef : LangDef->Vocabulary)
    {
        if (SpokenSentence.Contains(WordDef.NativeWord, ESearchCase::IgnoreCase))
        {
            int32 FoundIdx = ReplicatedKnownWords.Items.IndexOfByPredicate([&](const FDynamicKnownWordItem& Item) {
                return Item.LanguageTag == SpokenLanguageTag && Item.UnlockedNativeWord.Equals(WordDef.NativeWord, ESearchCase::IgnoreCase);
            });

            if (FoundIdx != INDEX_NONE)
            {
                ReplicatedKnownWords.Items[FoundIdx].TimesEncountered++;
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

            // Populate public challenge payload (CorrectTranslation is strictly omitted)
            OutChallenge.ChallengeGuid = ChallengeGuid;
            OutChallenge.LanguageTag = LanguageTag;
            OutChallenge.NativeWord = WordDef.NativeWord;
            OutChallenge.Difficulty = WordDef.DifficultyRating;
            OutChallenge.ShuffledOptions.Empty();
            OutChallenge.ShuffledOptions.Add(CorrectAnswer);
            OutChallenge.ShuffledOptions.Append(WordDef.MiniGameDecoyOptions);

            // Fisher-Yates Shuffle
            for (int32 i = OutChallenge.ShuffledOptions.Num() - 1; i > 0; --i)
            {
                int32 SwapIdx = FMath::RandRange(0, i);
                OutChallenge.ShuffledOptions.Swap(i, SwapIdx);
            }

            // Secure server-only storage
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
        return; // Challenge invalid, expired, or already answered
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

        // Grant comprehension progression
        if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()))
            {
                const float CurrentXP = AttrSet->GetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, 0);
                AttrSet->SetDynamicAttributeValue(LangDef->ComprehensionAttributeTag, CurrentXP + XPToGrant, 0);
            }
        }

        // Add to replicated FastArray and local cache
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