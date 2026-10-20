// DynamicNameGeneratorLibrary.cpp
#include "DynamicNameGeneratorLibrary.h"
#include "DynamicGameState.h"

namespace NamingInternal
{
    // Helper for weighted random selection from a filtered array of lexicon entries
    FString SelectWeightedToken(const TArray<const FNameLexiconEntry*>& ValidEntries)
    {
        if (ValidEntries.Num() == 0) return TEXT("Unknown");

        float TotalWeight = 0.0f;
        for (const FNameLexiconEntry* Entry : ValidEntries)
        {
            TotalWeight += FMath::Max(0.01f, Entry->GivenNameWeight); // Fallback weight safety
        }

        const float RandRoll = FMath::FRandRange(0.0f, TotalWeight);
        float Cumulative = 0.0f;

        for (const FNameLexiconEntry* Entry : ValidEntries)
        {
            Cumulative += FMath::Max(0.01f, Entry->GivenNameWeight);
            if (RandRoll <= Cumulative)
            {
                return Entry->Token;
            }
        }

        return ValidEntries[0]->Token;
    }

    FString SelectWeightedSurnameToken(const TArray<const FNameLexiconEntry*>& ValidEntries)
    {
        if (ValidEntries.Num() == 0) return TEXT("Smith");

        float TotalWeight = 0.0f;
        for (const FNameLexiconEntry* Entry : ValidEntries)
        {
            TotalWeight += FMath::Max(0.01f, Entry->SurnameWeight);
        }

        const float RandRoll = FMath::FRandRange(0.0f, TotalWeight);
        float Cumulative = 0.0f;

        for (const FNameLexiconEntry* Entry : ValidEntries)
        {
            Cumulative += FMath::Max(0.01f, Entry->SurnameWeight);
            if (RandRoll <= Cumulative)
            {
                return Entry->Token;
            }
        }

        return ValidEntries[0]->Token;
    }
}

FString UDynamicNameGeneratorLibrary::GenerateGivenName(const UObject* WorldContextObject, FGameplayTag CharacterTemplateTag, ECharacterGender Gender)
{
    if (!WorldContextObject || !CharacterTemplateTag.IsValid()) return TEXT("Stranger");
    UWorld* World = WorldContextObject->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || GS->NameGenerationRulesRegistry.Num() == 0) return TEXT("Aiden");

    // 1. Gather all candidate culture configs weighted by cultural adoption
    TArray<const FNameGenerationRuleConfig*> CandidateConfigs;
    TArray<float> ConfigWeights;
    float TotalConfigWeight = 0.0f;

    for (const auto& Pair : GS->NameGenerationRulesRegistry)
    {
        float Weight = Pair.Value.CulturalAdoptionWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        if (Weight > 0.0f)
        {
            CandidateConfigs.Add(&Pair.Value);
            ConfigWeights.Add(Weight);
            TotalConfigWeight += Weight;
        }
    }

    const FNameGenerationRuleConfig* SelectedRule = nullptr;
    if (CandidateConfigs.Num() > 0 && TotalConfigWeight > 0.0f)
    {
        const float Roll = FMath::FRandRange(0.0f, TotalConfigWeight);
        float Cumulative = 0.0f;
        for (int32 i = 0; i < CandidateConfigs.Num(); ++i)
        {
            Cumulative += ConfigWeights[i];
            if (Roll <= Cumulative)
            {
                SelectedRule = CandidateConfigs[i];
                break;
            }
        }
    }

    if (!SelectedRule)
    {
        SelectedRule = GS->NameGenerationRulesRegistry.Find(CharacterTemplateTag);
    }
    if (!SelectedRule)
    {
        for (const auto& Pair : GS->NameGenerationRulesRegistry)
        {
            SelectedRule = &Pair.Value;
            break;
        }
    }
    if (!SelectedRule) return TEXT("Aiden");

    // 2. Select name based on GenerationMode
    if (SelectedRule->GenerationMode == ENameGenerationMode::StaticPool || SelectedRule->GenerationMode == ENameGenerationMode::PatronymicOrClan)
    {
        TArray<const FNameLexiconEntry*> ValidEntries;
        float TotalWeight = 0.0f;

        for (const FNameLexiconEntry& Entry : SelectedRule->StandaloneNames)
        {
            if (Entry.GivenNameWeight <= 0.0f) continue;
            if (Entry.GenderRestriction != ECharacterGender::Neutral && Entry.GenderRestriction != Gender) continue;

            const float Prevalence = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
            const float FinalWeight = Entry.GivenNameWeight * FMath::Max(0.001f, Prevalence);

            ValidEntries.Add(&Entry);
            TotalWeight += FinalWeight;
        }

        if (ValidEntries.Num() == 0) return TEXT("Aiden");

        const float Roll = FMath::FRandRange(0.0f, TotalWeight);
        float Cumulative = 0.0f;
        for (const FNameLexiconEntry* Entry : ValidEntries)
        {
            const float Prevalence = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
            Cumulative += Entry->GivenNameWeight * FMath::Max(0.001f, Prevalence);
            if (Roll <= Cumulative)
            {
                return Entry->Token;
            }
        }
        return ValidEntries[0]->Token;
    }
    else if (SelectedRule->GenerationMode == ENameGenerationMode::Compound)
    {
        auto PickWeightedToken = [&CharacterTemplateTag](const TArray<FNameLexiconEntry>& Pool) -> FString
        {
            float Sum = 0.0f;
            TArray<const FNameLexiconEntry*> Filtered;
            for (const FNameLexiconEntry& Entry : Pool)
            {
                if (Entry.GivenNameWeight <= 0.0f) continue;
                const float Prev = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                Filtered.Add(&Entry);
                Sum += Entry.GivenNameWeight * FMath::Max(0.001f, Prev);
            }
            if (Filtered.Num() == 0) return TEXT("");
            const float R = FMath::FRandRange(0.0f, Sum);
            float Cum = 0.0f;
            for (const FNameLexiconEntry* E : Filtered)
            {
                const float Prev = E->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                Cum += E->GivenNameWeight * FMath::Max(0.001f, Prev);
                if (R <= Cum) return E->Token;
            }
            return Filtered[0]->Token;
        };

        const FString Prefix = PickWeightedToken(SelectedRule->PrefixPool);
        const FString Suffix = PickWeightedToken(SelectedRule->SuffixPool);
        return Prefix + Suffix;
    }

    return TEXT("Traveler");
}

FString UDynamicNameGeneratorLibrary::GenerateSurname(const UObject* WorldContextObject, FGameplayTag CharacterTemplateTag, const TArray<FString>& UsedSurnames)
{
    if (!WorldContextObject || !CharacterTemplateTag.IsValid()) return TEXT("Sterling");
    UWorld* World = WorldContextObject->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || GS->NameGenerationRulesRegistry.Num() == 0) return TEXT("Sterling");

    TArray<const FNameGenerationRuleConfig*> CandidateConfigs;
    TArray<float> ConfigWeights;
    float TotalConfigWeight = 0.0f;

    for (const auto& Pair : GS->NameGenerationRulesRegistry)
    {
        float Weight = Pair.Value.CulturalAdoptionWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        if (Weight > 0.0f)
        {
            CandidateConfigs.Add(&Pair.Value);
            ConfigWeights.Add(Weight);
            TotalConfigWeight += Weight;
        }
    }

    const FNameGenerationRuleConfig* SelectedRule = nullptr;
    if (CandidateConfigs.Num() > 0 && TotalConfigWeight > 0.0f)
    {
        const float Roll = FMath::FRandRange(0.0f, TotalConfigWeight);
        float Cumulative = 0.0f;
        for (int32 i = 0; i < CandidateConfigs.Num(); ++i)
        {
            Cumulative += ConfigWeights[i];
            if (Roll <= Cumulative)
            {
                SelectedRule = CandidateConfigs[i];
                break;
            }
        }
    }

    if (!SelectedRule) SelectedRule = GS->NameGenerationRulesRegistry.Find(CharacterTemplateTag);
    if (!SelectedRule)
    {
        for (const auto& Pair : GS->NameGenerationRulesRegistry)
        {
            SelectedRule = &Pair.Value;
            break;
        }
    }
    if (!SelectedRule) return TEXT("Sterling");

    FString CandidateSurname = TEXT("Sterling");
    int32 Attempts = 0;

    do
    {
        if (SelectedRule->GenerationMode == ENameGenerationMode::StaticPool || SelectedRule->GenerationMode == ENameGenerationMode::PatronymicOrClan)
        {
            TArray<const FNameLexiconEntry*> ValidEntries;
            float TotalWeight = 0.0f;

            for (const FNameLexiconEntry& Entry : SelectedRule->StandaloneNames)
            {
                if (Entry.SurnameWeight <= 0.0f) continue;
                const float Prevalence = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                const float FinalWeight = Entry.SurnameWeight * FMath::Max(0.001f, Prevalence);

                ValidEntries.Add(&Entry);
                TotalWeight += FinalWeight;
            }

            if (ValidEntries.Num() > 0)
            {
                const float Roll = FMath::FRandRange(0.0f, TotalWeight);
                float Cumulative = 0.0f;
                for (const FNameLexiconEntry* Entry : ValidEntries)
                {
                    const float Prevalence = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                    Cumulative += Entry->SurnameWeight * FMath::Max(0.001f, Prevalence);
                    if (Roll <= Cumulative)
                    {
                        CandidateSurname = Entry->Token;
                        break;
                    }
                }
            }
        }
        else if (SelectedRule->GenerationMode == ENameGenerationMode::Compound)
        {
            auto PickWeightedSurnameToken = [&CharacterTemplateTag](const TArray<FNameLexiconEntry>& Pool) -> FString
            {
                float Sum = 0.0f;
                TArray<const FNameLexiconEntry*> Filtered;
                for (const FNameLexiconEntry& Entry : Pool)
                {
                    if (Entry.SurnameWeight <= 0.0f) continue;
                    const float Prev = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                    Filtered.Add(&Entry);
                    Sum += Entry.SurnameWeight * FMath::Max(0.001f, Prev);
                }
                if (Filtered.Num() == 0) return TEXT("");
                const float R = FMath::FRandRange(0.0f, Sum);
                float Cum = 0.0f;
                for (const FNameLexiconEntry* E : Filtered)
                {
                    const float Prev = E->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
                    Cum += E->SurnameWeight * FMath::Max(0.001f, Prev);
                    if (R <= Cum) return E->Token;
                }
                return Filtered[0]->Token;
            };

            const FString Prefix = PickWeightedSurnameToken(SelectedRule->PrefixPool);
            const FString Suffix = PickWeightedSurnameToken(SelectedRule->SuffixPool);
            CandidateSurname = Prefix + Suffix;
        }

        ++Attempts;
    }
    while (UsedSurnames.Contains(CandidateSurname) && Attempts < 10);

    return CandidateSurname;
}