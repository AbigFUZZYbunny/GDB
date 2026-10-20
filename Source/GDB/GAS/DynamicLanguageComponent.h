// DynamicLanguageComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicLanguageComponent.generated.h"

// --- DELEGATE DECLARATIONS ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranslationChallengeAvailableSignature, const FTranslationMiniGameChallenge&, Challenge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWordLearnedSignature, FGameplayTag, LanguageTag, const FString&, Word);

// --- COMPONENT CLASS ---
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicLanguageComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicLanguageComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language")
    FOnTranslationChallengeAvailableSignature OnTranslationChallengeReady;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language")
    FOnWordLearnedSignature OnWordLearned;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    FGameplayTag ActiveSpokenLanguageTag;

    UPROPERTY(Replicated)
    FFastKnownWordArray ReplicatedKnownWords;

    TSet<FString> LocalKnownWordCache;

    void RebuildKnownWordCache();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    FString DecryptDialogueString(
        FGameplayTag SpokenLanguageTag, 
        const FString& RawUtterance, 
        FGameplayTag TargetLocalizationTag
    ) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    void ServerRegisterLanguageExposure(FGameplayTag SpokenLanguageTag, const FString& SpokenSentence);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Language")
    bool RequestMiniGameChallengeForWord(FGameplayTag LanguageTag, const FString& NativeWord, FTranslationMiniGameChallenge& OutChallenge);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Dynamic GAS|Language")
    void ServerSubmitMiniGameAnswer(FGameplayTag LanguageTag, const FString& NativeWord, const FString& SelectedAnswer);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool IsWordKnown(FGameplayTag LanguageTag, const FString& NativeWord) const;
};