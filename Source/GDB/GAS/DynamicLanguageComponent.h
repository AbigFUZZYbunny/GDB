// DynamicLanguageComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "DynamicLanguageComponent.generated.h"

class UAudioComponent;
class USoundWaveProcedural;
class IVoiceCapture;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranslationChallengeAvailableSignature, const FTranslationMiniGameChallenge&, Challenge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWordLearnedSignature, FGameplayTag, LanguageTag, const FString&, Word);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLanguageMasteryTierReachedSignature, FGameplayTag, LanguageTag, int32, NewMasteryTier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnDialogueSubtitleReceivedSignature, AActor*, Speaker, const FString&, FormattedSubtitle, FGameplayTag, SpokenLanguageTag, FGameplayTag, VolumeTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnDialogueSubtitleChunkReceivedSignature, AActor*, Speaker, const FString&, FormattedChunk, FGameplayTag, SpokenLanguageTag, FGameplayTag, VolumeTag, bool, bIsFinalChunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWrittenTextDecryptedSignature, const FString&, DecryptedText, FGameplayTag, WrittenLanguageTag, AActor*, TextSourceActor);

// Voice Synthesis & Recognition Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FOnSynthesizeSpeechRequestedSignature, AActor*, Speaker, const FString&, PlainText, FGameplayTag, VoiceProfileTag, float, PitchMultiplier, float, TempoMultiplier, FGameplayTag, EmotionTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVisemeFrameGeneratedSignature, FName, MorphTargetName, float, Weight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeechRecognitionProcessedSignature, const FVoiceInputTranscript&, Transcript);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicLanguageComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicLanguageComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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

    // Immersion Voice & Lip-Sync Delegates
    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Voice|TTS")
    FOnSynthesizeSpeechRequestedSignature OnSynthesizeSpeechRequested;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Voice|LipSync")
    FOnVisemeFrameGeneratedSignature OnVisemeFrameGenerated;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Voice|ASR")
    FOnSpeechRecognitionProcessedSignature OnSpeechRecognitionProcessed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    FGameplayTag ActiveSpokenLanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    FGameplayTag NativeLanguageTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Language")
    TArray<FGameplayTag> KnownLanguageTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Voice")
    FGameplayTag AssignedVoiceProfileTag;

    UPROPERTY(Replicated)
    FFastKnownWordArray ReplicatedKnownWords;

    TSet<FString> LocalKnownWordCache;
    void RebuildKnownWordCache();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    FString DecryptDialogueString(FGameplayTag SpokenLanguageTag, const FString& RawUtterance, FGameplayTag TargetLocalizationTag) const;

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

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language|Acoustics")
    bool CanHearUtteranceAtDistance(float DistanceUnits, float AudibleVolumeRadius, FGameplayTag VolumeTag, const FVector& SpeakerLocation, AActor* SpeakerActor = nullptr) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language|Acoustics")
    float GetAuditoryPerceptionMultiplier() const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool CanSpeakLanguage(FGameplayTag LanguageTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
    bool CanUnderstandLanguage(FGameplayTag LanguageTag) const;

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Dynamic GAS|Language")
    void ServerBroadcastSpokenUtterance(const FString& RawEnglishText, FGameplayTag VolumeTag, AActor* TargetActor = nullptr, FGameplayTag AssociatedTopicTag = FGameplayTag(), FGameplayTag EmotionTag = FGameplayTag());

    UFUNCTION(Client, Unreliable)
    void ClientReceiveHeardDialogue(AActor* Speaker, const FString& FormulatedSpokenText, FGameplayTag SpokenLanguageTag, float BaseAudibleRadius, FGameplayTag VolumeTag, FGameplayTag EmotionTag = FGameplayTag(), FGameplayTag VoiceProfileTag = FGameplayTag());

    UFUNCTION(Client, Unreliable)
    void ClientReceiveHeardDialogueChunk(AActor* Speaker, const FString& FormulatedChunk, FGameplayTag SpokenLanguageTag, float BaseAudibleRadius, FGameplayTag VolumeTag, bool bIsFinalChunk = false, FGameplayTag EmotionTag = FGameplayTag());

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

    // ============================================================================
    // IMMERSIVE TTS & SPEECH RECOGNITION (STT) EXPANSIONS
    // ============================================================================

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Dynamic GAS|Voice|STT")
    void ServerNotifyVoiceActivityStarted(AActor* ConversationalTarget);

    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Dynamic GAS|Voice|STT")
    void ServerSubmitSpeechRecognitionInput(const FVoiceInputTranscript& Transcript, AActor* ConversationalTarget = nullptr);

    // Client-Side Async Synthesis HTTP Request
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|TTS")
    void RequestTextToSpeechSynthesis(const FString& TextToSynthesize, FGameplayTag VoiceProfileTag);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|TTS")
    void CalculateAcousticVoiceModifiers(FGameplayTag EmotionTag, float& OutPitchMultiplier, float& OutTempoMultiplier) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voice")
    FGameplayTag ResolveActiveVoiceProfile() const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|TTS")
    void EnqueueTTSAudioChunk(USoundBase* AudioAsset, const FString& SpokenText, float Duration);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|LipSync")
    void ProcessLipSyncForWord(const FString& WordToken, float DurationSeconds);

    // --- Microphone Capture & VAD ---
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|STT")
    void StartVoiceCapture();

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Voice|STT")
    void StopVoiceCaptureAndSubmit(AActor* ConversationalTarget);

private:
    void CheckAndApplyLanguageBreakthrough(FGameplayTag LanguageTag, float OldComprehension, float NewComprehension);

    TMap<FGuid, FString> ServerSecretAnswers;
    TMap<FGuid, FGameplayTag> ServerChallengeLanguages;
    TMap<FGuid, FString> ServerChallengeNativeWords;

    // Audio & Viseme Subsystems
    UPROPERTY()
    TArray<FTTSAudioChunk> TTSPlaybackQueue;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> ActiveVoiceAudioComponent;

    UFUNCTION()
    void HandleAudioFinished();
    void PlayNextTTSChunk();
    void OnTTSHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, FString SpokenText);
    void OnSTTHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, AActor* TargetInterlocutor);

    TArray<FActiveViseme> ActiveVisemeQueue;
    FTSTicker::FDelegateHandle VisemeTickerHandle;
    void ManageVisemeInterpolationTick();

    // Voice Capture
    TSharedPtr<class IVoiceCapture> VoiceCapture;
    TArray<uint8> RecordedVoiceBuffer;
};