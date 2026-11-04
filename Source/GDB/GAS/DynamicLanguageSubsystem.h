// DynamicLanguageSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicLanguageSubsystem.generated.h"

UCLASS()
class GDB_API UDynamicLanguageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
	FString FormatSubtitleForListener(
		AActor* Speaker, 
		AActor* Listener, 
		const FString& RawUtterance, 
		FGameplayTag SpokenLanguageTag, 
		FGameplayTag TargetLocalizationTag,
		float AudibleRadius = 1500.0f,
		FGameplayTag VolumeTag = FGameplayTag()
	) const;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
	FString FormatWrittenTextForReader(
		AActor* Reader, 
		const FString& RawWrittenText, 
		FGameplayTag WrittenLanguageTag, 
		FGameplayTag TargetLocalizationTag
	) const;

	// Immersion Speech Recognition & TTS Audio Helpers
	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voice")
	bool GetVoiceProfile(FGameplayTag VoiceProfileTag, FVoiceProfileDef& OutProfile) const;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voice")
	float CalculateAcousticLowPassFilterFrequency(const FVector& SpeakerLoc, const FVector& ListenerLoc, AActor* ListenerActor) const;
};