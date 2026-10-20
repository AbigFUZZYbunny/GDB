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

	// Helper to format scrambled/decrypted subtitle output for any listener
	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Language")
	FString FormatSubtitleForListener(
		AActor* Speaker, 
		AActor* Listener, 
		const FString& RawUtterance, 
		FGameplayTag SpokenLanguageTag, 
		FGameplayTag TargetLocalizationTag
	) const;
};