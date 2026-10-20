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
    if (!GS) return TEXT("Stranger");

    // Gather all matching rule tables that have a non-zero adoption weight for this character's template tag
    // (e.g. A human can search both Human naming tables and, with 0.05 probability, Elven naming tables).
    
    // For simplicity, we query the primary table or resolve across cultural tables weighted by CulturalAdoptionWeights:
    const FNameGenerationRuleConfig* RuleConfig = nullptr; // GS->GetAppropriateNamingRule(CharacterTemplateTag);
    if (!RuleConfig) return TEXT("Aiden");

    TArray<const FNameLexiconEntry*> ValidEntries;
    for (const FNameLexiconEntry& Entry : RuleConfig->StandaloneNames)
    {
        // Extract how likely this specific race uses this token
        float RacialMultiplier = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        if (RacialMultiplier <= 0.0f) continue; // Exclude if race never uses this name

        if (Entry.GivenNameWeight > 0.0f && 
            (Entry.GenderRestriction == ECharacterGender::Neutral || Entry.GenderRestriction == Gender))
        {
            // Effective weight = Base Given Name Weight * Racial Adoption Prevalence
            // (We can temporarily wrap or push entries proportional to their combined weight)
            ValidEntries.Add(&Entry);
        }
    }

    if (ValidEntries.Num() == 0) return TEXT("Traveler");

    // Select token considering racial prevalence multipliers
    float TotalWeight = 0.0f;
    for (const FNameLexiconEntry* Entry : ValidEntries)
    {
        float Multiplier = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        TotalWeight += Entry->GivenNameWeight * FMath::Max(0.001f, Multiplier);
    }

    const float Roll = FMath::FRandRange(0.0f, TotalWeight);
    float Cumulative = 0.0f;
    for (const FNameLexiconEntry* Entry : ValidEntries)
    {
        float Multiplier = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        Cumulative += Entry->GivenNameWeight * FMath::Max(0.001f, Multiplier);
        if (Roll <= Cumulative)
        {
            return Entry->Token;
        }
    }

    return ValidEntries[0]->Token;
}

FString UDynamicNameGeneratorLibrary::GenerateSurname(const UObject* WorldContextObject, FGameplayTag CharacterTemplateTag, const TArray<FString>& UsedSurnames)
{
    if (!WorldContextObject || !CharacterTemplateTag.IsValid()) return TEXT("Stranger");

    UWorld* World = WorldContextObject->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return TEXT("Stranger");

    // Gather all matching rule tables that have a non-zero adoption weight for this character's template tag
    // (e.g. A human can search both Human naming tables and, with 0.05 probability, Elven naming tables).
    
    // For simplicity, we query the primary table or resolve across cultural tables weighted by CulturalAdoptionWeights:
    const FNameGenerationRuleConfig* RuleConfig = nullptr; // GS->GetAppropriateNamingRule(CharacterTemplateTag);
    if (!RuleConfig) return TEXT("Aiden");

    TArray<const FNameLexiconEntry*> ValidEntries;
    for (const FNameLexiconEntry& Entry : RuleConfig->StandaloneNames)
    {
        // Extract how likely this specific race uses this token
        float RacialMultiplier = Entry.RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        if (RacialMultiplier <= 0.0f) continue; // Exclude if race never uses this name

        if (Entry.SurnameWeight > 0.0f)
        {
            // Effective weight = Base Surname Weight * Racial Adoption Prevalence
            // (We can temporarily wrap or push entries proportional to their combined weight)
            ValidEntries.Add(&Entry);
        }
    }

    if (ValidEntries.Num() == 0) return TEXT("Traveler");

    // Select token considering racial prevalence multipliers
    float TotalWeight = 0.0f;
    for (const FNameLexiconEntry* Entry : ValidEntries)
    {
        float Multiplier = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        TotalWeight += Entry->SurnameWeight * FMath::Max(0.001f, Multiplier);
    }

    const float Roll = FMath::FRandRange(0.0f, TotalWeight);
    float Cumulative = 0.0f;
    for (const FNameLexiconEntry* Entry : ValidEntries)
    {
        float Multiplier = Entry->RacialPrevalenceWeights.LocalLookupMap.FindRef(CharacterTemplateTag);
        Cumulative += Entry->SurnameWeight * FMath::Max(0.001f, Multiplier);
        if (Roll <= Cumulative)
        {
            return Entry->Token;
        }
    }

    return ValidEntries[0]->Token;
}