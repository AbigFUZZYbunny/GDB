// DynamicLanguageComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicLanguageComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranslationChallengeAvailableSignature, const FTranslationMiniGameChallenge&, Challenge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWordLearnedSignature, FGameplayTag, LanguageTag, const FString&, Word);

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

    // Client requests mini-game: correct answer is kept securely on the server
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool RequestMiniGameChallengeForWord(FGameplayTag LanguageTag, const FString& NativeWord, FTranslationMiniGameChallenge& OutChallenge);

    // Client submits chosen answer token or option index
    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Dynamic GAS|Language")
    void ServerSubmitMiniGameAnswer(const FGuid& ChallengeGuid, const FString& SelectedAnswer);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool IsWordKnown(FGameplayTag LanguageTag, const FString& NativeWord) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool ServerForgetWord(FGameplayTag LanguageTag, const FString& Word);

private:
    // Server-only authoritative challenge cache: ChallengeGuid -> Correct Answer String
    TMap<FGuid, FString> ServerSecretAnswers;
    TMap<FGuid, FGameplayTag> ServerChallengeLanguages;
    TMap<FGuid, FString> ServerChallengeNativeWords;
};