// DynamicModuleManager.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "DynamicModuleManager.generated.h"

class ADynamicGameState;

UCLASS()
class GDB_API UDynamicModuleManager : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- UI/Menu Integration Endpoints ---
    
	// Scans your Saved directory and returns all custom created mod profiles found
	UFUNCTION(BlueprintCallable, Category = "Dynamic Session")
	static TArray<FString> GetAvailableModuleProfiles();

	// Saves the GameState's current runtime databases as a brand new module file set
	UFUNCTION(BlueprintCallable, Category = "Dynamic Session")
	static bool SaveCurrentRuntimeToProfile(const ADynamicGameState* InGameState, FString ProfileName);

	// Dynamic session initialization loop: merges chosen profile files into the game world
	static bool InitializeSessionWithModules(ADynamicGameState* TargetGameState, const TArray<FString>& ActiveModuleNames);
};