// DynamicLanguageSubsystem.cpp
#include "DynamicLanguageSubsystem.h"
#include "DynamicLanguageComponent.h"
#include "DynamicSocialEntity.h"
#include "DynamicCharacter.h"

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
	FGameplayTag TargetLocalizationTag) const
{
	if (!Listener) return RawUtterance;

	// Route through listener's language component if present
	if (const UDynamicLanguageComponent* LangComp = Listener->FindComponentByClass<UDynamicLanguageComponent>())
	{
		return LangComp->DecryptDialogueString(SpokenLanguageTag, RawUtterance, TargetLocalizationTag);
	}

	return RawUtterance;
}