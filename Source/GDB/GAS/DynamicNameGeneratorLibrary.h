// DynamicNameGeneratorLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "DynamicNameGeneratorLibrary.generated.h"

class ADynamicGameState;

UCLASS()
class GDB_API UDynamicNameGeneratorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Generates a data-driven given name based on character template probabilities
	UFUNCTION(BlueprintCallable, Category = "Dynamic Naming")
	static FString GenerateGivenName(const UObject* WorldContextObject, FGameplayTag CharacterTemplateTag, ECharacterGender Gender);

	// Generates a surname based on culture rules and a registry of already used surnames
	UFUNCTION(BlueprintCallable, Category = "Dynamic Naming")
	static FString GenerateSurname(const UObject* WorldContextObject, FGameplayTag CharacterTemplateTag, const TArray<FString>& UsedSurnames);
};