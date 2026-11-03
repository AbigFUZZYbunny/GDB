// DynamicLanguageComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicLanguageComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranslationChallengeAvailableSignature, const FTranslationMiniGameChallenge&, Challenge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWordLearnedSignature, FGameplayTag, LanguageTag, const FString&, Word);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLanguageMasteryTierReachedSignature, FGameplayTag, LanguageTag, int32, NewMasteryTier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnDialogueSubtitleReceivedSignature, AActor*, Speaker, const FString&, FormattedSubtitle, FGameplayTag, SpokenLanguageTag, FGameplayTag, VolumeTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnDialogueSubtitleChunkReceivedSignature, AActor*, Speaker, const FString&, FormattedChunk, FGameplayTag, SpokenLanguageTag, FGameplayTag, VolumeTag, bool, bIsFinalChunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWrittenTextDecryptedSignature, const FString&, DecryptedText, FGameplayTag, WrittenLanguageTag, AActor*, TextSourceActor);

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

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language")
    FOnLanguageMasteryTierReachedSignature OnLanguageMasteryTierReached;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language|UI")
    FOnDialogueSubtitleReceivedSignature OnDialogueSubtitleReceived;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language|UI")
    FOnDialogueSubtitleChunkReceivedSignature OnDialogueSubtitleChunkReceived;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Language|UI")
    FOnWrittenTextDecryptedSignature OnWrittenTextDecrypted;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    FGameplayTag ActiveSpokenLanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    FGameplayTag NativeLanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    TArray<FGameplayTag> KnownLanguageTags;

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

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    FString TranslateToNativeTongue(FGameplayTag LanguageTag, const FString& SourceText) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language|NonNative")
    FString GenerateNonNativeSpeakingAttempt(FGameplayTag TargetLanguageTag, const FString& IntendedEnglishText) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    FString FormulateSpokenUtterance(FGameplayTag TargetLanguageTag, const FString& IntendedEnglishText) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    float GetLanguageFluency(FGameplayTag LanguageTag) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    void ServerRegisterLanguageExposure(FGameplayTag SpokenLanguageTag, const FString& SpokenSentence);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    FGameplayTag DetermineSpokenLanguageForTarget(AActor* TargetListener, const FGameplayTagContainer& ContextTags);

    // Evaluates distance, volume radius, perception buffs, environmental weather noise, and O(1) room-based shelter attenuation
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language|Acoustics")
    bool CanHearUtteranceAtDistance(float DistanceUnits, float AudibleVolumeRadius, FGameplayTag VolumeTag, const FVector& SpeakerLocation, AActor* SpeakerActor = nullptr) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language|Acoustics")
    float GetAuditoryPerceptionMultiplier() const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool CanSpeakLanguage(FGameplayTag LanguageTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool CanUnderstandLanguage(FGameplayTag LanguageTag) const;

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Dynamic GAS|Language")
    void ServerBroadcastSpokenUtterance(const FString& RawEnglishText, FGameplayTag VolumeTag, AActor* TargetActor = nullptr, FGameplayTag AssociatedTopicTag = FGameplayTag());
    bool ServerBroadcastSpokenUtterance_Validate(const FString& RawEnglishText, FGameplayTag VolumeTag, AActor* TargetActor, FGameplayTag AssociatedTopicTag);
    
    UFUNCTION(Client, Unreliable)
    void ClientReceiveHeardDialogue(AActor* Speaker, const FString& FormulatedSpokenText, FGameplayTag SpokenLanguageTag, float BaseAudibleRadius, FGameplayTag VolumeTag);

    UFUNCTION(Client, Unreliable)
    void ClientReceiveHeardDialogueChunk(AActor* Speaker, const FString& FormulatedChunk, FGameplayTag SpokenLanguageTag, float BaseAudibleRadius, FGameplayTag VolumeTag, bool bIsFinalChunk);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    void ServerReadWrittenText(FGameplayTag WrittenLanguageTag, const FString& WrittenText, AActor* TextSourceActor);

    UFUNCTION(Client, Reliable)
    void ClientReceiveWrittenText(const FString& RawWrittenText, FGameplayTag WrittenLanguageTag, AActor* TextSourceActor);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool ServerInquireWordFromSpeaker(AActor* SpeakerActor, FGameplayTag LanguageTag, const FString& ForeignWord);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool ServerTeachRandomNativeWord(AActor* TargetStudent);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool RequestMiniGameChallengeForWord(FGameplayTag LanguageTag, const FString& NativeWord, FTranslationMiniGameChallenge& OutChallenge);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Dynamic GAS|Language")
    void ServerSubmitMiniGameAnswer(const FGuid& ChallengeGuid, const FString& SelectedAnswer);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool IsWordKnown(FGameplayTag LanguageTag, const FString& NativeWord) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Language")
    bool ServerForgetWord(FGameplayTag LanguageTag, const FString& Word);

    void InitializeFromTemplate(const FCharacterTemplateDef& TemplateDef);

private:
    void CheckAndApplyLanguageBreakthrough(FGameplayTag LanguageTag, float OldComprehension, float NewComprehension);

    TMap<FGuid, FString> ServerSecretAnswers;
    TMap<FGuid, FGameplayTag> ServerChallengeLanguages;
    TMap<FGuid, FString> ServerChallengeNativeWords;
};